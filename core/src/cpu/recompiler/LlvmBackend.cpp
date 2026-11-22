#include "LlvmBackend.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <llvm/Config/llvm-config.h>
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <mutex>
#include <stdexcept>
#include <type_traits>

namespace Core::Ppc {
    namespace {
        void initialize()
        {
            static std::once_flag once;
            std::call_once(once, [] {
                if (llvm::InitializeNativeTarget() || llvm::InitializeNativeTargetAsmPrinter())
                    throw std::runtime_error("LLVM native target unavailable");
            });
        }

        template<class T>
        T checked(llvm::Expected<T> result)
        {
            if (!result)
                throw std::runtime_error(llvm::toString(result.takeError()));
            return std::move(*result);
        }

        std::unique_ptr<llvm::Module> lower(const Block &block, llvm::LLVMContext &context, const llvm::DataLayout &layout, llvm::StringRef triple)
        {
            auto module = std::make_unique<llvm::Module>("wemu.ppc.block", context);
            module->setDataLayout(layout);
            module->setTargetTriple(triple);
            llvm::IRBuilder<> ir(context);
            static_assert(std::is_standard_layout_v<ExecutionState> && std::is_standard_layout_v<MemoryRegion>);
            auto *type = llvm::FunctionType::get(ir.getInt32Ty(), {ir.getPtrTy()}, false);
            auto *function = llvm::Function::Create(type, llvm::Function::ExternalLinkage, "wemu_ppc_block", *module);
            ir.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", function));
            const auto field = [&](std::size_t offset) { return ir.CreateGEP(ir.getInt8Ty(), function->getArg(0), ir.getInt64(offset)); };
            const auto operations = block.operations();
            const bool repeatable = Block::loopsToStart(block.pc(), block.words());
            auto *registers = ir.CreateLoad(ir.getPtrTy(), field(offsetof(ExecutionState, registers)));
            const auto guestRegister = [&](unsigned index) { return ir.CreateGEP(ir.getInt32Ty(), registers, ir.getInt32(index)); };
            std::array<llvm::AllocaInst *, 32> scratch{};
            if (repeatable)
                for (unsigned i = 0; i < scratch.size(); ++i) {
                    scratch[i] = ir.CreateAlloca(ir.getInt32Ty());
                    ir.CreateStore(ir.CreateLoad(ir.getInt32Ty(), guestRegister(i)), scratch[i]);
                }
            const auto address = [&](unsigned index) -> llvm::Value * { return repeatable ? scratch[index] : guestRegister(index); };
            struct RegionValues {
                    llvm::Value *base, *size, *data;
            };
            std::array<RegionValues, 3> regions{};
            const bool hasMemory = std::any_of(block.operations().begin(), block.operations().end(),
                                               [](const auto &op) { return op.op == IntegerOp::Load || op.op == IntegerOp::Store; });
            if (hasMemory)
                for (unsigned region = 0; region < 1; ++region) {
                    const auto offset = offsetof(ExecutionState, regions) + region * sizeof(MemoryRegion);
                    regions[region] = {ir.CreateLoad(ir.getInt32Ty(), field(offset + offsetof(MemoryRegion, base))),
                                       ir.CreateLoad(ir.getInt64Ty(), field(offset + offsetof(MemoryRegion, size))),
                                       ir.CreateLoad(ir.getPtrTy(), field(offset + offsetof(MemoryRegion, data)))};
                }
            std::array<llvm::Value *, 32> values{};
            std::array<bool, 32> dirty{};
            const auto readRegister = [&](unsigned index) {
                if (!values[index])
                    values[index] = ir.CreateLoad(ir.getInt32Ty(), address(index));
                return values[index];
            };
            const auto readOperand = [&](const Operand &operand) -> llvm::Value * {
                return operand.constant ? ir.getInt32(operand.value) : readRegister(operand.value);
            };
            llvm::Value *condition = ir.CreateLoad(ir.getInt32Ty(), field(offsetof(ExecutionState, condition)));
            llvm::Value *summaryOverflow = ir.CreateLoad(ir.getInt32Ty(), field(offsetof(ExecutionState, summaryOverflow)));
            llvm::Value *link = ir.CreateLoad(ir.getInt32Ty(), field(offsetof(ExecutionState, link)));
            llvm::Value *counter = ir.CreateLoad(ir.getInt32Ty(), field(offsetof(ExecutionState, counter)));
            llvm::Value *nextPc = ir.getInt32(block.pc());
            bool conditionDirty{}, linkDirty{}, counterDirty{};
            llvm::AllocaInst *conditionSlot{}, *linkSlot{}, *counterSlot{}, *retiredSlot{};
            llvm::Value *retiredBefore = ir.getInt32(0), *budget{};
            llvm::BasicBlock *loopBody{};
            if (repeatable) {
                conditionSlot = ir.CreateAlloca(ir.getInt32Ty());
                linkSlot = ir.CreateAlloca(ir.getInt32Ty());
                counterSlot = ir.CreateAlloca(ir.getInt32Ty());
                retiredSlot = ir.CreateAlloca(ir.getInt32Ty());
                ir.CreateStore(condition, conditionSlot);
                ir.CreateStore(link, linkSlot);
                ir.CreateStore(counter, counterSlot);
                ir.CreateStore(ir.getInt32(0), retiredSlot);
                budget = ir.CreateLoad(ir.getInt32Ty(), field(offsetof(ExecutionState, instructionBudget)));
                loopBody = llvm::BasicBlock::Create(context, "loop.body", function);
                auto *tooSmall = llvm::BasicBlock::Create(context, "budget.too.small", function);
                auto *canStart = ir.CreateOr(ir.CreateICmpEQ(budget, ir.getInt32(0)), ir.CreateICmpUGE(budget, ir.getInt32(operations.size())));
                ir.CreateCondBr(canStart, loopBody, tooSmall);
                ir.SetInsertPoint(tooSmall);
                ir.CreateRet(ir.getInt32(0));
                ir.SetInsertPoint(loopBody);
                condition = ir.CreateLoad(ir.getInt32Ty(), conditionSlot);
                link = ir.CreateLoad(ir.getInt32Ty(), linkSlot);
                counter = ir.CreateLoad(ir.getInt32Ty(), counterSlot);
                retiredBefore = ir.CreateLoad(ir.getInt32Ty(), retiredSlot);
                for (const auto &op: operations) {
                    const bool compare = op.op == IntegerOp::CompareSigned || op.op == IntegerOp::CompareUnsigned;
                    const bool control = op.op == IntegerOp::Branch || op.op == IntegerOp::BranchConditional;
                    if (!compare && !control && op.op != IntegerOp::Store && op.op != IntegerOp::WriteLink && op.op != IntegerOp::WriteCounter)
                        dirty[op.destination] = true;
                    if (op.updateRegister)
                        dirty[op.updateRegister] = true;
                    conditionDirty |= op.record || compare;
                    linkDirty |= op.op == IntegerOp::WriteLink || control && (op.mask & 1);
                    counterDirty |= op.op == IntegerOp::WriteCounter || op.op == IntegerOp::BranchConditional && !(op.mask & (4u << 21));
                }
            }
            const auto compare = [&](llvm::Value *lhs, llvm::Value *rhs, bool isSigned, unsigned slot) {
                auto *lt = isSigned ? ir.CreateICmpSLT(lhs, rhs) : ir.CreateICmpULT(lhs, rhs);
                auto *gt = isSigned ? ir.CreateICmpSGT(lhs, rhs) : ir.CreateICmpUGT(lhs, rhs);
                auto *eq = ir.CreateICmpEQ(lhs, rhs);
                auto *value = ir.CreateOr(ir.CreateSelect(lt, ir.getInt32(8), ir.getInt32(0)), ir.CreateSelect(gt, ir.getInt32(4), ir.getInt32(0)));
                value = ir.CreateOr(value, ir.CreateSelect(eq, ir.getInt32(2), ir.getInt32(0)));
                value = ir.CreateOr(value, ir.CreateAnd(summaryOverflow, ir.getInt32(1)));
                const unsigned shift = (7 - slot) * 4;
                condition = ir.CreateOr(ir.CreateAnd(condition, ir.getInt32(~(15u << shift))), ir.CreateShl(value, ir.getInt32(shift)));
                conditionDirty = true;
            };
            const auto commit = [&] {
                ir.CreateStore(nextPc, field(offsetof(ExecutionState, nextPc)));
                if (conditionDirty)
                    ir.CreateStore(condition, field(offsetof(ExecutionState, condition)));
                if (linkDirty)
                    ir.CreateStore(link, field(offsetof(ExecutionState, link)));
                if (counterDirty)
                    ir.CreateStore(counter, field(offsetof(ExecutionState, counter)));
                for (unsigned i = 0; i < values.size(); ++i)
                    if (dirty[i])
                        ir.CreateStore(values[i] ? values[i] : ir.CreateLoad(ir.getInt32Ty(), address(i)), guestRegister(i));
            };
            unsigned executed = 0;
            for (const auto &op: block.operations()) {
                auto *lhs = readOperand(op.lhs);
                auto *rhs = readOperand(op.rhs);
                llvm::Value *result{}, *writtenAddress{};
                switch (op.op) {
                    case IntegerOp::CompareSigned:
                    case IntegerOp::CompareUnsigned:
                        compare(lhs, rhs, op.op == IntegerOp::CompareSigned, op.conditionField);
                        break;
                    case IntegerOp::Negate:
                        result = ir.CreateNeg(lhs);
                        break;
                    case IntegerOp::SignExtend8:
                    case IntegerOp::SignExtend16:
                        result = ir.CreateSExt(ir.CreateTrunc(lhs, ir.getIntNTy(op.op == IntegerOp::SignExtend8 ? 8 : 16)), ir.getInt32Ty());
                        break;
                    case IntegerOp::LeadingZeros:
                        result = ir.CreateIntrinsic(llvm::Intrinsic::ctlz, {ir.getInt32Ty()}, {lhs, ir.getFalse()});
                        break;
                    case IntegerOp::ReadLink:
                        result = link;
                        break;
                    case IntegerOp::ReadCounter:
                        result = counter;
                        break;
                    case IntegerOp::WriteLink:
                        link = lhs;
                        linkDirty = true;
                        break;
                    case IntegerOp::WriteCounter:
                        counter = lhs;
                        counterDirty = true;
                        break;
                    case IntegerOp::Branch:
                    case IntegerOp::BranchConditional:
                    case IntegerOp::BranchLinkRegister: {
                        const auto word = op.mask;
                        const std::uint32_t pc = block.pc() + executed * 4;
                        llvm::Value *taken = ir.getTrue();
                        if (op.op != IntegerOp::Branch) {
                            const unsigned bo = (word >> 21) & 31, bi = (word >> 16) & 31;
                            if (!(bo & 4)) {
                                counter = ir.CreateSub(counter, ir.getInt32(1));
                                counterDirty = true;
                                taken = (bo & 2) ? ir.CreateICmpEQ(counter, ir.getInt32(0)) : ir.CreateICmpNE(counter, ir.getInt32(0));
                            }
                            if (!(bo & 16)) {
                                auto *bit = ir.CreateAnd(ir.CreateLShr(condition, ir.getInt32(31 - bi)), ir.getInt32(1));
                                taken = ir.CreateAnd(taken, ir.CreateICmpEQ(bit, ir.getInt32((bo >> 3) & 1)));
                            }
                        }
                        llvm::Value *target = ir.CreateAdd(link, ir.getInt32(0x02000000u));
                        if (op.op != IntegerOp::BranchLinkRegister) {
                            std::uint32_t offset = op.op == IntegerOp::Branch ? word & 0x03FFFFFCu : word & 0xFFFCu;
                            const auto sign = op.op == IntegerOp::Branch ? 0x02000000u : 0x8000u;
                            if (offset & sign)
                                offset |= op.op == IntegerOp::Branch ? 0xFC000000u : 0xFFFF0000u;
                            target = ir.getInt32((word & 2) ? offset : pc + offset);
                        }
                        nextPc = ir.CreateSelect(taken, target, ir.getInt32(pc + 4));
                        if (word & 1) {
                            link = ir.getInt32(pc + 4 - 0x02000000u);
                            linkDirty = true;
                        }
                        break;
                    }
                    case IntegerOp::Load:
                    case IntegerOp::Store: {
                        auto *guestAddress = ir.CreateAdd(lhs, rhs); // PPC address arithmetic wraps to 32 bits.
                        const auto regionAccess = [&](const RegionValues &region) {
                            const auto &[base, size, data] = region;
                            auto *index = ir.CreateZExt(ir.CreateSub(guestAddress, base), ir.getInt64Ty());
                            auto *fits = ir.CreateAnd(ir.CreateICmpUGE(guestAddress, base),
                                                      ir.CreateICmpULE(ir.CreateAdd(index, ir.getInt64(op.memoryBytes)), size));
                            fits = ir.CreateAnd(fits, ir.CreateICmpNE(data, llvm::ConstantPointerNull::get(ir.getPtrTy())));
                            return std::pair<llvm::Value *, llvm::Value *>{fits, ir.CreateGEP(ir.getInt8Ty(), data, index)};
                        };
                        const auto [mainValid, mainPointer] = regionAccess(regions[0]);
                        auto *mainBlock = ir.GetInsertBlock();
                        auto *other = llvm::BasicBlock::Create(context, "memory.other", function);
                        auto *access = llvm::BasicBlock::Create(context, "memory.access", function);
                        auto *fallback = llvm::BasicBlock::Create(context, "memory.fallback", function);
                        auto *fast = ir.CreateCondBr(mainValid, access, other);
                        fast->setMetadata(llvm::LLVMContext::MD_prof, llvm::MDBuilder(context).createBranchWeights(2000, 1));
                        ir.SetInsertPoint(other);
                        llvm::Value *otherValid = ir.getFalse();
                        llvm::Value *otherPointer = llvm::ConstantPointerNull::get(ir.getPtrTy());
                        for (unsigned region = 1; region < 3; ++region) {
                            const auto offset = offsetof(ExecutionState, regions) + region * sizeof(MemoryRegion);
                            const RegionValues descriptor{ir.CreateLoad(ir.getInt32Ty(), field(offset + offsetof(MemoryRegion, base))),
                                                          ir.CreateLoad(ir.getInt64Ty(), field(offset + offsetof(MemoryRegion, size))),
                                                          ir.CreateLoad(ir.getPtrTy(), field(offset + offsetof(MemoryRegion, data)))};
                            const auto [valid, pointer] = regionAccess(descriptor);
                            otherPointer = ir.CreateSelect(valid, pointer, otherPointer);
                            otherValid = ir.CreateOr(otherValid, valid);
                        }
                        ir.CreateCondBr(otherValid, access, fallback);
                        ir.SetInsertPoint(fallback);
                        commit();
                        ir.CreateRet(ir.CreateAdd(retiredBefore, ir.getInt32(executed)));
                        ir.SetInsertPoint(access);
                        auto *pointer = ir.CreatePHI(ir.getPtrTy(), 2);
                        pointer->addIncoming(mainPointer, mainBlock);
                        pointer->addIncoming(otherPointer, other);
                        auto *memoryType = ir.getIntNTy(op.memoryBytes * 8);
                        if (op.op == IntegerOp::Load) {
                            auto *load = ir.CreateLoad(memoryType, pointer);
                            load->setAlignment(llvm::Align(1));
                            llvm::Value *value = load;
                            if (std::endian::native == std::endian::little && op.memoryBytes > 1)
                                value = ir.CreateIntrinsic(llvm::Intrinsic::bswap, {memoryType}, {value});
                            result = op.signedLoad ? ir.CreateSExtOrTrunc(value, ir.getInt32Ty()) : ir.CreateZExtOrTrunc(value, ir.getInt32Ty());
                        } else {
                            writtenAddress = guestAddress;
                            llvm::Value *value = ir.CreateTruncOrBitCast(readRegister(op.destination), memoryType);
                            if (std::endian::native == std::endian::little && op.memoryBytes > 1)
                                value = ir.CreateIntrinsic(llvm::Intrinsic::bswap, {memoryType}, {value});
                            auto *store = ir.CreateStore(value, pointer);
                            store->setAlignment(llvm::Align(1));
                        }
                        if (op.updateRegister) {
                            values[op.updateRegister] = guestAddress;
                            dirty[op.updateRegister] = true;
                        }
                        break;
                    }
                    case IntegerOp::Add:
                        result = ir.CreateAdd(lhs, rhs);
                        break;
                    case IntegerOp::Subtract:
                        result = ir.CreateSub(lhs, rhs);
                        break;
                    case IntegerOp::Multiply:
                        result = ir.CreateMul(lhs, rhs);
                        break;
                    case IntegerOp::And:
                        result = ir.CreateAnd(lhs, rhs);
                        break;
                    case IntegerOp::Or:
                        result = ir.CreateOr(lhs, rhs);
                        break;
                    case IntegerOp::Xor:
                        result = ir.CreateXor(lhs, rhs);
                        break;
                    case IntegerOp::RotateMask:
                    case IntegerOp::RotateInsert:
                        result = ir.CreateAnd(ir.CreateIntrinsic(llvm::Intrinsic::fshl, {ir.getInt32Ty()}, {lhs, lhs, rhs}), ir.getInt32(op.mask));
                        if (op.op == IntegerOp::RotateInsert)
                            result = ir.CreateOr(result, ir.CreateAnd(readRegister(op.destination), ir.getInt32(~op.mask)));
                        break;
                    case IntegerOp::ShiftLeft:
                    case IntegerOp::ShiftRight: {
                        // PPC uses six count bits: 32..63 produce zero. Mask the
                        // LLVM shift itself to avoid poison from oversized counts.
                        auto *count = ir.CreateAnd(rhs, ir.getInt32(31));
                        auto *shifted = op.op == IntegerOp::ShiftLeft ? ir.CreateShl(lhs, count) : ir.CreateLShr(lhs, count);
                        result = ir.CreateSelect(ir.CreateICmpNE(ir.CreateAnd(rhs, ir.getInt32(32)), ir.getInt32(0)), ir.getInt32(0), shifted);
                        break;
                    }
                }
                // Plain i32 add wraps; no nsw/nuw assumptions about guest overflow.
                if (result) {
                    values[op.destination] = result;
                    dirty[op.destination] = true;
                    if (op.record)
                        compare(result, ir.getInt32(0), true, 0);
                }
                ++executed;
                if (op.op != IntegerOp::Branch && op.op != IntegerOp::BranchConditional && op.op != IntegerOp::BranchLinkRegister)
                    nextPc = ir.getInt32(block.pc() + executed * 4);
                if (writtenAddress) {
                    auto *begin = ir.CreateZExt(writtenAddress, ir.getInt64Ty());
                    auto *end = ir.CreateAdd(begin, ir.getInt64(op.memoryBytes));
                    auto *overlap = ir.CreateAnd(ir.CreateICmpULT(begin, ir.getInt64(std::uint64_t(block.pc()) + block.words().size() * 4)),
                                                 ir.CreateICmpUGT(end, ir.getInt64(block.pc())));
                    auto *changed = llvm::BasicBlock::Create(context, "code.changed", function);
                    auto *unchanged = llvm::BasicBlock::Create(context, "code.unchanged", function);
                    auto *guard = ir.CreateCondBr(overlap, changed, unchanged);
                    guard->setMetadata(llvm::LLVMContext::MD_prof, llvm::MDBuilder(context).createBranchWeights(1, 2000));
                    ir.SetInsertPoint(changed);
                    commit();
                    ir.CreateRet(ir.CreateAdd(retiredBefore, ir.getInt32(executed)));
                    ir.SetInsertPoint(unchanged);
                }
            }
            auto *retired = ir.CreateAdd(retiredBefore, ir.getInt32(executed));
            if (repeatable) {
                for (unsigned i = 0; i < values.size(); ++i)
                    if (dirty[i])
                        ir.CreateStore(values[i], scratch[i]);
                ir.CreateStore(condition, conditionSlot);
                ir.CreateStore(link, linkSlot);
                ir.CreateStore(counter, counterSlot);
                ir.CreateStore(retired, retiredSlot);
                auto *nextCount = ir.CreateAdd(retired, ir.getInt32(executed));
                auto *again = ir.CreateAnd(ir.CreateICmpEQ(nextPc, ir.getInt32(block.pc())),
                                           ir.CreateAnd(ir.CreateICmpULE(nextCount, budget), ir.CreateICmpULE(nextCount, ir.getInt32(64))));
                auto *done = llvm::BasicBlock::Create(context, "loop.done", function);
                ir.CreateCondBr(again, loopBody, done);
                ir.SetInsertPoint(done);
            }
            commit();
            ir.CreateRet(retired);
            llvm::LoopAnalysisManager loops;
            llvm::FunctionAnalysisManager functions;
            llvm::CGSCCAnalysisManager callGraphs;
            llvm::ModuleAnalysisManager modules;
            llvm::PassBuilder passes;
            passes.registerModuleAnalyses(modules);
            passes.registerCGSCCAnalyses(callGraphs);
            passes.registerFunctionAnalyses(functions);
            passes.registerLoopAnalyses(loops);
            passes.crossRegisterProxies(loops, functions, callGraphs, modules);
            auto pipeline = passes.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
            pipeline.run(*module, modules);
            std::string error;
            llvm::raw_string_ostream out(error);
            if (llvm::verifyModule(*module, &out))
                throw std::runtime_error(error);
            return module;
        }
    } // namespace

    struct NativeCompiler::Impl {
            std::unique_ptr<llvm::orc::LLJIT> jit;
            std::mutex mutex;
            std::uint64_t nextId{};
    };

    NativeCompiler::NativeCompiler() : m_impl(std::make_shared<Impl>())
    {
        initialize();
        m_impl->jit = checked(llvm::orc::LLJITBuilder().create());
    }

    NativeCompiler::~NativeCompiler() = default;

    struct NativeBlock::Impl {
            std::shared_ptr<NativeCompiler::Impl> compiler;
            llvm::orc::JITDylib *dylib{};
            unsigned (*entry)(ExecutionState *){};

            explicit Impl(std::shared_ptr<NativeCompiler::Impl> owner) : compiler(std::move(owner))
            {
                std::lock_guard lock(compiler->mutex);
                auto result = compiler->jit->createJITDylib("wemu.block." + std::to_string(compiler->nextId++));
                if (!result)
                    throw std::runtime_error(llvm::toString(result.takeError()));
                dylib = &*result;
            }

            ~Impl()
            {
                // Each block has its own symbol scope, including linked AOT objects.
                // Keep the engine alive until its last block has released its code.
                std::lock_guard lock(compiler->mutex);
                if (auto error = compiler->jit->getExecutionSession().removeJITDylib(*dylib))
                    llvm::logAllUnhandledErrors(std::move(error), llvm::errs(), "WEMU native code release: ");
            }
    };

    NativeBlock::NativeBlock(NativeCompiler &compiler, const Block &block) : m_impl(std::make_unique<Impl>(compiler.m_impl))
    {
        std::lock_guard lock(m_impl->compiler->mutex);
        auto &jit = *m_impl->compiler->jit;
        auto context = std::make_unique<llvm::LLVMContext>();
        auto module = lower(block, *context, jit.getDataLayout(), jit.getTargetTriple().str());
        if (auto error = jit.addIRModule(*m_impl->dylib, llvm::orc::ThreadSafeModule(std::move(module), std::move(context))))
            throw std::runtime_error(llvm::toString(std::move(error)));
        m_impl->entry = checked(jit.lookup(*m_impl->dylib, "wemu_ppc_block")).toPtr<unsigned(ExecutionState *)>();
    }

    NativeBlock::~NativeBlock() = default;

    NativeBlock::NativeBlock(NativeCompiler &compiler, std::span<const std::uint8_t> object) : m_impl(std::make_unique<Impl>(compiler.m_impl))
    {
        std::lock_guard lock(m_impl->compiler->mutex);
        auto &jit = *m_impl->compiler->jit;
        const llvm::StringRef bytes(reinterpret_cast<const char *>(object.data()), object.size());
        if (auto error = jit.addObjectFile(*m_impl->dylib, llvm::MemoryBuffer::getMemBufferCopy(bytes)))
            throw std::runtime_error(llvm::toString(std::move(error)));
        m_impl->entry = checked(jit.lookup(*m_impl->dylib, "wemu_ppc_block")).toPtr<unsigned(ExecutionState *)>();
    }

    void NativeBlock::execute(std::span<std::uint32_t, 32> registers) const
    {
        ExecutionState state{registers.data(), {}};
        (void) execute(state);
    }

    unsigned NativeBlock::execute(ExecutionState &state) const { return m_impl->entry(&state); }

    std::vector<std::uint8_t> emitObject(const Block &block)
    {
        initialize();
        auto builder = checked(llvm::orc::JITTargetMachineBuilder::detectHost());
        auto target = checked(builder.createTargetMachine());
        llvm::LLVMContext context;
        auto module = lower(block, context, target->createDataLayout(), target->getTargetTriple().str());
        llvm::SmallVector<char, 0> bytes;
        llvm::raw_svector_ostream output(bytes);
        llvm::legacy::PassManager passes;
        if (target->addPassesToEmitFile(passes, output, nullptr, llvm::CodeGenFileType::ObjectFile))
            throw std::runtime_error("LLVM target cannot emit object code");
        passes.run(*module);
        return {bytes.begin(), bytes.end()};
    }

    std::string nativeObjectIdentity()
    {
        initialize();
        auto builder = checked(llvm::orc::JITTargetMachineBuilder::detectHost());
        auto target = checked(builder.createTargetMachine());
        // Bump the semantics/ABI version whenever frontend or lowering behavior changes.
        return std::string("wemu-ppc-state-abi4-semantics5\n") + LLVM_VERSION_STRING + "\n" + WEMU_PPC_COMPILER_FINGERPRINT + "\n" +
               builder.getTargetTriple().str() + "\n" + builder.getCPU() + "\n" + builder.getFeatures().getString() + "\n" +
               target->createDataLayout().getStringRepresentation();
    }
} // namespace Core::Ppc
