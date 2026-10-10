#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "gfx/LatteProgram.hpp"

// CPU interpreter for a subset of Latte (R600-class) vertex and pixel shader bytecode.
// The legacy class name is retained; both stages share ALU decoding and execution.
//
// Scope (deliberately small — enough for GX2_SHADER_MODE_UNIFORM_REGISTER UI shaders):
//  - CF: NOP, CALL_FS (attributes are preloaded by the caller), ALU, EXPORT/EXPORT_DONE, RETURN,
//    END_OF_PROGRAM, limited forward JUMP/ELSE/POP. Normalized 2D SAMPLE_LZ is supported
//    in both stages and SAMPLE in the pixel stage, through a caller-provided sampler.
//    Pixel FETCH4 uses a separate caller-provided gather callback (normalized,
//    direct addressing, zero offsets); callers must enforce resource/LOD support.
//    VTX clauses, derivatives, LOD selection, discard, and multiple color exports are incomplete.
//  - ALU OP2: ADD MUL MUL_IEEE MAX MIN MOV NOP FRACT FLOOR DOT4 DOT4_IEEE RECIP_IEEE
//    RECIPSQRT_IEEE, float/int conversions and integer shifts; OP3: MULADD MULADD_IEEE,
//    CNDE/CNDGT/CNDGE and integer variants. neg/abs/omod/clamp honoured.
//  - Sources: GPR (0-127), kcache banks (128-191 -> treated as the constant file), constant file
//    (256-511), literals (253), PV/PS (254/255), 0/1/1-int/0.5 inline constants
//    (248/249/250/252).
//  - Groups: instructions until LAST=1 read pre-group register state; writes commit at group end;
//    PV/PS updated per group; literal dwords follow each group.
//
// Input dwords are already byte-corrected from little-endian guest shader storage.

namespace Core::Gfx {

    class LatteVsInterp {
        public:
            struct Output {
                    std::array<float, 4> pos{0.0f, 0.0f, 0.0f, 1.0f}; // EXPORT POS 60
                    std::array<std::array<float, 4>, 4> params{}; // EXPORT PARAM 0..3
                    std::array<bool, 4> paramValid{};
                    bool valid{false};
                    std::array<float, 4> color{}; // pixel EXPORT 0
                    bool colorValid{false};
                    float depth{}; // pixel EXPORT_Z (base 61), X component
                    bool depthValid{};
                    bool discarded{}; // All supported pixel exports were masked off.
            };

            // Fetches one float from uniform BLOCK `bank` at vec4 index / channel (kcache path,
            // GX2_SHADER_MODE_UNIFORM_BLOCK). May be empty when no blocks are bound.
            using KcacheFetch = std::function<float(std::uint32_t bank, std::uint32_t vec4Idx, std::uint32_t chan)>;
            using TextureSample = std::function<bool(std::uint32_t resource, std::uint32_t sampler, const std::array<float, 4> &coords,
                                                     std::array<float, 4> &rgba)>;
            // Four unfiltered mapped-red taps, not an ordinary RGBA sample.
            using TextureGather = TextureSample;

            using Program = Latte::Program;
            static std::shared_ptr<const Program> compile(const std::vector<std::uint32_t> &words);
            static Output runPixel(const Program &program, const std::array<std::array<float, 4>, 4> &inputs, const float *consts,
                                   std::uint32_t constCount, const TextureSample &textureSample, const KcacheFetch &kcache = {},
                                   const TextureGather &textureGather = {});

            // program: bytecode dwords (already byte-corrected). attribs: R1.. preloaded attribute
            // values. consts: the uniform register file (uniform-register mode / kcache-off reads).
            static Output run(const std::vector<std::uint32_t> &program, const std::array<std::array<float, 4>, 4> &attribs, const float *consts,
                              std::uint32_t constCount, const KcacheFetch &kcache = {}, const TextureSample &textureSample = {});

            // Force the ALU/export trace on for the next run() regardless of WEMU_VS_DEBUG, so a
            // single draw can be inspected without dumping every vertex in the frame.
            static void setDebugOnce(bool on);

        private:
            static Output execute(const Program &program, const std::array<std::array<float, 4>, 4> &inputs, const float *consts,
                                  std::uint32_t constCount, const KcacheFetch &kcache, const TextureSample &textureSample, bool pixelStage,
                                  const TextureGather &textureGather = {});
    };

} // namespace Core::Gfx
