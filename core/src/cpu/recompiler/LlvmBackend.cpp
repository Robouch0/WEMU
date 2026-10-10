#include "LlvmBackend.hpp"

#include <array>
#include <llvm/Config/llvm-config.h>
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <mutex>
#include <stdexcept>

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
            auto *type = llvm::FunctionType::get(ir.getVoidTy(), {ir.getPtrTy()}, false);
            auto *function = llvm::Function::Create(type, llvm::Function::ExternalLinkage, "wemu_ppc_block", *module);
            ir.SetInsertPoint(llvm::BasicBlock::Create(context, "entry", function));
            const auto address = [&](unsigned index) { return ir.CreateGEP(ir.getInt32Ty(), function->getArg(0), ir.getInt32(index)); };
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
            for (const auto &op: block.operations()) {
                auto *lhs = readOperand(op.lhs);
                auto *rhs = readOperand(op.rhs);
                llvm::Value *result{};
                switch (op.op) {
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
                values[op.destination] = result;
                dirty[op.destination] = true;
            }
            // This subset has no memory accesses, calls or fault exits. Keep
            // intermediates in SSA and commit only final modified registers.
            for (unsigned i = 0; i < values.size(); ++i)
                if (dirty[i])
                    ir.CreateStore(values[i], address(i));
            ir.CreateRetVoid();
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
            void (*entry)(std::uint32_t *){};

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
        m_impl->entry = checked(jit.lookup(*m_impl->dylib, "wemu_ppc_block")).toPtr<void(std::uint32_t *)>();
    }

    NativeBlock::~NativeBlock() = default;

    NativeBlock::NativeBlock(NativeCompiler &compiler, std::span<const std::uint8_t> object) : m_impl(std::make_unique<Impl>(compiler.m_impl))
    {
        std::lock_guard lock(m_impl->compiler->mutex);
        auto &jit = *m_impl->compiler->jit;
        const llvm::StringRef bytes(reinterpret_cast<const char *>(object.data()), object.size());
        if (auto error = jit.addObjectFile(*m_impl->dylib, llvm::MemoryBuffer::getMemBufferCopy(bytes)))
            throw std::runtime_error(llvm::toString(std::move(error)));
        m_impl->entry = checked(jit.lookup(*m_impl->dylib, "wemu_ppc_block")).toPtr<void(std::uint32_t *)>();
    }

    void NativeBlock::execute(std::span<std::uint32_t, 32> registers) const { m_impl->entry(registers.data()); }

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
        return std::string("wemu-ppc-register-abi1-semantics1\n") + LLVM_VERSION_STRING + "\n" + WEMU_PPC_COMPILER_FINGERPRINT + "\n" +
               builder.getTargetTriple().str() + "\n" + builder.getCPU() + "\n" + builder.getFeatures().getString() + "\n" +
               target->createDataLayout().getStringRepresentation();
    }
} // namespace Core::Ppc
