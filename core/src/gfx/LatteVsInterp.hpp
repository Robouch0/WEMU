#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
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
//    direct addressing, zero offsets); pixel SAMPLE_L and SET_CUBEMAP_INDEX
//    use the detailed TextureFetch callback for caller-owned cube/mip sampling.
//    Direct vertex uniform-buffer VTX_FETCH is supported in TEX/VTX/VTX_TC clauses.
//    Other VTX operations, derivatives, discard, and simultaneous color exports are incomplete.
//    A caller may select one direct color export, including a register in a burst.
//  - Vertex fetch inputs: up to 32 vec4 values in R1..R32; pixel inputs preload R0..R31.
//  - ALU OP2: ADD MUL MUL_IEEE MAX MIN MOV NOP FRACT FLOOR TRUNC SIN COS DOT4 DOT4_IEEE RECIP_IEEE SQRT_IEEE
//    RECIPSQRT_IEEE, float/int conversions, integer shifts and MULLO_INT/MULLO_UINT; OP3: MULADD MULADD_IEEE,
//    CNDE/CNDGT/CNDGE and integer variants. neg/abs/omod/clamp honoured.
//  - Sources: GPR (0-127), static kcache banks (128-191), constant file
//    (256-511), literals (253), PV/PS (254/255), 0/1/1-int/0.5 inline constants
//    (248/249/250/252).
//  - Clause-local MOVA/MOVA_FLOOR/MOVA_INT address registers and AR-relative
//    constant-file reads. Relative GPR/kcache/loop operands remain unsupported.
//  - Groups: instructions until LAST=1 read pre-group register state; writes commit at group end;
//    PV/PS updated per group; literal dwords follow each group.
//
// Input dwords are already byte-corrected from little-endian guest shader storage.

namespace Core::Gfx {

    class LatteVsInterp {
        public:
            struct Output {
                    std::array<float, 4> pos{0.0f, 0.0f, 0.0f, 1.0f}; // EXPORT POS 60
                    std::array<std::array<float, 4>, 32> params{}; // EXPORT PARAM 0..31
                    std::array<bool, 32> paramValid{};
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
            struct TextureRequest {
                    unsigned resource{}, sampler{};
                    std::array<float, 4> coords{};
                    float cubeIndex{};
                    bool explicitLod{}; // SAMPLE_L takes its LOD from coords.w.
            };
            using TextureFetch = std::function<bool(const TextureRequest &, std::array<float, 4> &)>;

            using Program = Latte::Program;
            static std::shared_ptr<const Program> compile(const std::vector<std::uint32_t> &words);
            // Proves an unsupported texture operation is reached before any
            // execute-mask change or branch. No pixel can return a shader result.
            static bool rejectsEveryPixel(const Program &program);
            static Output runPixel(const Program &program, std::span<const std::array<float, 4>> inputs, const float *consts,
                                   std::uint32_t constCount, const TextureSample &textureSample, const KcacheFetch &kcache = {},
                                   const TextureGather &textureGather = {}, const TextureFetch &textureFetch = {}, unsigned colorExport = 0);

            // Reuse immutable decoding across all vertices of a draw. Runtime
            // attributes, constants and sampler callbacks remain call-local.
            static Output runVertex(const Program &program, std::span<const std::array<float, 4>> attribs, const float *consts,
                                    std::uint32_t constCount, const KcacheFetch &kcache = {}, const TextureSample &textureSample = {});

            // program: bytecode dwords (already byte-corrected). attribs: R1.. preloaded attribute
            // values. consts: the uniform register file (uniform-register mode / kcache-off reads).
            static Output run(const std::vector<std::uint32_t> &program, std::span<const std::array<float, 4>> attribs, const float *consts,
                              std::uint32_t constCount, const KcacheFetch &kcache = {}, const TextureSample &textureSample = {});

            // Force the ALU/export trace on for the next run() regardless of WEMU_VS_DEBUG, so a
            // single draw can be inspected without dumping every vertex in the frame.
            static void setDebugOnce(bool on);

        private:
            static Output execute(const Program &program, std::span<const std::array<float, 4>> inputs, const float *consts, std::uint32_t constCount,
                                  const KcacheFetch &kcache, const TextureSample &textureSample, bool pixelStage,
                                  const TextureGather &textureGather = {}, const TextureFetch &textureFetch = {}, unsigned colorExport = 0);
    };

} // namespace Core::Gfx
