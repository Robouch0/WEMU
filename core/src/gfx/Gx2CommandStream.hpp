#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>

// GX2 command capture.
//
// The first stage of GX2->Vulkan: instead of executing GPU work, the GX2 HLE records each draw /
// clear / state-change / present call into an ordered command stream. A later stage walks this
// stream and replays it onto the Vulkan renderer. Capturing first (and unit-testing it) lets us
// build and validate the translation incrementally, decoupled from the CPU-side boot.

namespace Core::Gfx {

    enum class Gx2Cmd : std::uint32_t {
        Init,
        SetContextState,
        SetColorBuffer,
        SetDepthBuffer,
        ClearColor,
        ClearDepthStencilEx,
        ClearBuffersEx,
        SetViewport,
        SetScissor,
        SetFetchShader,
        SetVertexShader,
        SetPixelShader,
        SetGeometryShader,
        SetAttribBuffer,
        SetVertexUniformBlock,
        SetPixelUniformBlock,
        SetPixelTexture,
        SetPixelSampler,
        SetBlendControl,
        SetColorControl,
        SetDepthStencilControl,
        DrawEx,
        DrawIndexedEx,
        CopyColorBufferToScanBuffer,
        SwapScanBuffers,
        SetVertexUniformReg,
        SetPixelUniformReg,
        SetBlendConstantColor,
        SetTargetChannelMasks,
        SetVertexTexture,
        SetVertexSampler,
        SetPixelSamplerBorderColor,
        SetVertexSamplerBorderColor,
        SetPolygonControl,
        SetRasterizerClipControl,
        SetPolygonOffset,
        // Library video rendering, ordered with guest GX2 commands. Payload:
        // 16 clip-transform floats, 16 UV-transform floats, then RGBA8 words.
        DrawImage,

    };

    // One captured GX2 call: its type plus a raw snapshot of the integer (r3..r10) and float
    // (f1..f8) argument registers. The replay stage interprets these per command type.
    // payload: for uniform uploads (SetVertexUniformReg/Block) the pointed-to data is snapshotted
    // AT CALL TIME — the game reuses stack/ring memory immediately, so reading the pointers at
    // replay (end of frame) yields garbage.
    // Payload words of a captured command. Copies share one buffer, so display lists splice
    // recorded commands into every frame without copying payloads; writers detach first.
    class Gx2Payload {
        public:
            Gx2Payload() = default;
            Gx2Payload(std::vector<std::uint32_t> words) :
                m_words(words.empty() ? nullptr : std::make_shared<std::vector<std::uint32_t>>(std::move(words)))
            {
            }
            Gx2Payload(std::initializer_list<std::uint32_t> words) : Gx2Payload(std::vector<std::uint32_t>(words)) {}

            [[nodiscard]] const std::vector<std::uint32_t> &words() const noexcept { return m_words ? *m_words : empty_(); }
            operator const std::vector<std::uint32_t> &() const noexcept { return words(); }
            [[nodiscard]] std::size_t size() const noexcept { return m_words ? m_words->size() : 0; }
            [[nodiscard]] bool empty() const noexcept { return !size(); }
            [[nodiscard]] const std::uint32_t *data() const noexcept { return words().data(); }
            [[nodiscard]] const std::uint32_t *begin() const noexcept { return data(); }
            [[nodiscard]] const std::uint32_t *end() const noexcept { return data() + size(); }
            [[nodiscard]] const std::uint32_t &operator[](std::size_t index) const noexcept { return (*m_words)[index]; }
            // Writable element access detaches like mutate(); hot loops should call mutate() once.
            [[nodiscard]] std::uint32_t &operator[](std::size_t index) { return mutate()[index]; }
            [[nodiscard]] std::uint32_t *data() { return mutate().data(); }
            bool operator==(const Gx2Payload &other) const noexcept { return words() == other.words(); }

            // A buffer of its own to write: shared words are copied first.
            std::vector<std::uint32_t> &mutate()
            {
                if (!m_words)
                    m_words = std::make_shared<std::vector<std::uint32_t>>();
                else if (m_words.use_count() > 1)
                    m_words = std::make_shared<std::vector<std::uint32_t>>(*m_words);
                return *m_words;
            }
            // `count` words of its own to overwrite completely: shared words are not copied.
            std::vector<std::uint32_t> &overwrite(std::size_t count)
            {
                if (!m_words || m_words.use_count() > 1)
                    m_words = std::make_shared<std::vector<std::uint32_t>>(count);
                else
                    m_words->resize(count);
                return *m_words;
            }
            void clear() noexcept { m_words.reset(); }
            void resize(std::size_t count) { mutate().resize(count); }
            void reserve(std::size_t count) { mutate().reserve(count); }
            void push_back(std::uint32_t word) { mutate().push_back(word); }
            template<typename Iterator>
            void assign(Iterator first, Iterator last)
            {
                *this = Gx2Payload(std::vector<std::uint32_t>(first, last));
            }

        private:
            static const std::vector<std::uint32_t> &empty_() noexcept
            {
                static const std::vector<std::uint32_t> none;
                return none;
            }
            std::shared_ptr<std::vector<std::uint32_t>> m_words;
    };

    struct Gx2Command {
            Gx2Cmd type{};
            std::array<std::uint32_t, 8> gpr{}; // r3..r10
            std::array<double, 8> fpr{}; // f1..f8
            Gx2Payload payload;
            std::uint32_t callerLr{0}; // guest LR at capture time; diagnostic metadata, not frame content
    };

    class Gx2CommandStream {
        public:
            void push(const Gx2Command &cmd) { m_commands.push_back(cmd); }
            void clear() noexcept { m_commands.clear(); }

            [[nodiscard]] std::size_t size() const noexcept { return m_commands.size(); }
            [[nodiscard]] bool empty() const noexcept { return m_commands.empty(); }
            [[nodiscard]] const std::vector<Gx2Command> &commands() const noexcept { return m_commands; }
            // For the pre-replay fix-up pass (uniform-block payloads are re-read at frame end).
            [[nodiscard]] std::vector<Gx2Command> &mutableCommands() noexcept { return m_commands; }

            // FNV-1a hash over the whole captured frame (command types + argument registers +
            // call-time/refreshed payloads). Two frames with equal hashes produce an identical
            // rasterised image, so the replayer can skip rebuilding the framebuffer and just
            // re-present the previous one. Covers everything snapshotted into the stream; the only
            // uncovered inputs are vertex/index/texture-image bytes read live at replay, which can
            // change independently. DrawImage owns its pixel bytes in the payload.
            [[nodiscard]] std::uint64_t contentHash() const noexcept
            {
                std::uint64_t h = 1469598103934665603ull; // FNV-1a offset basis
                const auto mix = [&h](std::uint64_t v) {
                    h ^= v;
                    h *= 1099511628211ull; // FNV prime
                };
                for (const auto &c: m_commands) {
                    mix(static_cast<std::uint64_t>(c.type));
                    for (const auto g: c.gpr)
                        mix(g);
                    for (const auto f: c.fpr)
                        mix(std::bit_cast<std::uint64_t>(f));
                    mix(c.payload.size());
                    for (const auto p: c.payload)
                        mix(p);
                }
                return h;
            }

            // Number of recorded draw calls — a quick health metric for a captured frame.
            [[nodiscard]] std::size_t drawCount() const noexcept
            {
                std::size_t n = 0;
                for (const auto &c: m_commands)
                    if (c.type == Gx2Cmd::DrawEx || c.type == Gx2Cmd::DrawIndexedEx || c.type == Gx2Cmd::DrawImage)
                        n++;
                return n;
            }

        private:
            std::vector<Gx2Command> m_commands;
    };

    // Process-wide capture buffer (one GPU command queue is emulated).
    Gx2CommandStream &gx2Stream();

} // namespace Core::Gfx
