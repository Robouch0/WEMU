/*
** EPITECH PROJECT, 2026
** core
** File description:
** CoreinitExtra -- second wave of coreinit HLE: events/conds/alarms, GHS runtime, cache ops,
** report/panic visibility, UC/MCP system config, OSDynLoad exports, zlib125 bridge
*/

#include "CoreinitExtra.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <zlib.h>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/memory/Memory.hpp"
#include "utils/Logger.hpp"

namespace {

    std::string readGuestString(Core::Interpreter &cpu, std::uint32_t addr, std::size_t maxLen = 1024)
    {
        std::string out;
        for (std::size_t i = 0; i < maxLen; i++) {
            const std::uint8_t *p = cpu.m_memory.hostPtr(addr + static_cast<std::uint32_t>(i));
            if (!p || *p == 0)
                break;
            out.push_back(static_cast<char>(*p));
        }
        return out;
    }

    // ---- OSEvent ------------------------------------------------------------
    // Host-side state keyed by the guest OSEvent*. Mode: 0 = manual reset, 1 = auto reset.

    struct EventState {
            bool signaled{false};
            std::uint32_t mode{0};
    };
    std::unordered_map<std::uint32_t, EventState> g_events;

    void hle_OSInitEvent(Core::Interpreter &cpu)
    {
        g_events[cpu.m_gpr[3]] = EventState{cpu.m_gpr[4] != 0, cpu.m_gpr[5]};
        cpu.m_gpr[3] = 0;
    }
    void hle_OSInitEventEx(Core::Interpreter &cpu) { hle_OSInitEvent(cpu); }

    void hle_OSSignalEvent(Core::Interpreter &cpu)
    {
        auto &ev = g_events[cpu.m_gpr[3]];
        ev.signaled = true;
        // Auto-reset: a woken waiter consumes the signal (it resumes past its wait). With no
        // waiter the event must stay signaled so the next OSWaitEvent sees it.
        const std::size_t woken = ev.mode == 1 ? cpu.m_scheduler.wakeOne(cpu.m_gpr[3]) : cpu.m_scheduler.wakeAll(cpu.m_gpr[3]);
        if (ev.mode == 1 && woken > 0)
            ev.signaled = false;
        cpu.m_gpr[3] = 0;
        cpu.m_scheduler.rescheduleAfterHle(cpu);
    }
    void hle_OSSignalEventAll(Core::Interpreter &cpu)
    {
        auto &ev = g_events[cpu.m_gpr[3]];
        ev.signaled = true;
        if (cpu.m_scheduler.wakeAll(cpu.m_gpr[3]) && ev.mode == 1)
            ev.signaled = false;
        cpu.m_gpr[3] = 0;
        cpu.m_scheduler.rescheduleAfterHle(cpu);
    }
    void hle_OSResetEvent(Core::Interpreter &cpu)
    {
        g_events[cpu.m_gpr[3]].signaled = false;
        cpu.m_gpr[3] = 0;
    }
    void hle_OSWaitEvent(Core::Interpreter &cpu)
    {
        auto &ev = g_events[cpu.m_gpr[3]];
        if (ev.signaled) {
            if (ev.mode == 1)
                ev.signaled = false;
            cpu.m_gpr[3] = 0;
            return;
        }
        // Not signaled: block if another thread can run; otherwise pretend the wait was satisfied
        // (blocking the only runnable thread would deadlock the single-host-thread scheduler).
        // After a switch the live registers belong to the next thread -- don't touch them.
        if (!cpu.m_scheduler.blockOn(cpu, cpu.m_gpr[3]))
            cpu.m_gpr[3] = 0;
    }
    void hle_OSWaitEventWithTimeout(Core::Interpreter &cpu)
    {
        auto &ev = g_events[cpu.m_gpr[3]];
        if (ev.signaled) {
            if (ev.mode == 1)
                ev.signaled = false;
            cpu.m_gpr[3] = 1; // TRUE: signaled before timeout
            return;
        }
        // Cafe passes the 64-bit timeout in nanoseconds in the aligned r5:r6 pair.
        // Convert without overflowing the multiplication for large waits.
        const std::uint64_t ns = (std::uint64_t(cpu.m_gpr[5]) << 32) | cpu.m_gpr[6];
        constexpr std::uint64_t frequency = 248625000u / 4u;
        const auto ticks = (ns / 1000000000u) * frequency + (ns % 1000000000u) * frequency / 1000000000u;
        cpu.m_scheduler.blockWithTimeout(cpu, cpu.m_gpr[3], ticks);
    }

    // ---- OSCond / rendezvous / thread queues ---------------------------------

    void hle_OSInitCond(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
    void hle_OSWaitCond(Core::Interpreter &cpu)
    {
        // The guest holds the mutex (our mutexes are no-ops); spurious wakeups are legal for
        // condition variables, so waking without the predicate is safe -- callers re-check in a loop.
        // After a switch the live registers belong to the next thread -- don't touch them.
        if (!cpu.m_scheduler.blockOn(cpu, cpu.m_gpr[3]))
            cpu.m_gpr[3] = 0;
    }
    void hle_OSSignalCond(Core::Interpreter &cpu)
    {
        cpu.m_scheduler.wakeAll(cpu.m_gpr[3]);
        cpu.m_gpr[3] = 0;
        cpu.m_scheduler.rescheduleAfterHle(cpu);
    }
    void hle_OSInitRendezvous(Core::Interpreter &cpu)
    {
        const auto rendezvous = cpu.m_gpr[3];
        for (std::uint32_t core = 0; core < 3; ++core)
            cpu.m_memory.write<std::uint32_t>(rendezvous + core * 4, 0);
    }

    void hle_OSWaitRendezvous(Core::Interpreter &cpu)
    {
        const auto rendezvous = cpu.m_gpr[3];
        const auto mask = cpu.m_gpr[4] & 7u;
        const auto arrival = rendezvous + cpu.m_scheduler.currentCoreId() * 4;
        if (!cpu.m_memory.read<std::uint32_t>(arrival)) {
            cpu.m_memory.write<std::uint32_t>(arrival, 1);
            cpu.m_scheduler.wakeAll(rendezvous);
        }
        for (std::uint32_t core = 0; core < 3; ++core) {
            if ((mask & (1u << core)) && !cpu.m_memory.read<std::uint32_t>(rendezvous + core * 4)) {
                // Retry the predicate after another core arrives, preserving the call arguments.
                if (!cpu.m_scheduler.blockOn(cpu, rendezvous, true)) {
                    cpu.m_nextPc = cpu.m_pc;
                    cpu.m_hle_redirected = true;
                }
                return;
            }
        }
        cpu.m_gpr[3] = 1;
    }
    void hle_OSInitThreadQueue(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
    void hle_OSInitThreadQueueEx(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

    // ---- OSAlarm --------------------------------------------------------------
    // Store-only: alarms are registered but never fire (no timer interrupts in the cooperative
    // scheduler yet). Cancel/user-data round-trips work so title-side bookkeeping stays coherent.

    struct AlarmState {
            std::uint32_t callback{0};
            std::uint32_t userData{0};
            std::uint64_t period{0};
    };
    std::unordered_map<std::uint32_t, AlarmState> g_alarms;

    void hle_OSCreateAlarm(Core::Interpreter &cpu)
    {
        g_alarms[cpu.m_gpr[3]] = AlarmState{};
        cpu.m_gpr[3] = 0;
    }
    void hle_OSCreateAlarmEx(Core::Interpreter &cpu) { hle_OSCreateAlarm(cpu); }
    void hle_OSSetAlarm(Core::Interpreter &cpu)
    {
        g_alarms[cpu.m_gpr[3]].callback = cpu.m_gpr[7]; // (alarm, time hi, time lo, callback)
        cpu.m_gpr[3] = 1;
    }
    void hle_OSSetPeriodicAlarm(Core::Interpreter &cpu)
    {
        auto &a = g_alarms[cpu.m_gpr[3]];
        a.period = (static_cast<std::uint64_t>(cpu.m_gpr[6]) << 32) | cpu.m_gpr[7];
        a.callback = cpu.m_gpr[8];
        cpu.m_gpr[3] = 1;
    }
    void hle_OSCancelAlarm(Core::Interpreter &cpu)
    {
        g_alarms.erase(cpu.m_gpr[3]);
        cpu.m_gpr[3] = 1;
    }
    void hle_OSCancelAlarms(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
    void hle_OSSetAlarmUserData(Core::Interpreter &cpu)
    {
        g_alarms[cpu.m_gpr[3]].userData = cpu.m_gpr[4];
        cpu.m_gpr[3] = 0;
    }
    void hle_OSGetAlarmUserData(Core::Interpreter &cpu) { cpu.m_gpr[3] = g_alarms[cpu.m_gpr[3]].userData; }

    // ---- GHS (Green Hills) C runtime hooks -------------------------------------

    void hle_ghs_noop(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
    void hle_memmove(Core::Interpreter &cpu)
    {
        const std::uint32_t dst = cpu.m_gpr[3];
        std::uint8_t *d = cpu.m_memory.hostPtr(dst);
        const std::uint8_t *s = cpu.m_memory.hostPtr(cpu.m_gpr[4]);
        if (d && s && cpu.m_gpr[5])
            std::memmove(d, s, cpu.m_gpr[5]);
        cpu.m_gpr[3] = dst;
    }

    // ---- Cache / memory barriers -------------------------------------------------

    void hle_DCZeroRange(Core::Interpreter &cpu)
    {
        // Unlike the flush/invalidate family this one has a visible effect: zero the range.
        if (std::uint8_t *p = cpu.m_memory.hostPtr(cpu.m_gpr[3]); p && cpu.m_gpr[4])
            std::memset(p, 0, cpu.m_gpr[4]);
        cpu.m_gpr[3] = 0;
    }
    void hle_cache_noop(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
    void hle_OSIsAddressRangeDCValid(Core::Interpreter &cpu) { cpu.m_gpr[3] = 1; }

    // ---- Panic / report visibility -------------------------------------------------
    // These are the game telling us exactly what went wrong; print everything.

    void hle_OSPanic(Core::Interpreter &cpu)
    {
        const std::string file = readGuestString(cpu, cpu.m_gpr[3], 256);
        const std::string msg = readGuestString(cpu, cpu.m_gpr[5], 1024);
        std::fprintf(stderr, "[OSPanic] %s:%u %s\n", file.c_str(), cpu.m_gpr[4], msg.c_str());
        cpu.stop();
    }
    void hle_OSVReport(Core::Interpreter &cpu)
    {
        // Format string printed raw (no varargs decoding); usually descriptive enough.
        std::fprintf(stderr, "[OSVReport] %s\n", readGuestString(cpu, cpu.m_gpr[3], 1024).c_str());
        cpu.m_gpr[3] = 0;
    }
    void hle_OSConsoleWrite(Core::Interpreter &cpu)
    {
        const std::uint32_t size = std::min<std::uint32_t>(cpu.m_gpr[4], 4096);
        if (const std::uint8_t *p = cpu.m_memory.hostPtr(cpu.m_gpr[3]); p && size)
            std::fprintf(stderr, "[OSConsole] %.*s\n", static_cast<int>(size), reinterpret_cast<const char *>(p));
        cpu.m_gpr[3] = 0;
    }

    // ---- UC (system config) / MCP ------------------------------------------------

    void hle_UCOpen(Core::Interpreter &cpu) { cpu.m_gpr[3] = 1; } // any positive handle
    void hle_UCClose(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

    // UCReadSysConfig(handle, count, UCSysConfig settings[]). Entry layout (0x54 bytes):
    // name[64] @0x00, access u32 @0x40, dataType @0x44, error @0x48, dataSize @0x4C, dataPtr @0x50.
    void hle_UCReadSysConfig(Core::Interpreter &cpu)
    {
        const std::uint32_t count = cpu.m_gpr[4];
        const std::uint32_t settings = cpu.m_gpr[5];
        constexpr std::uint32_t ENTRY = 0x54;
        for (std::uint32_t i = 0; i < count && i < 64; i++) {
            const std::uint32_t entry = settings + i * ENTRY;
            const std::string name = readGuestString(cpu, entry, 64);
            const std::uint32_t dataSize = cpu.m_memory.read<std::uint32_t>(entry + 0x4C);
            const std::uint32_t dataPtr = cpu.m_memory.read<std::uint32_t>(entry + 0x50);
            cpu.m_memory.write<std::uint32_t>(entry + 0x48, 0); // error = OK
            if (!dataPtr || !dataSize)
                continue;
            for (std::uint32_t off = 0; off < dataSize; off++)
                cpu.m_memory.write<std::uint8_t>(dataPtr + off, 0);
            // English instead of the zero-default (Japanese).
            if (dataSize == 4 && name.find("language") != std::string::npos)
                cpu.m_memory.write<std::uint32_t>(dataPtr, 1);
            Utils::Log::debug("[UC] ReadSysConfig '{}' size={}", name, dataSize);
        }
        cpu.m_gpr[3] = 0;
    }

    void hle_MCP_Open(Core::Interpreter &cpu) { cpu.m_gpr[3] = 1; }
    void hle_MCP_Close(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
    void hle_MCP_GetSysProdSettings(Core::Interpreter &cpu)
    {
        const std::uint32_t out = cpu.m_gpr[4];
        for (std::uint32_t off = 0; off < 0x46; off++) // MCPSysProdSettings is 0x46 bytes
            cpu.m_memory.write<std::uint8_t>(out + off, 0);
        cpu.m_gpr[3] = 0;
    }

    // ---- OSDynLoad exports ---------------------------------------------------------
    // FindExport hands back a synthetic import sentinel so calls through the returned pointer
    // dispatch through the same BCTR sentinel path as static imports.

    std::unordered_map<std::string, std::uint32_t> g_dynExportSentinels;
    std::uint32_t g_dynAllocatorAlloc = 0, g_dynAllocatorFree = 0;

    void hle_OSDynLoad_FindExport(Core::Interpreter &cpu)
    {
        const std::string name = readGuestString(cpu, cpu.m_gpr[5], 256);
        const std::uint32_t outAddr = cpu.m_gpr[6];
        if (name.empty() || !outAddr) {
            cpu.m_gpr[3] = 0xFFFCFFE9; // OS_DYNLOAD invalid arg-ish
            return;
        }
        cpu.m_memory.write<std::uint32_t>(outAddr, Core::Hle::exportFunction(cpu, name));
        cpu.m_gpr[3] = 0;
    }
    void hle_OSDynLoad_Release(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
    void hle_OSDynLoad_SetAllocator(Core::Interpreter &cpu)
    {
        g_dynAllocatorAlloc = cpu.m_gpr[3];
        g_dynAllocatorFree = cpu.m_gpr[4];
        cpu.m_gpr[3] = 0;
    }
    void hle_OSDynLoad_GetAllocator(Core::Interpreter &cpu)
    {
        if (cpu.m_gpr[3])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[3], g_dynAllocatorAlloc);
        if (cpu.m_gpr[4])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[4], g_dynAllocatorFree);
        cpu.m_gpr[3] = 0;
    }

    // ---- Time / misc ------------------------------------------------------------------

    // OSTicksToCalendarTime(OSTime r3:r4, OSCalendarTime *r5) -- ten i32 fields:
    // sec, min, hour, mday, mon(0-based), year, wday, yday, msec, usec.
    void hle_OSTicksToCalendarTime(Core::Interpreter &cpu)
    {
        const std::uint32_t out = cpu.m_gpr[5];
        if (!out)
            return;
        const std::uint32_t fields[10] = {0, 0, 12, 5, 6, 2026, 0, 185, 0, 0};
        for (int i = 0; i < 10; i++)
            cpu.m_memory.write<std::uint32_t>(out + static_cast<std::uint32_t>(i) * 4, fields[i]);
    }
    void hle_OSTryLockMutex(Core::Interpreter &cpu) { cpu.m_gpr[3] = 1; }
    void hle_ENVGetEnvironmentVariable(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; } // not found
    void hle_OSGetSharedData(Core::Interpreter &cpu)
    {
        // (type, flags, u32 *outPtr, u32 *outSize). Titles use this for the system font (type
        // 0-3 = chinese/korean/standard/taiwanese); nw::font/lyt init fails without one and the
        // whole UI layer goes missing. WEMU_SHARED_FONT=<path to .bffnt> serves a real font file
        // (loaded once into the internal HLE guest pool) for every requested type.
        static std::uint32_t fontPtr = 0, fontSize = 0;
        static bool loaded = false;
        if (!loaded) {
            loaded = true;
            if (const char *path = std::getenv("WEMU_SHARED_FONT")) {
                if (std::FILE *f = std::fopen(path, "rb")) {
                    std::fseek(f, 0, SEEK_END);
                    const long sz = std::ftell(f);
                    std::fseek(f, 0, SEEK_SET);
                    if (sz > 0 && sz < 64 * 1024 * 1024) {
                        fontPtr = cpu.m_memory.heapAllocate(static_cast<std::uint32_t>(sz), 0x100);
                        if (std::uint8_t *host = cpu.m_memory.hostPtr(fontPtr); host && std::fread(host, 1, static_cast<std::size_t>(sz), f) == static_cast<std::size_t>(sz))
                            fontSize = static_cast<std::uint32_t>(sz);
                        else
                            fontPtr = 0;
                    }
                    std::fclose(f);
                }
                Utils::Log::error("[HLE] OSGetSharedData: font '{}' -> guest 0x{:08X} ({} bytes)", path, fontPtr, fontSize);
            } else {
                Utils::Log::error("[HLE] OSGetSharedData type={} -> none (set WEMU_SHARED_FONT=<.bffnt> to provide one)", cpu.m_gpr[3]);
            }
        }
        if (cpu.m_gpr[5])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[5], fontPtr);
        if (cpu.m_gpr[6])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[6], fontSize);
        cpu.m_gpr[3] = fontSize ? 1 : 0;
    }
    void hle_LCHardwareIsAvailable(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }
    void hle_OSGetOverlayArenaRange(Core::Interpreter &cpu)
    {
        if (cpu.m_gpr[3])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[3], 0);
        if (cpu.m_gpr[4])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[4], 0);
        cpu.m_gpr[3] = 0;
    }

    // ---- zlib125.rpl bridge -----------------------------------------------------------
    // Guest z_stream (32-bit BE): next_in @0x00, avail_in @0x04, total_in @0x08, next_out @0x0C,
    // avail_out @0x10, total_out @0x14, msg @0x18, state @0x1C, zalloc @0x20, zfree @0x24,
    // opaque @0x28, data_type @0x2C, adler @0x30. One host z_stream per guest struct.

    struct GuestZStream {
            z_stream host{};
            bool inflateMode{false};
    };
    std::unordered_map<std::uint32_t, GuestZStream> g_zstreams;

    void zSyncIn(Core::Interpreter &cpu, std::uint32_t g, GuestZStream &zs)
    {
        zs.host.next_in = cpu.m_memory.hostPtr(cpu.m_memory.read<std::uint32_t>(g + 0x00));
        zs.host.avail_in = cpu.m_memory.read<std::uint32_t>(g + 0x04);
        zs.host.next_out = cpu.m_memory.hostPtr(cpu.m_memory.read<std::uint32_t>(g + 0x0C));
        zs.host.avail_out = cpu.m_memory.read<std::uint32_t>(g + 0x10);
    }
    void zSyncOut(Core::Interpreter &cpu, std::uint32_t g, const GuestZStream &zs, std::uint32_t inBefore, std::uint32_t outBefore)
    {
        const std::uint32_t consumedIn = inBefore - zs.host.avail_in;
        const std::uint32_t producedOut = outBefore - zs.host.avail_out;
        cpu.m_memory.write<std::uint32_t>(g + 0x00, cpu.m_memory.read<std::uint32_t>(g + 0x00) + consumedIn);
        cpu.m_memory.write<std::uint32_t>(g + 0x04, zs.host.avail_in);
        cpu.m_memory.write<std::uint32_t>(g + 0x08, static_cast<std::uint32_t>(zs.host.total_in));
        cpu.m_memory.write<std::uint32_t>(g + 0x0C, cpu.m_memory.read<std::uint32_t>(g + 0x0C) + producedOut);
        cpu.m_memory.write<std::uint32_t>(g + 0x10, zs.host.avail_out);
        cpu.m_memory.write<std::uint32_t>(g + 0x14, static_cast<std::uint32_t>(zs.host.total_out));
        cpu.m_memory.write<std::uint32_t>(g + 0x30, static_cast<std::uint32_t>(zs.host.adler));
    }

    void hle_inflateInit2_(Core::Interpreter &cpu)
    {
        auto &zs = g_zstreams[cpu.m_gpr[3]];
        if (zs.inflateMode)
            inflateEnd(&zs.host);
        zs.host = z_stream{};
        zs.inflateMode = true;
        cpu.m_gpr[3] = static_cast<std::uint32_t>(inflateInit2(&zs.host, static_cast<int>(cpu.m_gpr[4])));
    }
    void hle_inflate(Core::Interpreter &cpu)
    {
        const std::uint32_t g = cpu.m_gpr[3];
        auto it = g_zstreams.find(g);
        if (it == g_zstreams.end()) {
            cpu.m_gpr[3] = static_cast<std::uint32_t>(Z_STREAM_ERROR);
            return;
        }
        zSyncIn(cpu, g, it->second);
        const std::uint32_t inBefore = it->second.host.avail_in, outBefore = it->second.host.avail_out;
        const int rc = inflate(&it->second.host, static_cast<int>(cpu.m_gpr[4]));
        zSyncOut(cpu, g, it->second, inBefore, outBefore);
        cpu.m_gpr[3] = static_cast<std::uint32_t>(rc);
    }
    void hle_inflateEnd(Core::Interpreter &cpu)
    {
        if (auto it = g_zstreams.find(cpu.m_gpr[3]); it != g_zstreams.end()) {
            inflateEnd(&it->second.host);
            g_zstreams.erase(it);
        }
        cpu.m_gpr[3] = Z_OK;
    }
    void hle_deflateInit2_(Core::Interpreter &cpu)
    {
        auto &zs = g_zstreams[cpu.m_gpr[3]];
        zs.host = z_stream{};
        zs.inflateMode = false;
        cpu.m_gpr[3] = static_cast<std::uint32_t>(deflateInit2(&zs.host, static_cast<int>(cpu.m_gpr[4]), static_cast<int>(cpu.m_gpr[5]),
                                                               static_cast<int>(cpu.m_gpr[6]), static_cast<int>(cpu.m_gpr[7]),
                                                               static_cast<int>(cpu.m_gpr[8])));
    }
    void hle_deflate(Core::Interpreter &cpu)
    {
        const std::uint32_t g = cpu.m_gpr[3];
        auto it = g_zstreams.find(g);
        if (it == g_zstreams.end()) {
            cpu.m_gpr[3] = static_cast<std::uint32_t>(Z_STREAM_ERROR);
            return;
        }
        zSyncIn(cpu, g, it->second);
        const std::uint32_t inBefore = it->second.host.avail_in, outBefore = it->second.host.avail_out;
        const int rc = deflate(&it->second.host, static_cast<int>(cpu.m_gpr[4]));
        zSyncOut(cpu, g, it->second, inBefore, outBefore);
        cpu.m_gpr[3] = static_cast<std::uint32_t>(rc);
    }
    void hle_deflateBound(Core::Interpreter &cpu)
    {
        cpu.m_gpr[3] = static_cast<std::uint32_t>(deflateBound(nullptr, cpu.m_gpr[4]));
    }
    void hle_deflateEnd(Core::Interpreter &cpu)
    {
        if (auto it = g_zstreams.find(cpu.m_gpr[3]); it != g_zstreams.end()) {
            deflateEnd(&it->second.host);
            g_zstreams.erase(it);
        }
        cpu.m_gpr[3] = Z_OK;
    }

    // ---- mvplayer.rpl (menu background movies) ------------------------------------------------
    // MK8 imports only Create/Destroy/Shutdown and drives the rest through the object:
    // [obj+0] -> impl, [impl+0x184] -> method table, [table+slot] -> function, invoked via bctr.
    // No video is decoded: every table entry points at a real `li r3,0; blr` in .text so each
    // call returns 0 immediately. A bare `blr` is NOT enough: it leaves r3 = this, and the
    // title's wait loops (e.g. 0x027404E0 "spin until progress sum >= vcall(+0x24) total")
    // then compare against the object pointer and never terminate.

    std::uint32_t g_mvPlayer = 0;

    // Each vtable slot points at a distinct sentinel PC (kMvSlotBase + slot*4), hooked per-method so
    // the movie can report a real "playing -> finished" lifecycle instead of one constant return.
    constexpr std::uint32_t kMvSlotBase = 0x0FFD0000;
    constexpr std::uint32_t kMvSlots = 128;

    void hle_MVPlayerCreate(Core::Interpreter &cpu)
    {
        if (!g_mvPlayer) {
            constexpr std::uint32_t kObjSize = 0x1000;
            constexpr std::uint32_t kImplSize = 0x400;
            constexpr std::uint32_t kTableSize = 0x200;
            constexpr std::uint32_t kRetZeroAddr = 0x02009868; // `li r3,0; blr` in MK8 v4.1 .text
            constexpr std::uint32_t kRetOneAddr = 0x02005FB4; // `li r3,1; blr` in MK8 v4.1 .text
            (void) kRetZeroAddr;
            (void) kRetOneAddr;
            const std::uint32_t table = cpu.m_memory.heapAllocate(kTableSize, 8);
            // Point each slot at its per-method sentinel (hooked in InstallMVPlayerHooks) so each
            // MVPlayer method gets distinct, stateful behaviour instead of one constant return.
            for (std::uint32_t off = 0; off < kTableSize; off += 4)
                cpu.m_memory.write<std::uint32_t>(table + off, kMvSlotBase + off);
            const std::uint32_t impl = cpu.m_memory.heapAllocate(kImplSize, 8);
            for (std::uint32_t off = 0; off < kImplSize; off += 4)
                cpu.m_memory.write<std::uint32_t>(impl + off, off == 0x184 ? table : 0);
            g_mvPlayer = cpu.m_memory.heapAllocate(kObjSize, 8);
            for (std::uint32_t off = 0; off < kObjSize; off += 4)
                cpu.m_memory.write<std::uint32_t>(g_mvPlayer + off, 0);
            cpu.m_memory.write<std::uint32_t>(g_mvPlayer, impl);
            // Some dispatch thunks skip the impl hop and read the table straight off the object.
            cpu.m_memory.write<std::uint32_t>(g_mvPlayer + 0x184, table);
            Utils::Log::error("[HLE] MVPlayer::Create -> stub object 0x{:08X}", g_mvPlayer);
        }
        cpu.m_gpr[3] = g_mvPlayer;
    }

    void hle_MVPlayerDestroy(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; } // keep the singleton
    void hle_MVViewDraw(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; } // video plane is not decoded/rendered yet
    void hle_MVPlayerShutdown(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

    // ---- MVPlayer per-method intercepts -------------------------------------------------------
    // Per-slot call counter (WEMU_MVP_TRACE=1 logs the call pattern to identify the methods).
    std::array<std::uint32_t, kMvSlots> g_mvSlotCalls{};

    void mvPlayerMethod(Core::Interpreter &cpu, std::uint32_t slot)
    {
        static const bool trace = []() {
            const char *e = std::getenv("WEMU_MVP_TRACE");
            return e && e[0] == '1';
        }();
        const std::uint32_t n = ++g_mvSlotCalls[slot];
        if (trace && n <= 3)
            Utils::Log::error("[MVP] slot#{:>3} call#{} r3=0x{:08X} r4=0x{:08X} r5=0x{:08X} r6=0x{:08X}", slot, n, cpu.m_gpr[3],
                              cpu.m_gpr[4], cpu.m_gpr[5], cpu.m_gpr[6]);
        // WEMU_MVPLAYER_RET1=1: diagnostic only. Some callers may treat these method returns as
        // boolean success/ready state; keep the default as the original conservative return-0 stub
        // while allowing one-run validation without patching the title binary.
        static const bool retOne = []() {
            const char *e = std::getenv("WEMU_MVPLAYER_RET1");
            return e && e[0] == '1';
        }();
        cpu.m_gpr[3] = retOne ? 1 : 0;
    }

    // ---- Title-sequence frame-pool fix -------------------------------------------------------
    //
    // MK8's global "sead"-style frame allocator (getter 0x026A866C, singleton object 0x101DD944)
    // hands out fixed-size objects from a bump pool embedded at object+0x4A8. That pool holds no
    // memory of its own: it is *seeded* on demand by draining a message queue at object+0x248
    // (coreinit OSReceiveMessage) whose producer — a system/IPC path we do not emulate — never runs.
    // So the pool stays empty, the first allocation (the title-sequence worker's 332-byte object)
    // returns NULL, and the worker faults writing through it (Unmapped write @ 0x00000004 at PC
    // 0x026A6DB8). `main` then waits forever on the sync object that worker was meant to signal, so
    // the title screen never advances to the interactive menu.
    //
    // We stand in for the missing producer. The refill routine at 0x026A9904 is what the allocator
    // calls when the pool cannot satisfy a request; we replace it and hand the pool a real backing
    // buffer from the HLE heap, laid out exactly as the native pool-init (0x026A9630) would:
    //   pool+0x00 = base pointer   pool+0x04 = capacity in words
    //   pool+0x08 = cursor (0)     pool+0x0C = high-water (0)   pool+0x10 = frozen flag (0)
    // The native bump allocator (0x026A9650) then satisfies the retry and every subsequent request
    // out of that buffer; when a buffer fills, the allocator calls refill again and we append a
    // fresh one. Old buffers are never reclaimed (the HLE heap is bump-only), so pointers into them
    // stay valid — which matches the native design, where each drained message adds a new block.
    constexpr std::uint32_t kSeqRefillPc = 0x026A9904; // the pool's refill routine
    constexpr std::uint32_t kSeqPoolOff = 0x4A8; // frame pool offset within the allocator object
    constexpr std::uint32_t kSeqBackingBytes = 0x100000; // 1 MB of backing per refill slot
    constexpr std::uint32_t kSeqMaxBackings = 8; // bounded HLE stand-in for the native recycle queue
    // Refill prologue: mflr r0 / stwu r1,-0x10(r1) / stw r31,0xc(r1) / mr r31,r3 / addi r3,r31,0x248.
    // The trailing addi (…,0x248) is the message-queue offset, which makes this a strong signature.
    constexpr std::array<std::uint32_t, 5> kSeqRefillSig{0x7C0802A6, 0x9421FFF0, 0x93E1000C, 0x7C7F1B78, 0x387F0248};

    struct SeqPoolState {
            std::array<std::uint32_t, kSeqMaxBackings> backing{};
            std::uint32_t count{0};
            std::uint32_t current{0};
    };
    std::unordered_map<std::uint32_t, SeqPoolState> g_seqPools;

    std::uint32_t seqPoolBacking(Core::Interpreter &cpu, const std::uint32_t allocator, const bool allocationRetry)
    {
        auto &state = g_seqPools[allocator];
        if (allocationRetry) {
            if (state.count == 0 || state.current + 1 >= state.count) {
                if (state.count < kSeqMaxBackings)
                    state.backing[state.count++] = cpu.m_memory.heapAllocate(kSeqBackingBytes, 0x20);
                else
                    state.current = 0; // bounded fallback: rotate instead of leaking past guest RAM
            } else {
                state.current++;
            }
        } else {
            state.current = 0; // native refill/recycle points start a fresh frame from the first block
            if (state.count == 0)
                state.backing[state.count++] = cpu.m_memory.heapAllocate(kSeqBackingBytes, 0x20);
        }
        if (state.current >= state.count)
            state.current = 0;
        return state.backing[state.current];
    }

} // namespace

std::uint32_t Core::Hle::exportFunction(Core::Interpreter &cpu, const std::string &name)
{
    const auto [it, inserted] = g_dynExportSentinels.try_emplace(
        name, 0xC0F00000u + static_cast<std::uint32_t>(g_dynExportSentinels.size()) * 4);
    // unordered_map element references survive rehash. Attach to each interpreter,
    // including when a name was already exported by a previous instance.
    cpu.m_importBySentinel[it->second] = &it->first;
    return it->second;
}

void InstallMVPlayerHooks(Core::Interpreter &interp)
{
    for (std::uint32_t slot = 0; slot < kMvSlots; slot++)
        interp.m_hooks[kMvSlotBase + slot * 4] = [slot](Core::Interpreter &cpu) { mvPlayerMethod(cpu, slot); };
}

namespace {
    // Scene-config method 0x0203AB18 (prologue signature). It iterates a fixed 14 container entries
    // (li r26,0xE) via the indexer at 0x026B8318; the container's backing (obj+0x20) is never
    // allocated because its resource descriptor yields 0 entries in our offline state, so element 0
    // dereferences NULL at 0x0203AB70. Full trail in project memory.
    constexpr std::uint32_t kSceneCfgPc = 0x0203AB18;
    constexpr std::array<std::uint32_t, 6> kSceneCfgSig{0x9421FFB8, 0x7C0802A6, 0xBF21002C, 0x7C7E1B78, 0x3880FFFF, 0x3861001C};
} // namespace

void InstallSceneConfigWorkaround(Core::Interpreter &interp)
{
    if (const char *e = std::getenv("WEMU_NO_SCENECFG_SKIP"); e && e[0] == '1')
        return;
    for (std::uint32_t i = 0; i < kSceneCfgSig.size(); i++) {
        std::uint32_t word = 0;
        try {
            word = interp.m_memory.read<std::uint32_t>(kSceneCfgPc + i * 4);
        } catch (...) {
            return;
        }
        if (word != kSceneCfgSig[i])
            return; // not MK8's scene-config method — leave this address alone
    }
    // Return immediately (r3 stays = this). The dispatcher 0x0203ABF8 has already stored the scene
    // float at this+0x218, so only the 14-entry iteration over the (empty) container is skipped.
    interp.m_hooks[kSceneCfgPc] = [](Core::Interpreter & /*cpu*/) {};
    Utils::Log::info("[HLE] scene-config workaround armed at 0x{:08X}", kSceneCfgPc);
}

void InstallSceneUpdateForce(Core::Interpreter &interp)
{
    // EXPERIMENT (WEMU_FORCE_SCENE=1): MK8's scene-transition "is-busy" predicate at 0x024CE528
    // returns true while a scene transition is stuck at state 3, which makes every scene/task skip
    // its per-frame update (0x0205889x / 0x02044AD4 gate on it) — the whole game freezes on the
    // title. Force it to report "not busy" so updates keep running, to see whether the transition
    // then completes (or what breaks next).
    if (const char *e = std::getenv("WEMU_FORCE_SCENE"); !e || e[0] != '1')
        return;
    interp.m_hooks[0x024CE528] = [](Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; };
    Utils::Log::error("[HLE] EXPERIMENT: scene is-busy forced to 0 at 0x024CE528");
}

void InstallSubsysReadyForce(Core::Interpreter &interp)
{
    // EXPERIMENT (WEMU_FORCE_SUBSYS=1): MK8's scene-object subsystem sets a global "ready" byte at
    // 0x1018C1D0 (via init 0x026BCD2C) — but in our boot that init runs AFTER the scene builds its
    // 14-entry object container, so the container-fill (0x026B7D18 → getter 0x026BD544) reads the
    // flag as 0, bails, and (being one-shot) leaves the container empty forever → the scene never
    // signals ready → the title→menu transition freezes at state 3. Force the getter to report ready
    // so the fill succeeds on its only attempt, to test whether the container then builds.
    if (const char *e = std::getenv("WEMU_FORCE_SUBSYS"); !e || e[0] != '1')
        return;
    // Getter 0x026BD544 is a 3-instr leaf: lis/lbz/blr. Replace with "return 1".
    interp.m_hooks[0x026BD544] = [](Core::Interpreter &cpu) { cpu.m_gpr[3] = 1; };
    Utils::Log::error("[HLE] EXPERIMENT: scene-object subsystem forced ready at 0x026BD544");
}

void InstallTitleSeqPoolFix(Core::Interpreter &interp)
{
    // Self-guard: only hook if the exact refill routine is present at this address, so the fix is
    // inert for any other title whose code differs there.
    for (std::uint32_t i = 0; i < kSeqRefillSig.size(); i++) {
        std::uint32_t word = 0;
        try {
            word = interp.m_memory.read<std::uint32_t>(kSeqRefillPc + i * 4);
        } catch (...) {
            return;
        }
        if (word != kSeqRefillSig[i])
            return;
    }
    interp.m_hooks[kSeqRefillPc] = [](Core::Interpreter &cpu) {
        const std::uint32_t allocator = cpu.m_gpr[3]; // r3 = the allocator object (context)
        const std::uint32_t pool = allocator + kSeqPoolOff;
        const std::uint32_t lr = cpu.m_lr + Core::Memory::MemoryMap::ApplicationCode;
        const bool allocationRetry = lr == 0x026A9998; // 0x026A9968 retry path after bump allocation failed
        const std::uint32_t buf = seqPoolBacking(cpu, allocator, allocationRetry);
        cpu.m_memory.write<std::uint32_t>(pool + 0x00, buf); // base pointer
        cpu.m_memory.write<std::uint32_t>(pool + 0x04, kSeqBackingBytes >> 2); // capacity (words)
        cpu.m_memory.write<std::uint32_t>(pool + 0x08, 0); // cursor
        cpu.m_memory.write<std::uint32_t>(pool + 0x0C, 0); // high-water
        cpu.m_memory.write<std::uint8_t>(pool + 0x10, 0); // unfrozen
        static const bool trace = []() {
            const char *e = std::getenv("WEMU_SEQ_POOL_TRACE");
            return e && e[0] == '1';
        }();
        if (trace) {
            const auto &state = g_seqPools[allocator];
            Utils::Log::error("[SEQPOOL] refill allocator=0x{:08X} lr=0x{:08X} retry={} buf=0x{:08X} slot={}/{}",
                              allocator, lr, allocationRetry ? 1 : 0, buf, state.current, state.count);
        }
    };
    Utils::Log::info("[HLE] title-sequence frame-pool fix armed at 0x{:08X}", kSeqRefillPc);
}

void RegisterCoreinitExtraFunctions()
{
    auto &sh = Core::syscallHandler;

    // mvplayer.rpl
    sh.registerSyscall("Create__8MVPlayerSFv", hle_MVPlayerCreate);
    sh.registerSyscall("Destroy__8MVPlayerSFP8MVPlayer", hle_MVPlayerDestroy);
    sh.registerSyscall("Draw__6MVViewFv", hle_MVViewDraw);
    sh.registerSyscall("Shutdown__8MVPlayerSFv", hle_MVPlayerShutdown);

    // OSEvent
    sh.registerSyscall("OSInitEvent", hle_OSInitEvent);
    sh.registerSyscall("OSInitEventEx", hle_OSInitEventEx);
    sh.registerSyscall("OSSignalEvent", hle_OSSignalEvent);
    sh.registerSyscall("OSSignalEventAll", hle_OSSignalEventAll);
    sh.registerSyscall("OSResetEvent", hle_OSResetEvent);
    sh.registerSyscall("OSWaitEvent", hle_OSWaitEvent);
    sh.registerSyscall("OSWaitEventWithTimeout", hle_OSWaitEventWithTimeout);

    // OSCond / rendezvous / thread queues
    sh.registerSyscall("OSInitCond", hle_OSInitCond);
    sh.registerSyscall("OSInitCondEx", hle_OSInitCond);
    sh.registerSyscall("OSWaitCond", hle_OSWaitCond);
    sh.registerSyscall("OSSignalCond", hle_OSSignalCond);
    sh.registerSyscall("OSInitRendezvous", hle_OSInitRendezvous);
    sh.registerSyscall("OSWaitRendezvous", hle_OSWaitRendezvous);
    sh.registerSyscall("OSInitThreadQueue", hle_OSInitThreadQueue);
    sh.registerSyscall("OSInitThreadQueueEx", hle_OSInitThreadQueueEx);

    // OSAlarm
    sh.registerSyscall("OSCreateAlarm", hle_OSCreateAlarm);
    sh.registerSyscall("OSCreateAlarmEx", hle_OSCreateAlarmEx);
    sh.registerSyscall("OSSetAlarm", hle_OSSetAlarm);
    sh.registerSyscall("OSSetPeriodicAlarm", hle_OSSetPeriodicAlarm);
    sh.registerSyscall("OSCancelAlarm", hle_OSCancelAlarm);
    sh.registerSyscall("OSCancelAlarms", hle_OSCancelAlarms);
    sh.registerSyscall("OSSetAlarmUserData", hle_OSSetAlarmUserData);
    sh.registerSyscall("OSGetAlarmUserData", hle_OSGetAlarmUserData);

    // GHS runtime
    sh.registerSyscall("__ghs_mtx_init", hle_ghs_noop);
    sh.registerSyscall("__ghs_mtx_lock", hle_ghs_noop);
    sh.registerSyscall("__ghs_mtx_unlock", hle_ghs_noop);
    sh.registerSyscall("__ghs_mtx_dst", hle_ghs_noop);
    sh.registerSyscall("__ghs_flock_file", hle_ghs_noop);
    sh.registerSyscall("__ghs_flock_ptr", hle_ghs_noop);
    sh.registerSyscall("__ghs_funlock_file", hle_ghs_noop);
    sh.registerSyscall("__gh_set_errno", hle_ghs_noop);
    sh.registerSyscall("__gh_get_errno", hle_ghs_noop);
    sh.registerSyscall("memmove", hle_memmove);

    // Cache / barriers
    sh.registerSyscall("DCZeroRange", hle_DCZeroRange);
    sh.registerSyscall("DCFlushRangeNoSync", hle_cache_noop);
    sh.registerSyscall("DCStoreRange", hle_cache_noop);
    sh.registerSyscall("DCStoreRangeNoSync", hle_cache_noop);
    sh.registerSyscall("DCTouchRange", hle_cache_noop);
    sh.registerSyscall("ICInvalidateRange", hle_cache_noop);
    sh.registerSyscall("OSMemoryBarrier", hle_cache_noop);
    sh.registerSyscall("OSCoherencyBarrier", hle_cache_noop);
    sh.registerSyscall("OSIsAddressRangeDCValid", hle_OSIsAddressRangeDCValid);

    // Panic / report
    sh.registerSyscall("OSPanic", hle_OSPanic);
    sh.registerSyscall("OSVReport", hle_OSVReport);
    sh.registerSyscall("OSConsoleWrite", hle_OSConsoleWrite);

    // UC / MCP system config
    sh.registerSyscall("UCOpen", hle_UCOpen);
    sh.registerSyscall("UCClose", hle_UCClose);
    sh.registerSyscall("UCReadSysConfig", hle_UCReadSysConfig);
    sh.registerSyscall("MCP_Open", hle_MCP_Open);
    sh.registerSyscall("MCP_Close", hle_MCP_Close);
    sh.registerSyscall("MCP_GetSysProdSettings", hle_MCP_GetSysProdSettings);

    // OSDynLoad
    sh.registerSyscall("OSDynLoad_FindExport", hle_OSDynLoad_FindExport);
    sh.registerSyscall("OSDynLoad_Release", hle_OSDynLoad_Release);
    sh.registerSyscall("OSDynLoad_SetAllocator", hle_OSDynLoad_SetAllocator);
    sh.registerSyscall("OSDynLoad_GetAllocator", hle_OSDynLoad_GetAllocator);

    // Time / misc
    sh.registerSyscall("OSTicksToCalendarTime", hle_OSTicksToCalendarTime);
    sh.registerSyscall("OSTryLockMutex", hle_OSTryLockMutex);
    sh.registerSyscall("ENVGetEnvironmentVariable", hle_ENVGetEnvironmentVariable);
    sh.registerSyscall("OSGetSharedData", hle_OSGetSharedData);
    sh.registerSyscall("LCHardwareIsAvailable", hle_LCHardwareIsAvailable);
    sh.registerSyscall("LCGetMaxSize", hle_ghs_noop);
    sh.registerSyscall("LCGetAllocatableSize", hle_ghs_noop);
    sh.registerSyscall("OSIsEnabledOverlayArena", hle_ghs_noop);
    sh.registerSyscall("OSEnableOverlayArena", hle_ghs_noop);
    sh.registerSyscall("OSDisableOverlayArena", hle_ghs_noop);
    sh.registerSyscall("OSGetOverlayArenaRange", hle_OSGetOverlayArenaRange);
    sh.registerSyscall("OSSavesDone_ReadyToRelease", hle_ghs_noop);

    // zlib125.rpl
    sh.registerSyscall("inflateInit2_", hle_inflateInit2_);
    sh.registerSyscall("inflate", hle_inflate);
    sh.registerSyscall("inflateEnd", hle_inflateEnd);
    sh.registerSyscall("deflateInit2_", hle_deflateInit2_);
    sh.registerSyscall("deflate", hle_deflate);
    sh.registerSyscall("deflateBound", hle_deflateBound);
    sh.registerSyscall("deflateEnd", hle_deflateEnd);
}
