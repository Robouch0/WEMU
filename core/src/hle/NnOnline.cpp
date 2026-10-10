/*
** EPITECH PROJECT, 2026
** core
** File description:
** NnOnline -- see header. The title initialises a stack of online/account libraries during boot;
** each returns nn::Result-style codes (0 = success) and some register async completion callbacks.
** We report a self-consistent OFFLINE state: local account present, network layers "initialised"
** but not connected, and any async login completes (via the deferred callback pump) so the title
** stops waiting and proceeds to the offline menu.
*/

#include "NnOnline.hpp"

#include <cstdint>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/memory/Memory.hpp"
#include "hle/AsyncCallbacks.hpp"
#include "utils/Logger.hpp"

namespace {

    // nn::Result: 0 is the success sentinel these libraries test with IsSuccess().
    constexpr std::uint32_t kResultOk = 0;

    // Generic "returns nn::Result success".
    void ok(Core::Interpreter &cpu) { cpu.m_gpr[3] = kResultOk; }

    // Generic "returns bool true / false".
    void retTrue(Core::Interpreter &cpu) { cpu.m_gpr[3] = 1; }
    void retFalse(Core::Interpreter &cpu) { cpu.m_gpr[3] = 0; }

    // A C++ constructor returns `this` (r3), which arrives already in r3 — so leaving the registers
    // untouched is the correct behaviour. (The default unknown-import path would wrongly zero r3,
    // handing the caller a NULL object.)
    void ctorReturnThis(Core::Interpreter & /*cpu*/) {}

    // ---- nn::fp (friend presence) -------------------------------------------------------------
    // The title calls LoginAsync(callback, arg) and then waits for the callback to fire. Signature:
    //   Result LoginAsync(void (*cb)(nn::Result, void *user), void *user)
    // r3 = callback, r4 = user. We report the async launch succeeded and queue the callback to run
    // shortly with a success Result (r3=0) and the user pointer (r4), which unblocks the title.
    void hle_fp_LoginAsync(Core::Interpreter &cpu)
    {
        const std::uint32_t callback = cpu.m_gpr[3];
        const std::uint32_t user = cpu.m_gpr[4];
        // The completion callback is void(nn::Result const *result, void *user): it reads the Result
        // *through a pointer* (lwz r0,0(r3)). So r3 must point at a Result, not hold its value. Hand
        // it a small buffer containing a success Result (0).
        const std::uint32_t resultBuf = cpu.m_memory.heapAllocate(8, 8);
        cpu.m_memory.write<std::uint32_t>(resultBuf, kResultOk);
        cpu.m_memory.write<std::uint32_t>(resultBuf + 4, 0);
        Core::Async::enqueue(callback, resultBuf, user);
        Utils::Log::error("[nn::fp] LoginAsync -> success (callback 0x{:08X}, result@0x{:08X})", callback, resultBuf);
        cpu.m_gpr[3] = kResultOk;
    }

} // namespace

void RegisterNnOnlineFunctions()
{
    auto &sh = Core::syscallHandler;

    // ---- nn::ac (internet connection / auto-connect) ----
    sh.registerSyscall("Initialize__Q2_2nn2acFv", ok);
    sh.registerSyscall("Finalize__Q2_2nn2acFv", ok);
    sh.registerSyscall("Close__Q2_2nn2acFv", ok);
    sh.registerSyscall("Connect__Q2_2nn2acFv", ok); // "connected" (offline features still work)
    sh.registerSyscall("ConnectAsync__Q2_2nn2acFv", ok);
    sh.registerSyscall("IsApplicationConnected__Q2_2nn2acFPb", retFalse);
    sh.registerSyscall("GetConnectStatus__Q2_2nn2acFPQ3_2nn2ac6Status", ok);
    sh.registerSyscall("GetLastErrorCode__Q2_2nn2acFPUi", ok);
    sh.registerSyscall("GetStatus__Q2_2nn2acFPi", ok);

    // ---- nn::fp (friend presence) ----
    sh.registerSyscall("Initialize__Q2_2nn2fpFv", ok);
    sh.registerSyscall("Finalize__Q2_2nn2fpFv", ok);
    sh.registerSyscall("IsInitialized__Q2_2nn2fpFv", retTrue);
    sh.registerSyscall("LoginAsync__Q2_2nn2fpFPFQ2_2nn6ResultPv_vPv", hle_fp_LoginAsync);
    sh.registerSyscall("HasLoggedIn__Q2_2nn2fpFv", retTrue);
    sh.registerSyscall("Logout__Q2_2nn2fpFv", ok);
    sh.registerSyscall("UpdateGameMode__Q2_2nn2fpFPCQ3_2nn2fp8GameModePCwUi", ok);
    sh.registerSyscall("IsOnline__Q2_2nn2fpFv", retFalse);
    sh.registerSyscall("IsPreabsent__Q2_2nn2fpFv", retFalse);
    sh.registerSyscall("SetNotificationHandler__Q2_2nn2fpFUiPFQ3_2nn2fp16NotificationTypeUiPv_vPv", ok);
    sh.registerSyscall("GetMyPrincipalId__Q2_2nn2fpFv", [](Core::Interpreter &cpu) { cpu.m_gpr[3] = 0x80000001u; });

    // ---- nn::olv (Miiverse — discontinued; report unavailable so the title skips it) ----
    sh.registerSyscall("IsInitialized__Q2_2nn3olvFv", retFalse);
    sh.registerSyscall("Initialize__Q2_2nn3olvFPCQ3_2nn3olv15InitializeParam", ok);
    sh.registerSyscall("__ct__Q3_2nn3olv15InitializeParamFv", ctorReturnThis);
    sh.registerSyscall("SetWork__Q3_2nn3olv15InitializeParamFPUcUi", ok);
    sh.registerSyscall("SetReportTypes__Q3_2nn3olv15InitializeParamFUi", ok);
    sh.registerSyscall("GetReportTypes__Q3_2nn3olv6ReportFv", retFalse);
    sh.registerSyscall("__ct__Q3_2nn3olv19DownloadedTopicDataFv", ctorReturnThis);
    sh.registerSyscall("__ct__Q3_2nn3olv23DownloadedCommunityDataFv", ctorReturnThis);
    sh.registerSyscall("__ct__Q3_2nn3olv18DownloadedPostDataFv", ctorReturnThis);
    sh.registerSyscall("__ct__Q3_2nn3olv16UploadedPostDataFv", ctorReturnThis);
    sh.registerSyscall("__ct__Q3_2nn3olv30DownloadCommunityDataListParamFv", ctorReturnThis);
    sh.registerSyscall("__ct__Q3_2nn3olv24UploadCommunityDataParamFv", ctorReturnThis);

    // ---- nn::boss (background online tasks) ----
    sh.registerSyscall("Initialize__Q2_2nn4bossFv", ok);
    sh.registerSyscall("Finalize__Q2_2nn4bossFv", ok);
    sh.registerSyscall("GetBossState__Q2_2nn4bossFv", ok);
    sh.registerSyscall("__ct__Q3_2nn4boss5TitleFv", ctorReturnThis);
    sh.registerSyscall("__ct__Q3_2nn4boss4TaskFv", ctorReturnThis);
    sh.registerSyscall("__dt__Q3_2nn4boss4TaskFv", ctorReturnThis);
    sh.registerSyscall("Initialize__Q3_2nn4boss4TaskFPCc", ok);
    sh.registerSyscall("IsRegistered__Q3_2nn4boss4TaskCFv", retFalse);
    sh.registerSyscall("Register__Q3_2nn4boss4TaskFRQ3_2nn4boss11TaskSetting", ok);
    sh.registerSyscall("StartScheduling__Q3_2nn4boss4TaskFb", ok);
    sh.registerSyscall("__ct__Q3_2nn4boss7StorageFv", ctorReturnThis);
    sh.registerSyscall("__ct__Q3_2nn4boss6NsDataFv", ctorReturnThis);
    sh.registerSyscall("__ct__Q3_2nn4boss17PlayReportSettingFv", ctorReturnThis);
    sh.registerSyscall("Initialize__Q3_2nn4boss17PlayReportSettingFPvUi", ok);
    sh.registerSyscall("Set__Q3_2nn4boss17PlayReportSettingFPCcUi", ok);

    // ---- nn::ec (e-commerce / shop) ----
    sh.registerSyscall("Initialize__Q2_2nn2ecFUi", ok);
    sh.registerSyscall("SetAllocator__Q2_2nn2ecFPFUii_PvPFPv_v", ok);
    sh.registerSyscall("__ct__Q3_2nn2ec15ShoppingCatalogFv", ctorReturnThis);
    sh.registerSyscall("__ct__Q3_2nn2ec5QueryFv", ctorReturnThis);
    sh.registerSyscall("__ct__Q3_2nn2ec5MoneyFPCcN21", ctorReturnThis);

    // ---- nn::act extras (the base act HLE lives in Coreinit.cpp) ----
    sh.registerSyscall("GetSimpleAddressId__Q2_2nn3actFv", [](Core::Interpreter &cpu) { cpu.m_gpr[3] = 0x80000001u; });

    // ---- AOC (add-on content / DLC) ----
    sh.registerSyscall("AOC_Initialize", ok);
    sh.registerSyscall("AOC_Finalize", ok);
    sh.registerSyscall("AOC_DebugRealDeviceAccess", ok);
    // CalculateWorkBufferSize(count): return a modest work-buffer size the title then allocates.
    sh.registerSyscall("AOC_CalculateWorkBufferSize", [](Core::Interpreter &cpu) { cpu.m_gpr[3] = 0x1000u; });
    sh.registerSyscall("AOC_ListTitle", retFalse); // 0 DLC titles owned
    sh.registerSyscall("AOC_OpenTitle", ok);
    sh.registerSyscall("AOC_CloseTitle", ok);

    // ---- network plumbing the title pokes during online init (offline: succeed as no-ops) ----
    sh.registerSyscall("set_resolver_allocator", ok);
    sh.registerSyscall("socket_lib_init", ok);
    sh.registerSyscall("curl_global_init_mem", ok);
    sh.registerSyscall("socket_lib_finish", ok);
    sh.registerSyscall("NSSLInit", ok);
    sh.registerSyscall("NSSLFinish", ok);
    sh.registerSyscall("OSGetSecurityLevel", ok);

    // ---- high-frequency polls: register explicit no-op stubs so they stop appearing as
    // "unknown import" spam (all currently return 0 already; this just makes it intentional) ----
    sh.registerSyscall("KPADReadEx", retFalse); // 0 samples: no Wii Remotes connected
    sh.registerSyscall("KPADSetConnectCallback", ok);
    sh.registerSyscall("WPADProbe", [](Core::Interpreter &cpu) { cpu.m_gpr[3] = static_cast<std::uint32_t>(-1); }); // no controller
    sh.registerSyscall("VPADGetTPCalibratedPoint", ok);
    sh.registerSyscall("VPADBASEGetHeadphoneStatus", retFalse);
    sh.registerSyscall("WPADGetSpeakerVolume", retFalse);
    sh.registerSyscall("VPADStopMotor", ok);
    sh.registerSyscall("FSGetLastErrorCodeForViewer", ok); // FS_ERROR_OK
    sh.registerSyscall("LCIsDMAEnabled", retFalse);
    sh.registerSyscall("LCEnableDMA", ok);
    sh.registerSyscall("LCDisableDMA", ok);
    sh.registerSyscall("LCGetUnallocated", [](Core::Interpreter &cpu) { cpu.m_gpr[3] = 0x4000u; }); // 16 KB LC free
    sh.registerSyscall("LCAlloc", retFalse); // no locked-cache buffer (title has a fallback)
    sh.registerSyscall("LCDealloc", ok);
    sh.registerSyscall("MIXInit", ok);
    sh.registerSyscall("MIXInitInputControl", ok);
    sh.registerSyscall("MIXInitDeviceControl", ok);
    sh.registerSyscall("MIXAssignChannel", ok);
    sh.registerSyscall("MIXSetDeviceSoundMode", ok);
    sh.registerSyscall("MIXSetDeviceFader", ok);
    sh.registerSyscall("MIXUpdateSettings", ok);
}
