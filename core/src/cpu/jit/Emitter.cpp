/*
** EPITECH PROJECT, 2026
** core
** File description:
** Emitter -- x86-64 code generation for one PowerPC block (internal to the recompiler)
*/

#include "Emitter.hpp"

#include <bit>
#include <cstring>
#include <optional>
#include <utility>

#include "cpu/jit/Callouts.hpp"
#include "cpu/memory/Memory.hpp"
#include "cpu/types/InstructionTraits.hpp"

#define CTX(field) offsetof(Context, field)

namespace Core::Jit {
    namespace {
        constexpr std::uint32_t code(const ExitReason reason) { return static_cast<std::uint32_t>(reason); }


        // Guest address minus a region base, as a 32-bit lea displacement that wraps the same way.
        constexpr std::int32_t minus(const std::uint32_t base) { return static_cast<std::int32_t>(0u - base); }

        struct LoadResult {
                std::uint64_t value;
                std::uint64_t fault;
        };

        // Accesses that miss main RAM and the stack: other regions through Memory, or a fault.
        LoadResult loadSlow(const Memory *memory, const std::uint32_t ea, const std::uint32_t bytes) noexcept
        {
            const std::uint8_t *host = memory->hostPtr(ea, bytes);
            if (!host)
                return {0, 1};
            std::uint64_t value = 0;
            switch (bytes) {
                case 1:
                    value = *host;
                    break;
                case 2: {
                    std::uint16_t v;
                    std::memcpy(&v, host, 2);
                    value = std::byteswap(v);
                    break;
                }
                case 4: {
                    std::uint32_t v;
                    std::memcpy(&v, host, 4);
                    value = std::byteswap(v);
                    break;
                }
                default: {
                    std::uint64_t v;
                    std::memcpy(&v, host, 8);
                    value = std::byteswap(v);
                    break;
                }
            }
            return {value, 0};
        }

        // Main-RAM stores into code or watched pages, page-crossing stores and other regions go
        // through Memory::write for its tracking. Returns 0, 1 when decoded code was written (the
        // block must leave after this instruction) or 2 when unmapped (before it).
        std::uint32_t storeSlow(Memory *memory, const std::uint32_t ea, const std::uint64_t value, const std::uint32_t bytes) noexcept
        {
            if (!std::as_const(*memory).hostPtr(ea, bytes))
                return 2;
            try {
                switch (bytes) {
                    case 1:
                        memory->write<std::uint8_t>(ea, static_cast<std::uint8_t>(value));
                        break;
                    case 2:
                        memory->write<std::uint16_t>(ea, static_cast<std::uint16_t>(value));
                        break;
                    case 4:
                        memory->write<std::uint32_t>(ea, static_cast<std::uint32_t>(value));
                        break;
                    default:
                        memory->write<std::uint64_t>(ea, value);
                        break;
                }
            } catch (...) { // only unmapped stores throw, and hostPtr rejected those above
                return 2;
            }
            if (!memory->codeWritten())
                return 0;
            memory->clearCodeWritten();
            return 1;
        }
    } // namespace

    Emitter::Emitter(std::uint8_t *at, const std::size_t capacity, const GuestLayout &layout, const Stubs &stubs) :
        Xbyak::CodeGenerator(capacity, at), m_layout(layout), m_stubs(stubs)
    {
    }

    void Emitter::callHost(const void *fn)
    {
        const auto next = reinterpret_cast<std::intptr_t>(getCurr()) + 5;
        const auto delta = reinterpret_cast<std::intptr_t>(fn) - next;
        if (delta == static_cast<std::int32_t>(delta)) {
            call(fn);
        } else {
            mov(rax, reinterpret_cast<std::uintptr_t>(fn));
            call(rax);
        }
    }

    void Emitter::emitStubs(Stubs &stubs, const TableEntry *table)
    {
        Xbyak::Label lookup, miss, exit;
        // void enter(Context *context, std::uint32_t pc): System V entry from the interpreter.
        stubs.enter = getCurr<void (*)(Context *, std::uint32_t)>();
        push(rbp);
        mov(rbp, rsp);
        push(rbx);
        push(r12);
        push(r13);
        push(r14);
        push(r15);
        sub(rsp, 8); // calls from blocks see a 16-byte aligned stack
        mov(r15, rdi);
        mov(rbx, qword[r15 + CTX(gprBase)]);
        mov(r12, qword[r15 + CTX(ramBias)]);
        mov(r13, qword[r15 + CTX(retired)]);
        mov(r14, qword[r15 + CTX(slowPages)]);
        mov(eax, esi);

        // lookup: eax = next pc. Jumps to its compiled block, or leaves with LookupMiss.
        L(lookup);
        stubs.lookup = getCurr();
        mov(ecx, eax);
        and_(ecx, kTableMask << 2);
        lea(rdx, ptr[at(table)]);
        cmp(dword[rdx + rcx * 4], eax);
        jne(miss);
        jmp(ptr[rdx + rcx * 4 + 8]);
        L(miss);
        stubs.miss = getCurr();
        mov(dword[r15 + CTX(exitPc)], eax);
        mov(dword[r15 + CTX(reason)], code(ExitReason::LookupMiss));

        // exit: back to the interpreter; the reason fields are already stored.
        L(exit);
        stubs.exit = getCurr();
        mov(qword[r15 + CTX(retired)], r13);
        add(rsp, 8);
        pop(r15);
        pop(r14);
        pop(r13);
        pop(r12);
        pop(rbx);
        pop(rbp);
        ret();

        // calloutExit: eax = the call-out's ExitReason; blockPc and blockStores are stored.
        stubs.calloutExit = getCurr();
        mov(dword[r15 + CTX(reason)], eax);
        cmp(eax, code(ExitReason::Done));
        jne(exit);
        mov(ecx, dword[rbx + m_layout.nextPc]);
        mov(dword[r15 + CTX(exitPc)], ecx);
        jmp(exit);

        // spinExit: eax = next pc of a block without stores that reached the spin threshold.
        stubs.spinExit = getCurr();
        mov(dword[r15 + CTX(exitPc)], eax);
        mov(dword[r15 + CTX(reason)], code(ExitReason::Done));
        mov(dword[r15 + CTX(blockStores)], 0);
        jmp(exit);

        // hook: eax = a hooked pc, reached through the lookup table. The loop checks the service
        // tick before a hook, so a due tick leaves first. The hook returns to LR (m_pc).
        Xbyak::Label hookLeft;
        stubs.hook = getCurr();
        cmp(r13, qword[r15 + CTX(retireLimit)]);
        jae(miss, T_NEAR);
        lea(rdi, ptr[rbx + m_layout.cpu]);
        mov(esi, eax);
        callHost(reinterpret_cast<const void *>(&runHook));
        test(eax, eax);
        jnz(hookLeft);
        mov(eax, dword[rbx + m_layout.pc]);
        jmp(lookup, T_NEAR);
        L(hookLeft);
        mov(dword[r15 + CTX(reason)], eax);
        mov(ecx, dword[rbx + m_layout.pc]);
        mov(dword[r15 + CTX(exitPc)], ecx);
        jmp(exit, T_NEAR);
    }

    void Emitter::emitBlock(const std::uint32_t pc, const std::span<const GuestInstruction> instrs, const bool stores, BlockSlot *slot)
    {
        m_blockPc = pc;
        m_count = static_cast<std::uint32_t>(instrs.size());
        m_stores = stores;
        Xbyak::Label budget;

        // The interpreter loop's service-tick check before a block. Blocks are only reachable
        // while their words are current (X64Jit::sync unlinks them after a code write).
        cmp(r13, qword[r15 + CTX(retireLimit)]);
        jae(budget, T_NEAR);
        add(r13, m_count);
        if (m_layout.profile)
            inc(qword[at(&slot->executions)]);

        bool terminated = false;
        for (m_index = 0; m_index < m_count; ++m_index) {
            m_pc = pc + m_index * kInstructionBytes;
            m_current = instrs[m_index];
            m_crLive = m_crInEdx;
            m_crInEdx = false;
            const EncodedInstruction ei(m_current.raw);
            const InstructionID id = m_current.id;
            const std::uint32_t mask = m_layout.nativeMask;
            // A native branch ends the block itself: it chains to the next block on every path.
            if ((mask & FamilyBranch) && emitBranch(ei, id)) {
                terminated = true;
                break;
            }
            m_crRecorded = false;
            if ((mask & (FamilyInteger | FamilyCompare)) && emitInteger(ei, id)) {
                m_crInEdx = m_crRecorded;
                continue;
            }
            const bool native = ((mask & FamilySystem) && emitSystem(ei, id)) || ((mask & (FamilyLoad | FamilyStore)) && emitMemory(ei, id)) ||
                                ((mask & FamilyFloat) && emitFloat(ei, id)) || ((mask & FamilyPaired) && emitPaired(ei, id));
            if (!native) {
                emitCallout(m_current);
                if (endsBlock(m_current.raw)) {
                    mov(eax, dword[rbx + m_layout.nextPc]);
                    exitIndirect();
                    terminated = true;
                }
            }
        }
        if (!terminated)
            exitTo(pc + m_count * kInstructionBytes);

        L(budget);
        mov(dword[r15 + CTX(exitPc)], pc);
        mov(dword[r15 + CTX(reason)], code(ExitReason::Budget));
        jmp(m_stubs.exit, T_NEAR);
        for (std::size_t i = 0; i < m_cold.size(); ++i)
            m_cold[i]();
    }

    void Emitter::emitCallout(const GuestInstruction &instr)
    {
        ++m_callouts;
        calloutCall(instr, m_pc, m_index);
    }

    void Emitter::calloutCall(const GuestInstruction &instr, const std::uint32_t pc, const std::uint32_t index)
    {
        Xbyak::Label &failed = newLabel();
        lea(rdi, ptr[rbx + m_layout.cpu]);
        mov(esi, instr.raw);
        mov(edx, pc);
        callHost(reinterpret_cast<const void *>(callout(instr.id)));
        test(eax, eax);
        jnz(failed, T_NEAR);
        cold([this, &failed, skipped = m_count - index - 1] {
            L(failed);
            // Instructions after the one that left did not retire.
            if (skipped)
                sub(r13, skipped);
            mov(dword[r15 + CTX(blockPc)], m_blockPc);
            mov(dword[r15 + CTX(blockStores)], m_stores ? 1 : 0);
            jmp(m_stubs.calloutExit, T_NEAR);
        });
    }

    void Emitter::spinCheck(const std::optional<std::uint32_t> target)
    {
        // The interpreter's noteBlockRetired after a whole block: blocks with stores record the
        // retired count, the others leave once the spin threshold is reached (eax = next pc).
        const std::uint64_t threshold = m_layout.spinThreshold;
        if (threshold == ~std::uint64_t{0})
            return;
        if (m_stores) {
            if (threshold <= 0x7FFFFFFF) {
                lea(rcx, ptr[r13 + static_cast<std::int32_t>(threshold)]);
            } else {
                mov(rcx, threshold);
                add(rcx, r13);
            }
            mov(qword[r15 + CTX(spinDeadline)], rcx);
            return;
        }
        cmp(r13, qword[r15 + CTX(spinDeadline)]);
        if (!target) {
            jae(m_stubs.spinExit);
            return;
        }
        Xbyak::Label &spin = newLabel();
        jae(spin, T_NEAR);
        cold([this, &spin, next = *target] {
            L(spin);
            mov(eax, next);
            jmp(m_stubs.spinExit, T_NEAR);
        });
    }

    void Emitter::exitTo(const std::uint32_t target)
    {
        spinCheck(target);
        // Direct jump, patched to the target block once compiled; until then through the lookup.
        Xbyak::Label &jump = newLabel();
        Xbyak::Label &fallback = newLabel();
        L(jump);
        jmp(fallback, T_NEAR);
        m_pendingLinks.push_back({target, &jump, &fallback});
        cold([this, &fallback, target] {
            L(fallback);
            mov(eax, target);
            jmp(m_stubs.lookup, T_NEAR);
        });
    }

    void Emitter::exitIndirect()
    {
        // eax = next pc. An inline lookup gives each indirect branch its own prediction.
        spinCheck(std::nullopt);
        mov(ecx, eax);
        and_(ecx, kTableMask << 2);
        lea(rdx, ptr[at(m_layout.table)]);
        cmp(dword[rdx + rcx * 4], eax);
        jne(m_stubs.miss);
        jmp(ptr[rdx + rcx * 4 + 8]);
    }

    std::vector<Link> Emitter::links() const
    {
        std::vector<Link> out;
        out.reserve(m_pendingLinks.size());
        for (const auto &link: m_pendingLinks)
            out.push_back({link.target, const_cast<std::uint8_t *>(link.jump->getAddress()), link.fallback->getAddress()});
        return out;
    }

    Xbyak::Label &Emitter::exitBefore()
    {
        Xbyak::Label &label = newLabel();
        cold([this, &label, pc = m_pc, index = m_index] {
            L(label);
            sub(r13, m_count - index);
            mov(dword[r15 + CTX(exitPc)], pc);
            mov(dword[r15 + CTX(blockPc)], m_blockPc);
            mov(dword[r15 + CTX(index)], index);
            mov(dword[r15 + CTX(blockStores)], m_stores ? 1 : 0);
            mov(dword[r15 + CTX(reason)], code(ExitReason::Suffix));
            jmp(m_stubs.exit, T_NEAR);
        });
        return label;
    }

    Xbyak::Label &Emitter::exitAfter()
    {
        Xbyak::Label &label = newLabel();
        cold([this, &label, pc = m_pc, index = m_index] {
            L(label);
            if (const auto skipped = m_count - index - 1)
                sub(r13, skipped);
            mov(dword[r15 + CTX(exitPc)], pc + kInstructionBytes);
            mov(dword[r15 + CTX(blockStores)], m_stores ? 1 : 0);
            mov(dword[r15 + CTX(reason)], code(ExitReason::Done));
            jmp(m_stubs.exit, T_NEAR);
        });
        return label;
    }

    void Emitter::recordCr0()
    {
        // Interpreter::updateCR0: LT/GT/EQ of the signed result, SO copied from XER.
        test(eax, eax);
        mov(ecx, 2);
        mov(edx, 4);
        cmovg(ecx, edx);
        mov(edx, 8);
        cmovl(ecx, edx);
        mov(edx, xer());
        and_(edx, 1);
        or_(ecx, edx);
        setCrField(0);
        m_crRecorded = true;
    }

    void Emitter::setCrField(const unsigned field)
    {
        const unsigned shift = (7 - field) * 4;
        if (shift)
            shl(ecx, static_cast<int>(shift));
        mov(edx, cr());
        and_(edx, ~(0xFu << shift));
        or_(edx, ecx);
        mov(cr(), edx);
    }

    void Emitter::setCarry(const bool inverted)
    {
        if (inverted)
            setnc(cl);
        else
            setc(cl);
        movzx(ecx, cl);
        mov(edx, xer());
        and_(edx, ~4u);
        lea(edx, ptr[rdx + rcx * 4]);
        mov(xer(), edx);
    }

    void Emitter::loadCarry() { bt(xer(), 2); }

    void Emitter::load(const unsigned bytes, const bool signExtend)
    {
        Xbyak::Label &other = newLabel();
        Xbyak::Label &done = newLabel();
        const auto access = [this, bytes, signExtend](const Xbyak::RegExp &address) {
            switch (bytes) {
                case 1:
                    movzx(eax, byte[address]);
                    break;
                case 2:
                    movbe(ax, word[address]);
                    if (signExtend)
                        movsx(eax, ax);
                    else
                        movzx(eax, ax);
                    break;
                case 4:
                    movbe(eax, dword[address]);
                    break;
                default:
                    movbe(rax, qword[address]);
                    break;
            }
        };
        lea(edx, ptr[rcx + minus(Memory::ApplicationCode)]);
        cmp(edx, m_layout.ramBytes - bytes);
        ja(other, T_NEAR);
        access(r12 + rcx);
        L(done);
        Xbyak::Label &fault = exitBefore();
        cold([this, &other, &done, &fault, access, bytes, signExtend] {
            Xbyak::Label helper;
            L(other);
            lea(edx, ptr[rcx + minus(Memory::STACK_BASE)]);
            cmp(edx, Memory::STACK_SIZE - bytes);
            ja(helper, T_NEAR);
            mov(r8, qword[r15 + CTX(stackHost)]);
            access(r8 + rdx);
            jmp(done, T_NEAR);
            L(helper);
            mov(dword[r15 + CTX(scratch)], ecx);
            mov(rdi, reinterpret_cast<std::uintptr_t>(m_layout.memory));
            mov(esi, ecx);
            mov(edx, bytes);
            callHost(reinterpret_cast<const void *>(&loadSlow));
            test(rdx, rdx);
            jnz(fault, T_NEAR);
            if (bytes == 2 && signExtend)
                movsx(eax, ax);
            mov(ecx, dword[r15 + CTX(scratch)]);
            jmp(done, T_NEAR);
        });
    }

    void Emitter::store(const unsigned bytes, const std::function<void()> &tail, const bool replayOnCodeWrite)
    {
        Xbyak::Label &slow = newLabel();
        Xbyak::Label &done = newLabel();
        const auto access = [this, bytes](const Xbyak::RegExp &address) {
            switch (bytes) {
                case 1:
                    mov(byte[address], al);
                    break;
                case 2:
                    movbe(word[address], ax);
                    break;
                case 4:
                    movbe(dword[address], eax);
                    break;
                default:
                    movbe(qword[address], rax);
                    break;
            }
        };
        lea(edx, ptr[rcx + minus(Memory::ApplicationCode)]);
        cmp(edx, m_layout.ramBytes - bytes);
        ja(slow, T_NEAR);
        // Code and watched pages need Memory's tracking; a store crossing a page checks both.
        mov(r8d, edx);
        shr(r8d, Memory::kCodePageShift);
        mov(r9d, r8d);
        shr(r9d, 6);
        mov(r9, qword[r14 + r9 * 8]);
        bt(r9, r8);
        jc(slow, T_NEAR);
        if (bytes > 1) {
            mov(r8d, edx);
            and_(r8d, (1u << Memory::kCodePageShift) - 1);
            cmp(r8d, (1u << Memory::kCodePageShift) - bytes);
            ja(slow, T_NEAR);
        }
        access(r12 + rcx);
        L(done);
        if (tail)
            tail();
        Xbyak::Label &fault = exitBefore();
        Xbyak::Label &after = replayOnCodeWrite ? fault : exitAfter();
        cold([this, &slow, &done, &fault, &after, access, bytes, tail, replayOnCodeWrite] {
            Xbyak::Label helper;
            L(slow);
            lea(edx, ptr[rcx + minus(Memory::STACK_BASE)]);
            cmp(edx, Memory::STACK_SIZE - bytes);
            ja(helper, T_NEAR);
            mov(r8, qword[r15 + CTX(stackHost)]);
            access(r8 + rdx);
            jmp(done, T_NEAR);
            L(helper);
            mov(dword[r15 + CTX(scratch)], ecx);
            mov(rdi, reinterpret_cast<std::uintptr_t>(m_layout.memory));
            mov(esi, ecx);
            mov(rdx, rax);
            mov(ecx, bytes);
            callHost(reinterpret_cast<const void *>(&storeSlow));
            mov(ecx, dword[r15 + CTX(scratch)]);
            cmp(eax, 1);
            ja(fault, T_NEAR);
            jne(done, T_NEAR);
            // Decoded code was written: finish the instruction, then leave right after it.
            if (tail && !replayOnCodeWrite)
                tail();
            jmp(after, T_NEAR);
        });
    }
} // namespace Core::Jit
