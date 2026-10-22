#include "gfx/LatteSurfaceLayout.hpp"

#include <addrlib/addrinterface.h>
#include <algorithm>
#include <cstdlib>
#include <limits>

namespace Core::Gfx {
    namespace {
        struct AddressLibrary {
                ADDR_HANDLE handle{};
                AddressLibrary()
                {
                    ADDR_CREATE_INPUT input{};
                    ADDR_CREATE_OUTPUT output{};
                    input.size = sizeof(input);
                    output.size = sizeof(output);
                    input.chipEngine = CIASICIDGFXENGINE_R600;
                    input.chipFamily = 0x51;
                    input.chipRevision = 71;
                    input.regValue.gbAddrConfig = 0x44902;
                    input.createFlags.fillSizeFields = 1;
                    input.callbacks.allocSysMem = [](const ADDR_ALLOCSYSMEM_INPUT *request) { return std::malloc(request->sizeInBytes); };
                    input.callbacks.freeSysMem = [](const ADDR_FREESYSMEM_INPUT *request) {
                        std::free(request->pVirtAddr);
                        return ADDR_OK;
                    };
                    if (AddrCreate(&input, &output) == ADDR_OK)
                        handle = output.hLib;
                }
                ~AddressLibrary()
                {
                    if (handle)
                        AddrDestroy(handle);
                }
        };
    } // namespace

    std::optional<LatteHtileLayout> latteHtileLayout(unsigned pitch, unsigned height, unsigned slices)
    {
        if (!pitch || !height || !slices || pitch > 8192 || height > 8192 || slices > 2048)
            return {};
        thread_local const AddressLibrary library;
        if (!library.handle)
            return {};
        ADDR_COMPUTE_HTILE_INFO_INPUT input{};
        ADDR_COMPUTE_HTILE_INFO_OUTPUT output{};
        input.size = sizeof(input);
        output.size = sizeof(output);
        input.pitch = pitch;
        input.height = height;
        input.numSlices = slices;
        input.blockWidth = input.blockHeight = ADDR_HTILE_BLOCKSIZE_8;
        input.tileIndex = -1;
        if (AddrComputeHtileInfo(library.handle, &input, &output) != ADDR_OK || output.htileBytes > std::numeric_limits<std::uint32_t>::max())
            return {};
        return LatteHtileLayout{output.baseAlign, static_cast<std::uint32_t>(output.htileBytes)};
    }

    std::optional<LatteSurfaceLayout> latteSurfaceLayout(unsigned width, unsigned height, unsigned depth, unsigned format, unsigned dimension,
                                                         unsigned tileMode, unsigned level, unsigned use)
    {
        if (!width || !height || !depth || width > 8192 || height > 8192 || depth > 2048 || level > 13 ||
            (dimension != 1 && dimension != 3 && dimension != 5) || (dimension == 3 && (width != height || depth % 6)) ||
            (tileMode != 0 && tileMode != 1 && tileMode != 2 && tileMode != 4 && tileMode != 16))
            return {};
        const auto hwFormat = format & 63u;
        const auto bytes = hwFormat == 1                                                                                     ? 1u
                           : hwFormat == 5 || hwFormat == 7 || hwFormat == 6 || hwFormat == 8                                ? 2u
                           : hwFormat == 0x31 || hwFormat == 0x34                                                            ? 8u
                           : hwFormat >= 0x32 && hwFormat <= 0x35                                                            ? 16u
                           : hwFormat == 0x22 || hwFormat == 0x23                                                            ? 16u
                           : hwFormat == 0x1F || hwFormat == 0x20                                                            ? 8u
                           : hwFormat == 0xE || hwFormat == 0x11 || hwFormat == 0x16 || hwFormat == 0x19 || hwFormat == 0x1A ? 4u
                                                                                                                             : 0u;
        if (!bytes)
            return {};
        width = std::max(1u, width >> level);
        height = std::max(1u, height >> level);
        if (tileMode == 16) {
            if (hwFormat >= 0x31 && hwFormat <= 0x35) {
                width = (width + 3) / 4;
                height = (height + 3) / 4;
            }
            const auto size = std::uint64_t(width) * height * depth * bytes;
            if (size > std::numeric_limits<std::uint32_t>::max())
                return {};
            return LatteSurfaceLayout{16, width, height, depth, 1, static_cast<std::uint32_t>(size)};
        }
        // AddrLib owns immutable chip configuration. One instance per host thread
        // also keeps future worker-side queries independent of other renderers.
        thread_local const AddressLibrary library;
        if (!library.handle)
            return {};
        ADDR_COMPUTE_SURFACE_INFO_INPUT input{};
        ADDR_COMPUTE_SURFACE_INFO_OUTPUT output{};
        input.size = sizeof(input);
        output.size = sizeof(output);
        input.tileMode = static_cast<AddrTileMode>(tileMode ? tileMode : 4);
        input.format = static_cast<AddrFormat>(hwFormat);
        input.bpp = bytes * 8;
        input.width = width;
        input.height = height;
        input.numSlices = depth;
        input.numSamples = input.numFrags = 1;
        input.mipLevel = level;
        input.flags.cube = dimension == 3;
        input.flags.color = (use & 2u) != 0;
        input.flags.depth = (use & 4u) != 0;
        input.flags.texture = (use & 1u) != 0;
        input.flags.inputBaseMap = level == 0;
        input.tileIndex = -1;
        if (AddrComputeSurfaceInfo(library.handle, &input, &output) != ADDR_OK || output.surfSize > std::numeric_limits<std::uint32_t>::max())
            return {};
        return LatteSurfaceLayout{
                unsigned(output.tileMode), output.pitch, output.height, output.depth, output.baseAlign, static_cast<std::uint32_t>(output.surfSize)};
    }
} // namespace Core::Gfx
