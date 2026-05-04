/*
** EPITECH PROJECT, 2025
** core
** File description:
** HLE WHB (WiiU Homebrew Library) stubs
*/

#include "Whb.hpp"
#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "gfx/Renderer.hpp"
#include <cstdio>

// ---- WHBProc ----

static void hle_WHBProcInit(Core::Interpreter& cpu)      { cpu.m_gpr[3] = 0; }
static void hle_WHBProcShutdown(Core::Interpreter& cpu)  { cpu.m_gpr[3] = 0; }
static void hle_WHBProcStopRunning(Core::Interpreter& cpu){ cpu.m_gpr[3] = 0; }

static void hle_WHBProcIsRunning(Core::Interpreter& cpu)
{
    if (cpu.m_renderer) {
        cpu.m_renderer->poll_events();
        cpu.m_gpr[3] = cpu.m_renderer->is_open() ? 1u : 0u;
    } else {
        cpu.m_gpr[3] = 1;
    }
}

// ---- WHBLog ----

static void hle_WHBLogInit(Core::Interpreter& cpu)           { cpu.m_gpr[3] = 1; }
static void hle_WHBLogCafeInit(Core::Interpreter& cpu)       { cpu.m_gpr[3] = 0; }
static void hle_WHBLogUdpInit(Core::Interpreter& cpu)        { cpu.m_gpr[3] = 0; }
static void hle_WHBLogCafeDeinit(Core::Interpreter& cpu)     { cpu.m_gpr[3] = 0; }
static void hle_WHBLogUdpDeinit(Core::Interpreter& cpu)      { cpu.m_gpr[3] = 0; }
static void hle_WHBLogConsoleDraw(Core::Interpreter& cpu)    { cpu.m_gpr[3] = 0; }
static void hle_WHBLogConsoleInit(Core::Interpreter& cpu)    { cpu.m_gpr[3] = 0; }
static void hle_WHBLogConsoleSetColor(Core::Interpreter& cpu){ cpu.m_gpr[3] = 0; }

static void hle_WHBLogPrint(Core::Interpreter& cpu)
{
    const uint8_t* p = cpu.m_memory.hostPtr(cpu.m_gpr[3]);
    if (p) fprintf(stderr, "[WHB] %s\n", (const char*)p);
    cpu.m_gpr[3] = 0;
}

static void hle_WHBLogConsolePrint(Core::Interpreter& cpu)
{
    const uint8_t* p = cpu.m_memory.hostPtr(cpu.m_gpr[3]);
    if (p) fprintf(stderr, "[WHBConsole] %s\n", (const char*)p);
    cpu.m_gpr[3] = 0;
}

static void hle_WHBLogCafePrint(Core::Interpreter& cpu)
{
    const uint8_t* p = cpu.m_memory.hostPtr(cpu.m_gpr[3]);
    if (p) fprintf(stderr, "[WHBCafe] %s\n", (const char*)p);
    cpu.m_gpr[3] = 0;
}

static void hle_WHBLogPrintf(Core::Interpreter& cpu)
{
    const char* fmt = (const char*)cpu.m_memory.hostPtr(cpu.m_gpr[3]);
    if (!fmt) { cpu.m_gpr[3] = 0; return; }
    uint32_t args[7];
    for (int i = 0; i < 7; i++) args[i] = cpu.m_gpr[4 + i];
    int N = 0;
    fprintf(stderr, "[WHB] ");
    for (int i = 0; fmt[i] && N <= 6; i++) {
        if (fmt[i] != '%') { fputc(fmt[i], stderr); continue; }
        i++;
        if (!fmt[i]) break;
        switch (fmt[i]) {
        case 'd': case 'i': fprintf(stderr, "%d",  (int)args[N++]);  break;
        case 'u':           fprintf(stderr, "%u",  args[N++]);       break;
        case 'x': case 'X': fprintf(stderr, "%X",  args[N++]);       break;
        case 's': {
            const char* p = (const char*)cpu.m_memory.hostPtr(args[N++]);
            if (p) fprintf(stderr, "%s", p);
            break;
        }
        case '%': fputc('%', stderr); break;
        default:  fputc('%', stderr); fputc(fmt[i], stderr); N++; break;
        }
    }
    fputc('\n', stderr);
    cpu.m_gpr[3] = 0;
}

void RegisterWhbFunctions()
{
    // WHBProc
    Core::syscallHandler.registerSyscall("WHBProcInit",          hle_WHBProcInit);
    Core::syscallHandler.registerSyscall("WHBProcIsRunning",     hle_WHBProcIsRunning);
    Core::syscallHandler.registerSyscall("WHBProcShutdown",      hle_WHBProcShutdown);
    Core::syscallHandler.registerSyscall("WHBProcStopRunning",   hle_WHBProcStopRunning);

    // WHBLog
    Core::syscallHandler.registerSyscall("WHBLogInit",           hle_WHBLogInit);
    Core::syscallHandler.registerSyscall("WHBLogCafeInit",       hle_WHBLogCafeInit);
    Core::syscallHandler.registerSyscall("WHBLogUdpInit",        hle_WHBLogUdpInit);
    Core::syscallHandler.registerSyscall("WHBLogCafeDeinit",     hle_WHBLogCafeDeinit);
    Core::syscallHandler.registerSyscall("WHBLogUdpDeinit",      hle_WHBLogUdpDeinit);
    Core::syscallHandler.registerSyscall("WHBLogPrint",          hle_WHBLogPrint);
    Core::syscallHandler.registerSyscall("WHBLogPrintf",         hle_WHBLogPrintf);
    Core::syscallHandler.registerSyscall("WHBLogConsolePrint",   hle_WHBLogConsolePrint);
    Core::syscallHandler.registerSyscall("WHBLogConsoleDraw",    hle_WHBLogConsoleDraw);
    Core::syscallHandler.registerSyscall("WHBLogConsoleInit",    hle_WHBLogConsoleInit);
    Core::syscallHandler.registerSyscall("WHBLogConsoleSetColor",hle_WHBLogConsoleSetColor);
    Core::syscallHandler.registerSyscall("WHBLogCafePrint",      hle_WHBLogCafePrint);
}
