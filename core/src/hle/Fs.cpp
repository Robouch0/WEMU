#include "Fs.hpp"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cpu/interpreter/Interpreter.hpp"
#include "cpu/interpreter/SyscallHandler.hpp"
#include "cpu/memory/Memory.hpp"
#include "utils/Diagnostics.hpp"
#include "utils/Logger.hpp"

// Cafe filesystem HLE, backed by the host filesystem.
//
// Wii U titles read assets through paths like "/vol/content/ui/...". We translate that prefix to the
// extracted game's content directory on the host and service the synchronous FS* calls (the only
// variants MK8 imports) with real fopen/fread. File handles are small integers we hand out and track
// host-side. FSStatus: 0 = OK, negative = error (we use NOT_FOUND = -6).

namespace {

    constexpr std::int32_t FS_STATUS_OK = 0;
    constexpr std::int32_t FS_STATUS_END = -2; // no more directory entries
    constexpr std::int32_t FS_STATUS_NOT_FOUND = -6;
    constexpr std::int32_t FS_STATUS_NOT_DIR = -8;
    constexpr std::int32_t FS_STATUS_FATAL_ERROR = -0x400;

    std::string g_contentRoot; // host dir backing "/vol/content"
    std::vector<std::string> g_contentLayers; // fallback roots (e.g. base game under an update)
    std::unordered_map<std::uint32_t, std::string> g_workingDirectories;

    std::string workingDirectory(std::uint32_t client)
    {
        const auto it = g_workingDirectories.find(client);
        return it == g_workingDirectories.end() ? "/" : it->second;
    }

    std::string resolveGuestPath(std::uint32_t client, const std::string &path)
    {
        return (std::filesystem::path(workingDirectory(client)) / path).lexically_normal().generic_string();
    }

    bool isWithinMount(const std::string &path, const std::string &mount)
    {
        return path == mount || (path.starts_with(mount) && path[mount.size()] == '/');
    }

    struct OpenFile {
            std::FILE *fp{nullptr};
            std::string guestPath;
            std::string hostPath;
    };
    std::unordered_map<std::uint32_t, OpenFile> g_openFiles;
    std::uint32_t g_nextHandle = 1;

    // Open directory iteration: a snapshot of entries taken at FSOpenDir time plus a cursor.
    struct OpenDir {
            std::vector<std::filesystem::directory_entry> entries;
            std::size_t cursor{0};
    };
    std::unordered_map<std::uint32_t, OpenDir> g_openDirs;

    // Reads a NUL-terminated guest string (bounded) into a std::string.
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

    // Maps a guest FS path to a host path. "/vol/content" is the extracted game content;
    // "/vol/save" is a writable directory created next to it.
    std::string translatePath(const std::string &guestPath)
    {
        static const std::string kContent = "/vol/content";
        static const std::string kSave = "/vol/save";
        if (isWithinMount(guestPath, kContent)) {
            const std::string rel = guestPath.substr(kContent.size());
            // Title updates overlay the base content: first root that has the file wins.
            std::error_code ec;
            if (!g_contentLayers.empty() && !std::filesystem::exists(g_contentRoot + rel, ec))
                for (const std::string &layer: g_contentLayers)
                    if (std::filesystem::exists(layer + rel, ec))
                        return layer + rel;
            return g_contentRoot + rel;
        }
        if (isWithinMount(guestPath, kSave)) {
            const std::string saveRoot = g_contentRoot + "/../wemu_save";
            std::error_code ec;
            std::filesystem::create_directories(saveRoot, ec);
            return saveRoot + guestPath.substr(kSave.size());
        }
        // An unmounted guest path must not fall through to the host filesystem.
        return {};
    }

    // Directory overlays differ from file overlays: an update may contain an empty directory while
    // the base title provides most of the entries. The Wii U content view should expose the merged
    // directory, with earlier roots winning when names collide.
    std::vector<std::filesystem::path> translateDirCandidates(const std::string &guestPath)
    {
        static const std::string kContent = "/vol/content";
        std::vector<std::filesystem::path> out;
        if (isWithinMount(guestPath, kContent)) {
            const std::string rel = guestPath.substr(kContent.size());
            out.emplace_back(g_contentRoot + rel);
            for (const std::string &layer: g_contentLayers)
                out.emplace_back(layer + rel);
            return out;
        }
        out.emplace_back(translatePath(guestPath));
        return out;
    }

    void writeStat(Core::Interpreter &cpu, std::uint32_t statPtr, std::uint64_t size, bool isDir)
    {
        if (!statPtr)
            return;
        for (std::uint32_t off = 0; off < 0x64; off += 4) // zero the FSStat struct
            cpu.m_memory.write<std::uint32_t>(statPtr + off, 0);
        constexpr std::uint32_t FS_STAT_FLAG_IS_DIRECTORY = 0x80000000u;
        cpu.m_memory.write<std::uint32_t>(statPtr + 0x00, isDir ? FS_STAT_FLAG_IS_DIRECTORY : 0u); // flags
        cpu.m_memory.write<std::uint32_t>(statPtr + 0x10, static_cast<std::uint32_t>(size)); // size
    }

    // ---- handlers ----

    void fs_noop_ok(Core::Interpreter &cpu) { cpu.m_gpr[3] = FS_STATUS_OK; }

    void fs_AddClient(Core::Interpreter &cpu)
    {
        g_workingDirectories[cpu.m_gpr[3]] = "/";
        cpu.m_gpr[3] = FS_STATUS_OK;
    }

    void fs_DelClient(Core::Interpreter &cpu)
    {
        g_workingDirectories.erase(cpu.m_gpr[3]);
        cpu.m_gpr[3] = FS_STATUS_OK;
    }

    void fs_GetVolumeState(Core::Interpreter &cpu)
    {
        // The host content mount is synchronous. INITIAL (0) would keep titles
        // waiting for media even after successful asset reads.
        std::error_code ec;
        cpu.m_gpr[3] = std::filesystem::is_directory(g_contentRoot, ec) ? 1u : 2u; // READY / NO_MEDIA
    }

    // WEMU_FS_TRACE=1 echoes every path the title asks for to stderr. The build-time log level is a
    // constant, so bring-up tracing that has to survive a release build goes through here.
    bool fsTrace()
    {
        static const bool on = []() {
            const char *e = std::getenv("WEMU_FS_TRACE");
            return e && e[0] == '1';
        }();
        return on;
    }

    void traceStackForPath(Core::Interpreter &cpu, const std::string &guestPath, const char *op)
    {
        const char *filter = std::getenv("WEMU_FS_TRACE_STACK");
        if (!filter || !filter[0] || guestPath.find(filter) == std::string::npos)
            return;
        const std::uint32_t lr = cpu.m_lr + Core::Memory::MemoryMap::ApplicationCode;
        std::fprintf(stderr, "[FSTRACE] stack op=%s path=%s thread=0x%08X LR=%s\n", op, guestPath.c_str(), cpu.m_scheduler.currentHandle(),
                     Core::Diag::symbolize(cpu, lr).c_str());
        std::fprintf(stderr, "[FSTRACE]   regs r3=%08X r4=%08X r5=%08X r6=%08X r7=%08X r8=%08X r9=%08X r10=%08X sp=%08X\n", cpu.m_gpr[3],
                     cpu.m_gpr[4], cpu.m_gpr[5], cpu.m_gpr[6], cpu.m_gpr[7], cpu.m_gpr[8], cpu.m_gpr[9], cpu.m_gpr[10], cpu.m_gpr[1]);
        if (std::strcmp(op, "statfile") == 0 && cpu.m_gpr[6]) {
            std::fprintf(stderr, "[FSTRACE]   statOut@0x%08X", cpu.m_gpr[6]);
            for (std::uint32_t off = 0; off <= 0x20; off += 4) {
                try {
                    std::fprintf(stderr, " +%02X=%08X", off, cpu.m_memory.read<std::uint32_t>(cpu.m_gpr[6] + off));
                } catch (...) {
                    std::fprintf(stderr, " +%02X=<bad>", off);
                }
            }
            std::fprintf(stderr, "\n");
        }
        std::uint32_t sp = cpu.m_gpr[1];
        for (int frame = 0; frame < 8 && sp; frame++) {
            const std::uint32_t callerSp = [&]() {
                try {
                    return cpu.m_memory.read<std::uint32_t>(sp);
                } catch (...) {
                    return 0u;
                }
            }();
            if (!callerSp || callerSp <= sp)
                break;
            const std::uint32_t savedLr = [&]() {
                try {
                    return cpu.m_memory.read<std::uint32_t>(callerSp + 4);
                } catch (...) {
                    return 0u;
                }
            }();
            std::fprintf(stderr, "[FSTRACE]   #%d sp=0x%08X ret=%s\n", frame, callerSp,
                         Core::Diag::symbolize(cpu, savedLr + Core::Memory::MemoryMap::ApplicationCode).c_str());
            sp = callerSp;
        }
    }

    // FSOpenFile(client, cmd, const char* path, const char* mode, FSFileHandle* outHandle, errMask)
    void fs_OpenFile(Core::Interpreter &cpu)
    {
        const std::string guestPath = resolveGuestPath(cpu.m_gpr[3], readGuestString(cpu, cpu.m_gpr[5]));
        const std::string mode = readGuestString(cpu, cpu.m_gpr[6], 8);
        const std::uint32_t outHandlePtr = cpu.m_gpr[7];
        const std::string host = translatePath(guestPath);

        const bool writing = mode.find('w') != std::string::npos || mode.find('a') != std::string::npos;
        std::FILE *fp = std::fopen(host.c_str(), writing ? "r+b" : "rb");
        if (!fp && writing) { // create missing save files (and their parent directories)
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(host).parent_path(), ec);
            fp = std::fopen(host.c_str(), "w+b");
        }
        if (fsTrace())
            std::fprintf(stderr, "[FSTRACE] open%s %s mode=%s\n", fp ? "" : " MISS", guestPath.c_str(), mode.c_str());
        traceStackForPath(cpu, guestPath, "open");
        if (!fp) {
            Utils::Log::debug("[FS] OpenFile MISS {} -> {}", guestPath, host);
            cpu.m_gpr[3] = FS_STATUS_NOT_FOUND;
            return;
        }
        const std::uint32_t handle = g_nextHandle++;
        g_openFiles[handle] = OpenFile{fp, guestPath, host};
        if (outHandlePtr)
            cpu.m_memory.write<std::uint32_t>(outHandlePtr, handle);
        if (fsTrace())
            std::fprintf(stderr, "[FSTRACE] open handle=%u path=%s host=%s\n", handle, guestPath.c_str(), host.c_str());
        Utils::Log::debug("[FS] OpenFile {} -> {} handle={}", guestPath, host, handle);
        cpu.m_gpr[3] = FS_STATUS_OK;
    }

    // Copies `bytes` from the file into the guest buffer; returns bytes actually read (or -1).
    std::int64_t readIntoGuest(Core::Interpreter &cpu, std::FILE *fp, std::uint32_t bufAddr, std::uint64_t bytes)
    {
        std::uint8_t *dst = cpu.m_memory.hostPtr(bufAddr);
        if (!dst)
            return -1;
        return static_cast<std::int64_t>(std::fread(dst, 1, bytes, fp));
    }

    // FSReadFile(client, cmd, buffer, size, count, FSFileHandle handle, unk, errMask)
    // Returns the number of `size`-byte blocks read, or a negative FSStatus.
    void fs_ReadFile(Core::Interpreter &cpu)
    {
        const std::uint32_t buffer = cpu.m_gpr[5];
        const std::uint32_t size = cpu.m_gpr[6];
        const std::uint32_t count = cpu.m_gpr[7];
        const std::uint32_t handle = cpu.m_gpr[8];
        auto it = g_openFiles.find(handle);
        if (it == g_openFiles.end() || size == 0) {
            cpu.m_gpr[3] = FS_STATUS_NOT_FOUND;
            return;
        }
        const std::int64_t read = readIntoGuest(cpu, it->second.fp, buffer, static_cast<std::uint64_t>(size) * count);
        if (fsTrace())
            std::fprintf(stderr, "[FSTRACE] read handle=%u path=%s size=%u count=%u -> %lld blocks\n", handle, it->second.guestPath.c_str(), size,
                         count, static_cast<long long>(read < 0 ? read : read / size));
        traceStackForPath(cpu, it->second.guestPath, "read");
        cpu.m_gpr[3] = read < 0 ? FS_STATUS_NOT_FOUND : static_cast<std::int32_t>(read / size);
    }

    // FSReadFileWithPos(client, cmd, buffer, size, count, pos, FSFileHandle handle, unk, errMask)
    void fs_ReadFileWithPos(Core::Interpreter &cpu)
    {
        const std::uint32_t buffer = cpu.m_gpr[5];
        const std::uint32_t size = cpu.m_gpr[6];
        const std::uint32_t count = cpu.m_gpr[7];
        const std::uint32_t pos = cpu.m_gpr[8];
        const std::uint32_t handle = cpu.m_gpr[9];
        auto it = g_openFiles.find(handle);
        if (it == g_openFiles.end() || size == 0) {
            cpu.m_gpr[3] = FS_STATUS_NOT_FOUND;
            return;
        }
        std::fseek(it->second.fp, static_cast<long>(pos), SEEK_SET);
        const std::int64_t read = readIntoGuest(cpu, it->second.fp, buffer, static_cast<std::uint64_t>(size) * count);
        if (fsTrace())
            std::fprintf(stderr, "[FSTRACE] readpos handle=%u path=%s pos=%u size=%u count=%u -> %lld blocks\n", handle, it->second.guestPath.c_str(),
                         pos, size, count, static_cast<long long>(read < 0 ? read : read / size));
        traceStackForPath(cpu, it->second.guestPath, "readpos");
        cpu.m_gpr[3] = read < 0 ? FS_STATUS_NOT_FOUND : static_cast<std::int32_t>(read / size);
    }

    // FSSetPosFile(client, cmd, FSFileHandle handle, pos, errMask)
    void fs_SetPosFile(Core::Interpreter &cpu)
    {
        auto it = g_openFiles.find(cpu.m_gpr[5]);
        if (it == g_openFiles.end()) {
            cpu.m_gpr[3] = FS_STATUS_NOT_FOUND;
            return;
        }
        if (fsTrace())
            std::fprintf(stderr, "[FSTRACE] setpos handle=%u path=%s pos=%u\n", cpu.m_gpr[5], it->second.guestPath.c_str(), cpu.m_gpr[6]);
        traceStackForPath(cpu, it->second.guestPath, "setpos");
        std::fseek(it->second.fp, static_cast<long>(cpu.m_gpr[6]), SEEK_SET);
        cpu.m_gpr[3] = FS_STATUS_OK;
    }

    // FSGetStatFile(client, cmd, FSFileHandle handle, FSStat* stat, errMask)
    void fs_GetStatFile(Core::Interpreter &cpu)
    {
        auto it = g_openFiles.find(cpu.m_gpr[5]);
        if (it == g_openFiles.end()) {
            cpu.m_gpr[3] = FS_STATUS_NOT_FOUND;
            return;
        }
        std::FILE *fp = it->second.fp;
        const long cur = std::ftell(fp);
        std::fseek(fp, 0, SEEK_END);
        const long end = std::ftell(fp);
        std::fseek(fp, cur, SEEK_SET);
        if (fsTrace())
            std::fprintf(stderr, "[FSTRACE] statfile handle=%u path=%s size=%ld\n", cpu.m_gpr[5], it->second.guestPath.c_str(), end);
        writeStat(cpu, cpu.m_gpr[6], static_cast<std::uint64_t>(end < 0 ? 0 : end), false);
        traceStackForPath(cpu, it->second.guestPath, "statfile");
        cpu.m_gpr[3] = FS_STATUS_OK;
    }

    // FSGetStat(client, cmd, const char* path, FSStat* stat, errMask)
    void fs_GetStat(Core::Interpreter &cpu)
    {
        const std::string guestPath = resolveGuestPath(cpu.m_gpr[3], readGuestString(cpu, cpu.m_gpr[5]));
        const std::string host = translatePath(guestPath);
        std::error_code ec;
        if (std::filesystem::is_directory(host, ec)) {
            if (fsTrace())
                std::fprintf(stderr, "[FSTRACE] stat %s dir\n", guestPath.c_str());
            traceStackForPath(cpu, guestPath, "stat");
            writeStat(cpu, cpu.m_gpr[6], 0, true);
            cpu.m_gpr[3] = FS_STATUS_OK;
            return;
        }
        const auto sz = std::filesystem::file_size(host, ec);
        if (ec) {
            if (fsTrace())
                std::fprintf(stderr, "[FSTRACE] stat MISS %s\n", guestPath.c_str());
            traceStackForPath(cpu, guestPath, "stat");
            cpu.m_gpr[3] = FS_STATUS_NOT_FOUND;
            return;
        }
        if (fsTrace())
            std::fprintf(stderr, "[FSTRACE] stat %s size=%llu\n", guestPath.c_str(), static_cast<unsigned long long>(sz));
        traceStackForPath(cpu, guestPath, "stat");
        writeStat(cpu, cpu.m_gpr[6], sz, false);
        cpu.m_gpr[3] = FS_STATUS_OK;
    }

    // FSCloseFile(client, cmd, FSFileHandle handle, errMask)
    void fs_CloseFile(Core::Interpreter &cpu)
    {
        auto it = g_openFiles.find(cpu.m_gpr[5]);
        if (it != g_openFiles.end()) {
            if (fsTrace())
                std::fprintf(stderr, "[FSTRACE] close handle=%u path=%s\n", cpu.m_gpr[5], it->second.guestPath.c_str());
            traceStackForPath(cpu, it->second.guestPath, "close");
            std::fclose(it->second.fp);
            g_openFiles.erase(it);
        }
        cpu.m_gpr[3] = FS_STATUS_OK;
    }

    // FSGetCwd(client, cmd, char* buffer, length, errMask)
    void fs_GetCwd(Core::Interpreter &cpu)
    {
        const std::uint32_t buffer = cpu.m_gpr[5];
        const std::uint32_t length = cpu.m_gpr[6];
        const std::string cwd = workingDirectory(cpu.m_gpr[3]);
        auto *dst = cpu.m_memory.hostPtr(buffer);
        if (!dst || length < 0x27F || buffer > UINT32_MAX - cwd.size() || !cpu.m_memory.hostPtr(buffer + static_cast<std::uint32_t>(cwd.size()))) {
            cpu.m_gpr[3] = FS_STATUS_FATAL_ERROR;
            return;
        }
        std::memcpy(dst, cwd.c_str(), cwd.size() + 1);
        cpu.m_gpr[3] = FS_STATUS_OK;
    }

    void fs_ChangeDir(Core::Interpreter &cpu)
    {
        const auto client = cpu.m_gpr[3];
        const std::string path = resolveGuestPath(client, readGuestString(cpu, cpu.m_gpr[5]));
        bool found = path == "/" || path == "/vol" || path == "/vol/";
        std::error_code ec;
        for (const auto &host: translateDirCandidates(path))
            found |= std::filesystem::is_directory(host, ec);
        if (!found) {
            cpu.m_gpr[3] = std::filesystem::exists(translatePath(path), ec) ? FS_STATUS_NOT_DIR : FS_STATUS_NOT_FOUND;
            return;
        }
        if (path.size() + (path.back() != '/') >= 0x27F) {
            cpu.m_gpr[3] = FS_STATUS_FATAL_ERROR;
            return;
        }
        g_workingDirectories[client] = path.back() == '/' ? path : path + "/";
        cpu.m_gpr[3] = FS_STATUS_OK;
    }

    // FSOpenDir(client, cmd, const char* path, FSDirHandle* outHandle, errMask)
    void fs_OpenDir(Core::Interpreter &cpu)
    {
        const std::string guestPath = resolveGuestPath(cpu.m_gpr[3], readGuestString(cpu, cpu.m_gpr[5]));
        std::error_code ec;
        const auto candidates = translateDirCandidates(guestPath);
        bool anyDir = false;
        OpenDir dir;
        std::unordered_set<std::string> seenNames;
        for (const auto &host: candidates) {
            if (!std::filesystem::is_directory(host, ec))
                continue;
            anyDir = true;
            for (const auto &entry: std::filesystem::directory_iterator(host, ec)) {
                const std::string name = entry.path().filename().string();
                if (seenNames.insert(name).second)
                    dir.entries.push_back(entry);
            }
        }
        if (fsTrace())
            std::fprintf(stderr, "[FSTRACE] opendir%s %s\n", anyDir ? "" : " MISS", guestPath.c_str());
        if (!anyDir) {
            Utils::Log::debug("[FS] OpenDir MISS {}", guestPath);
            cpu.m_gpr[3] = FS_STATUS_NOT_FOUND;
            return;
        }
        const std::uint32_t handle = g_nextHandle++;
        g_openDirs[handle] = std::move(dir);
        if (cpu.m_gpr[6])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[6], handle);
        Utils::Log::debug("[FS] OpenDir {} entries={} handle={}", guestPath, g_openDirs[handle].entries.size(), handle);
        cpu.m_gpr[3] = FS_STATUS_OK;
    }

    // FSReadDir(client, cmd, FSDirHandle handle, FSDirEntry* entry, errMask)
    // FSDirEntry = FSStat (0x64 bytes) followed by char name[256].
    void fs_ReadDir(Core::Interpreter &cpu)
    {
        auto it = g_openDirs.find(cpu.m_gpr[5]);
        const std::uint32_t entryPtr = cpu.m_gpr[6];
        if (it == g_openDirs.end() || !entryPtr) {
            cpu.m_gpr[3] = FS_STATUS_NOT_FOUND;
            return;
        }
        OpenDir &dir = it->second;
        if (dir.cursor >= dir.entries.size()) {
            if (fsTrace())
                std::fprintf(stderr, "[FSTRACE] readdir END handle=%u\n", cpu.m_gpr[5]);
            cpu.m_gpr[3] = FS_STATUS_END;
            return;
        }
        const auto &e = dir.entries[dir.cursor++];
        std::error_code ec;
        const bool isDir = e.is_directory(ec);
        const std::uint64_t size = isDir ? 0 : e.file_size(ec);
        writeStat(cpu, entryPtr, size, isDir);
        const std::string name = e.path().filename().string();
        for (std::size_t i = 0; i < 255 && i < name.size(); i++)
            cpu.m_memory.write<std::uint8_t>(entryPtr + 0x64 + static_cast<std::uint32_t>(i), static_cast<std::uint8_t>(name[i]));
        cpu.m_memory.write<std::uint8_t>(entryPtr + 0x64 + static_cast<std::uint32_t>(std::min<std::size_t>(name.size(), 255)), 0);
        if (fsTrace())
            std::fprintf(stderr, "[FSTRACE] readdir handle=%u name=%s%s size=%llu\n", cpu.m_gpr[5], name.c_str(), isDir ? "/" : "",
                         static_cast<unsigned long long>(size));
        cpu.m_gpr[3] = FS_STATUS_OK;
    }

    // FSCloseDir(client, cmd, FSDirHandle handle, errMask)
    void fs_CloseDir(Core::Interpreter &cpu)
    {
        g_openDirs.erase(cpu.m_gpr[5]);
        cpu.m_gpr[3] = FS_STATUS_OK;
    }

    // FSWriteFile(client, cmd, buffer, size, count, FSFileHandle handle, unk, errMask)
    void fs_WriteFile(Core::Interpreter &cpu)
    {
        const std::uint32_t buffer = cpu.m_gpr[5];
        const std::uint32_t size = cpu.m_gpr[6];
        const std::uint32_t count = cpu.m_gpr[7];
        auto it = g_openFiles.find(cpu.m_gpr[8]);
        const std::uint8_t *src = cpu.m_memory.hostPtr(buffer);
        if (it == g_openFiles.end() || size == 0 || !src) {
            cpu.m_gpr[3] = FS_STATUS_NOT_FOUND;
            return;
        }
        const std::size_t written = std::fwrite(src, 1, static_cast<std::size_t>(size) * count, it->second.fp);
        cpu.m_gpr[3] = static_cast<std::int32_t>(written / size);
    }

    // Mount sources aren't modeled (no SD card / USB); report none so titles skip external media.
    void fs_GetMountSource(Core::Interpreter &cpu) { cpu.m_gpr[3] = FS_STATUS_NOT_FOUND; }
    void fs_Mount(Core::Interpreter &cpu) { cpu.m_gpr[3] = FS_STATUS_NOT_FOUND; }

    // ---- SAVE library ----
    // SAVE* mirror the FS* calls with an account slot inserted as the third argument (r5) and
    // paths rooted in that slot's save directory. Slot 255 is the "common" save.

    std::string saveGuestPath(std::uint32_t slot, const std::string &rel)
    {
        const std::string dir = slot == 255 ? "common" : "80000001";
        if (rel.empty())
            return "/vol/save/" + dir;
        return "/vol/save/" + dir + (rel[0] == '/' ? "" : "/") + rel;
    }

    void writeGuestString(Core::Interpreter &cpu, std::uint32_t addr, std::uint32_t maxLen, const std::string &s)
    {
        if (!addr || !maxLen)
            return;
        const std::uint32_t n = std::min<std::uint32_t>(static_cast<std::uint32_t>(s.size()), maxLen - 1);
        for (std::uint32_t i = 0; i < n; i++)
            cpu.m_memory.write<std::uint8_t>(addr + i, static_cast<std::uint8_t>(s[i]));
        cpu.m_memory.write<std::uint8_t>(addr + n, 0);
    }

    // SAVEOpenDir(client, cmd, u8 slot, const char* path, FSDirHandle* outHandle, errMask)
    void save_OpenDir(Core::Interpreter &cpu)
    {
        const std::string guestPath = saveGuestPath(cpu.m_gpr[5], readGuestString(cpu, cpu.m_gpr[6]));
        const std::string host = translatePath(guestPath);
        std::error_code ec;
        std::filesystem::create_directories(host, ec); // save dirs exist implicitly on a real console
        OpenDir dir;
        for (const auto &entry: std::filesystem::directory_iterator(host, ec))
            dir.entries.push_back(entry);
        const std::uint32_t handle = g_nextHandle++;
        g_openDirs[handle] = std::move(dir);
        if (cpu.m_gpr[7])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[7], handle);
        Utils::Log::debug("[FS] SAVEOpenDir {} -> {} handle={}", guestPath, host, handle);
        cpu.m_gpr[3] = FS_STATUS_OK;
    }

    // SAVEOpenFile(client, cmd, u8 slot, const char* path, const char* mode, FSFileHandle* out, errMask)
    void save_OpenFile(Core::Interpreter &cpu)
    {
        const std::string guestPath = saveGuestPath(cpu.m_gpr[5], readGuestString(cpu, cpu.m_gpr[6]));
        const std::string mode = readGuestString(cpu, cpu.m_gpr[7], 8);
        const std::string host = translatePath(guestPath);
        const bool writing = mode.find('w') != std::string::npos || mode.find('a') != std::string::npos;
        std::FILE *fp = std::fopen(host.c_str(), writing ? "r+b" : "rb");
        if (!fp && writing) {
            std::error_code ec;
            std::filesystem::create_directories(std::filesystem::path(host).parent_path(), ec);
            fp = std::fopen(host.c_str(), "w+b");
        }
        if (!fp) {
            Utils::Log::debug("[FS] SAVEOpenFile MISS {} -> {}", guestPath, host);
            cpu.m_gpr[3] = FS_STATUS_NOT_FOUND;
            return;
        }
        const std::uint32_t handle = g_nextHandle++;
        g_openFiles[handle] = OpenFile{fp, guestPath, host};
        if (cpu.m_gpr[8])
            cpu.m_memory.write<std::uint32_t>(cpu.m_gpr[8], handle);
        if (fsTrace())
            std::fprintf(stderr, "[FSTRACE] saveopen handle=%u path=%s host=%s mode=%s\n", handle, guestPath.c_str(), host.c_str(), mode.c_str());
        Utils::Log::debug("[FS] SAVEOpenFile {} ({}) -> handle={}", guestPath, mode, handle);
        cpu.m_gpr[3] = FS_STATUS_OK;
    }

    // SAVEGetStat(client, cmd, u8 slot, const char* path, FSStat* stat, errMask)
    void save_GetStat(Core::Interpreter &cpu)
    {
        const std::string host = translatePath(saveGuestPath(cpu.m_gpr[5], readGuestString(cpu, cpu.m_gpr[6])));
        std::error_code ec;
        if (std::filesystem::is_directory(host, ec)) {
            writeStat(cpu, cpu.m_gpr[7], 0, true);
            cpu.m_gpr[3] = FS_STATUS_OK;
            return;
        }
        const auto sz = std::filesystem::file_size(host, ec);
        if (ec) {
            cpu.m_gpr[3] = FS_STATUS_NOT_FOUND;
            return;
        }
        writeStat(cpu, cpu.m_gpr[7], sz, false);
        cpu.m_gpr[3] = FS_STATUS_OK;
    }

    // SAVERemove(client, cmd, u8 slot, const char* path, errMask)
    void save_Remove(Core::Interpreter &cpu)
    {
        const std::string host = translatePath(saveGuestPath(cpu.m_gpr[5], readGuestString(cpu, cpu.m_gpr[6])));
        std::error_code ec;
        std::filesystem::remove_all(host, ec);
        cpu.m_gpr[3] = FS_STATUS_OK;
    }

    // SAVEGetFreeSpaceSize(client, cmd, u8 slot, u64* outSize, errMask)
    void save_GetFreeSpaceSize(Core::Interpreter &cpu)
    {
        if (cpu.m_gpr[6])
            cpu.m_memory.write<std::uint64_t>(cpu.m_gpr[6], 512ull * 1024 * 1024);
        cpu.m_gpr[3] = FS_STATUS_OK;
    }

    // SAVEInitSaveDir(u8 slot)
    void save_InitSaveDir(Core::Interpreter &cpu)
    {
        std::error_code ec;
        std::filesystem::create_directories(translatePath(saveGuestPath(cpu.m_gpr[3], "")), ec);
        cpu.m_gpr[3] = FS_STATUS_OK;
    }

    // SAVEGetSharedDataTitlePath(u64 titleId, const char* file, char* out, u32 outSize)
    // Points into another title's /vol/content; we have no shared titles installed, so hand back
    // a distinctive path -- the subsequent FSOpenFile misses and titles take their fallback path.
    void save_GetSharedDataTitlePath(Core::Interpreter &cpu)
    {
        const std::string file = readGuestString(cpu, cpu.m_gpr[5]);
        writeGuestString(cpu, cpu.m_gpr[6], cpu.m_gpr[7], "/vol/shared_data/" + file);
        cpu.m_gpr[3] = FS_STATUS_OK;
    }

    // SAVEGetSharedSaveDataPath(u64 titleId, const char* file, char* out, u32 outSize)
    void save_GetSharedSaveDataPath(Core::Interpreter &cpu)
    {
        const std::string file = readGuestString(cpu, cpu.m_gpr[5]);
        writeGuestString(cpu, cpu.m_gpr[6], cpu.m_gpr[7], "/vol/shared_save/" + file);
        cpu.m_gpr[3] = FS_STATUS_OK;
    }

} // namespace

void SetFsContentRoot(const std::string &hostPath) { g_contentRoot = hostPath; }
void AddFsContentLayer(const std::string &hostPath) { g_contentLayers.push_back(hostPath); }

void RegisterFsFunctions()
{
    Core::syscallHandler.registerSyscall("FSAddClient", fs_AddClient);
    Core::syscallHandler.registerSyscall("FSDelClient", fs_DelClient);
    Core::syscallHandler.registerSyscall("FSGetVolumeState", fs_GetVolumeState);
    Core::syscallHandler.registerSyscall("FSOpenFile", fs_OpenFile);
    Core::syscallHandler.registerSyscall("FSReadFile", fs_ReadFile);
    Core::syscallHandler.registerSyscall("FSReadFileWithPos", fs_ReadFileWithPos);
    Core::syscallHandler.registerSyscall("FSSetPosFile", fs_SetPosFile);
    Core::syscallHandler.registerSyscall("FSGetStatFile", fs_GetStatFile);
    Core::syscallHandler.registerSyscall("FSGetStat", fs_GetStat);
    Core::syscallHandler.registerSyscall("FSCloseFile", fs_CloseFile);
    Core::syscallHandler.registerSyscall("FSGetCwd", fs_GetCwd);
    Core::syscallHandler.registerSyscall("FSChangeDir", fs_ChangeDir);
    Core::syscallHandler.registerSyscall("FSOpenDir", fs_OpenDir);
    Core::syscallHandler.registerSyscall("FSReadDir", fs_ReadDir);
    Core::syscallHandler.registerSyscall("FSCloseDir", fs_CloseDir);
    Core::syscallHandler.registerSyscall("FSWriteFile", fs_WriteFile);
    Core::syscallHandler.registerSyscall("FSGetMountSource", fs_GetMountSource);
    Core::syscallHandler.registerSyscall("FSMount", fs_Mount);
    Core::syscallHandler.registerSyscall("FSUnmount", fs_noop_ok);
    Core::syscallHandler.registerSyscall("FSSetCmdPriority", fs_noop_ok);
    Core::syscallHandler.registerSyscall("FSFlushQuota", fs_noop_ok);
    Core::syscallHandler.registerSyscall("SAVEOpenDir", save_OpenDir);
    Core::syscallHandler.registerSyscall("SAVEOpenFile", save_OpenFile);
    Core::syscallHandler.registerSyscall("SAVEGetStat", save_GetStat);
    Core::syscallHandler.registerSyscall("SAVERemove", save_Remove);
    Core::syscallHandler.registerSyscall("SAVEGetFreeSpaceSize", save_GetFreeSpaceSize);
    Core::syscallHandler.registerSyscall("SAVEInitSaveDir", save_InitSaveDir);
    Core::syscallHandler.registerSyscall("SAVEFlushQuota", fs_noop_ok);
    Core::syscallHandler.registerSyscall("SAVEGetSharedDataTitlePath", save_GetSharedDataTitlePath);
    Core::syscallHandler.registerSyscall("SAVEGetSharedSaveDataPath", save_GetSharedSaveDataPath);
}
