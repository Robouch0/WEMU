#include "gfx/PipelineManifest.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <format>
#include <fstream>

namespace Core::Gfx {
    namespace {
        constexpr char kMagic[8]{'W', 'E', 'M', 'U', 'P', 'M', 'F', '1'};
        constexpr std::uint32_t kVersion = 1;
        constexpr std::uint32_t kBuiltinVertex = 0xFFFFFFFFu;
        // magic, version, reserved, payload bytes, FNV-1a of the payload
        constexpr std::size_t kHeaderBytes = sizeof(kMagic) + 2 * sizeof(std::uint32_t) + 2 * sizeof(std::uint64_t);
        constexpr std::size_t kMaxProgramWords = 16384, kMaxResourceTypes = 32, kMaxStateWords = 1024;

        struct Fnv {
                std::uint64_t value = 0xCBF29CE484222325ull;
                void add(const void *data, std::size_t bytes)
                {
                    const auto *in = static_cast<const std::uint8_t *>(data);
                    for (std::size_t i = 0; i < bytes; ++i)
                        value = (value ^ in[i]) * 0x100000001B3ull;
                }
                void add(std::uint32_t word) { add(&word, sizeof(word)); }
        };
        std::uint64_t originHash(const Latte::ShaderOrigin &origin)
        {
            Fnv hash;
            hash.add(origin.words.data(), origin.words.size() * sizeof(std::uint32_t));
            for (const auto type: origin.resourceTypes)
                hash.add(std::uint32_t(type));
            hash.add(unsigned(origin.vertex) | (unsigned(origin.onlyColorZero) << 1));
            hash.add(origin.colorExport);
            hash.add(origin.positionRegister);
            hash.add(origin.bufferWindow);
            for (const auto window: origin.bufferWindows)
                hash.add(window);
            return hash.value;
        }
        bool sameOrigin(const std::shared_ptr<const Latte::ShaderOrigin> &a, const std::shared_ptr<const Latte::ShaderOrigin> &b)
        {
            return a == b || (a && b && *a == *b);
        }

        // Deduplicates origins by content, then their programs, while serializing.
        class Interner {
            public:
                std::uint32_t origin(const Latte::ShaderOrigin &value)
                {
                    const auto hash = originHash(value);
                    const auto [first, last] = m_originIndex.equal_range(hash);
                    for (auto it = first; it != last; ++it)
                        if (*origins[it->second].first == value)
                            return std::uint32_t(it->second);
                    const auto [found, inserted] = m_programIndex.try_emplace(value.words, std::uint32_t(programs.size()));
                    if (inserted)
                        programs.push_back(&value.words);
                    m_originIndex.emplace(hash, origins.size());
                    origins.emplace_back(&value, found->second);
                    return std::uint32_t(origins.size() - 1);
                }
                std::vector<const std::vector<std::uint32_t> *> programs;
                std::vector<std::pair<const Latte::ShaderOrigin *, std::uint32_t>> origins;

            private:
                std::unordered_multimap<std::uint64_t, std::size_t> m_originIndex;
                std::unordered_map<std::vector<std::uint32_t>, std::uint32_t, Latte::ProgramWordsHash, Latte::ProgramWordsEqual> m_programIndex;
        };

        struct Writer {
                std::vector<std::uint8_t> bytes;
                void word(std::uint32_t value)
                {
                    const auto offset = bytes.size();
                    bytes.resize(offset + sizeof(value));
                    std::memcpy(bytes.data() + offset, &value, sizeof(value));
                }
                void words(std::span<const std::uint32_t> values)
                {
                    word(std::uint32_t(values.size()));
                    for (const auto value: values)
                        word(value);
                }
        };

        struct Reader {
                std::span<const std::uint8_t> bytes;
                std::size_t offset{};
                bool ok{true};
                std::uint32_t word()
                {
                    if (!ok || bytes.size() - offset < sizeof(std::uint32_t)) {
                        ok = false;
                        return 0;
                    }
                    std::uint32_t value{};
                    std::memcpy(&value, bytes.data() + offset, sizeof(value));
                    offset += sizeof(value);
                    return value;
                }
                // Count of a following array, rejected beyond `limit` or the remaining bytes.
                std::uint32_t count(std::size_t limit, std::size_t elementBytes)
                {
                    const auto value = word();
                    if (value > limit || std::uint64_t(value) * elementBytes > bytes.size() - offset)
                        ok = false;
                    return ok ? value : 0;
                }
        };
    } // namespace

    bool PipelineRecipe::operator==(const PipelineRecipe &other) const
    {
        return fragmentVariant == other.fragmentVariant && state == other.state && sameOrigin(vertex, other.vertex) &&
               sameOrigin(fragment, other.fragment);
    }

    std::string PipelineManifest::defaultDirectory()
    {
        if (const char *configured = std::getenv("WEMU_PIPELINE_MANIFEST_DIR"))
            return configured;
        const auto root = Utils::PrivateDirectory::cacheRoot();
        return root.empty() ? root : root + "/manifests";
    }

    std::string PipelineManifest::filenameFor(const std::filesystem::path &executable)
    {
        std::ifstream file(executable, std::ios::binary);
        if (!file)
            return {};
        Fnv hash;
        std::array<char, 1 << 16> buffer{};
        while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0)
            hash.add(buffer.data(), std::size_t(file.gcount()));
        if (file.bad())
            return {};
        std::string stem;
        for (const char c: executable.stem().string())
            stem += (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ? c : '_';
        return std::format("{}-{:016x}.manifest", stem.substr(0, 64), hash.value);
    }

    PipelineManifest::PipelineManifest(const std::string &directory, std::string filename) : m_directory(directory), m_filename(std::move(filename))
    {
    }

    PipelineManifest::~PipelineManifest() { finish(); }

    std::vector<PipelineRecipe> PipelineManifest::load()
    {
        if (!enabled())
            return {};
        const auto file = m_directory.read(m_filename, maxFileBytes);
        if (!file)
            return {};
        auto recipes = parse(*file);
        if (!recipes)
            return {};
        for (auto &recipe: *recipes)
            add(recipe);
        m_saved = m_recipes.size();
        return std::move(*recipes);
    }

    std::uint64_t PipelineManifest::fingerprint(const PipelineRecipe &recipe) const
    {
        Fnv hash;
        hash.add(recipe.vertex ? std::uint32_t(originHash(*recipe.vertex)) : kBuiltinVertex);
        hash.add(std::uint32_t(originHash(*recipe.fragment)));
        hash.add(recipe.fragmentVariant);
        hash.add(recipe.state.data(), recipe.state.size() * sizeof(std::uint32_t));
        return hash.value;
    }

    bool PipelineManifest::add(PipelineRecipe recipe)
    {
        if (!recipe.fragment || recipe.fragment->vertex || (recipe.vertex && !recipe.vertex->vertex) || recipe.state.size() > kMaxStateWords ||
            m_recipes.size() >= maxRecipes)
            return false;
        const auto hash = fingerprint(recipe);
        const auto [first, last] = m_index.equal_range(hash);
        for (auto it = first; it != last; ++it)
            if (m_recipes[it->second] == recipe)
                return false;
        m_index.emplace(hash, m_recipes.size());
        m_recipes.push_back(std::move(recipe));
        return true;
    }

    void PipelineManifest::saveInBackground(std::chrono::steady_clock::duration interval)
    {
        using namespace std::chrono_literals;
        if (!enabled() || m_recipes.size() == m_saved)
            return;
        const auto now = std::chrono::steady_clock::now();
        if (now - m_lastSave < interval || (m_save.valid() && m_save.wait_for(0s) != std::future_status::ready))
            return;
        m_saved = m_recipes.size();
        m_lastSave = now;
        m_save = std::async(std::launch::async, [this, snapshot = m_recipes] { return m_directory.write(m_filename, serialize(snapshot)); });
    }

    bool PipelineManifest::finish()
    {
        try {
            const bool lastSaved = !m_save.valid() || m_save.get();
            if (!enabled())
                return false;
            if (lastSaved && m_saved == m_recipes.size())
                return true;
            m_saved = m_recipes.size();
            return m_directory.write(m_filename, serialize(m_recipes));
        } catch (...) {
            return false;
        }
    }

    std::vector<std::uint8_t> PipelineManifest::serialize(std::span<const PipelineRecipe> recipes)
    {
        Interner interner;
        std::vector<std::array<std::uint32_t, 3>> indices;
        indices.reserve(recipes.size());
        for (const auto &recipe: recipes)
            indices.push_back(
                    {recipe.vertex ? interner.origin(*recipe.vertex) : kBuiltinVertex, interner.origin(*recipe.fragment), recipe.fragmentVariant});
        Writer payload;
        payload.word(std::uint32_t(interner.programs.size()));
        for (const auto *program: interner.programs)
            payload.words(*program);
        payload.word(std::uint32_t(interner.origins.size()));
        for (const auto &[origin, program]: interner.origins) {
            payload.word(program);
            payload.word(unsigned(origin->vertex) | (unsigned(origin->onlyColorZero) << 1));
            payload.word(origin->colorExport);
            payload.word(origin->positionRegister);
            payload.word(origin->bufferWindow);
            for (const auto window: origin->bufferWindows)
                payload.word(window);
            payload.word(std::uint32_t(origin->resourceTypes.size()));
            for (const auto type: origin->resourceTypes)
                payload.word(std::uint32_t(type));
        }
        payload.word(std::uint32_t(recipes.size()));
        for (std::size_t i = 0; i < recipes.size(); ++i) {
            for (const auto value: indices[i])
                payload.word(value);
            payload.words(recipes[i].state);
        }
        Fnv hash;
        hash.add(payload.bytes.data(), payload.bytes.size());
        std::vector<std::uint8_t> file(kHeaderBytes);
        const std::uint32_t version = kVersion, reserved = 0;
        const std::uint64_t bytes = payload.bytes.size();
        auto *out = file.data();
        for (const auto &[data, size]: std::initializer_list<std::pair<const void *, std::size_t>>{
                     {kMagic, sizeof(kMagic)}, {&version, 4}, {&reserved, 4}, {&bytes, 8}, {&hash.value, 8}}) {
            std::memcpy(out, data, size);
            out += size;
        }
        file.insert(file.end(), payload.bytes.begin(), payload.bytes.end());
        return file;
    }

    std::optional<std::vector<PipelineRecipe>> PipelineManifest::parse(std::span<const std::uint8_t> bytes)
    {
        if (bytes.size() < kHeaderBytes || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0)
            return std::nullopt;
        std::uint32_t version{}, reserved{};
        std::uint64_t payloadBytes{}, expectedHash{};
        std::memcpy(&version, bytes.data() + sizeof(kMagic), sizeof(version));
        std::memcpy(&reserved, bytes.data() + sizeof(kMagic) + 4, sizeof(reserved));
        std::memcpy(&payloadBytes, bytes.data() + sizeof(kMagic) + 8, sizeof(payloadBytes));
        std::memcpy(&expectedHash, bytes.data() + sizeof(kMagic) + 16, sizeof(expectedHash));
        const auto payload = bytes.subspan(kHeaderBytes);
        Fnv hash;
        hash.add(payload.data(), payload.size());
        if (version != kVersion || reserved || payloadBytes != payload.size() || hash.value != expectedHash)
            return std::nullopt;

        Reader in{payload};
        std::vector<std::vector<std::uint32_t>> programs(in.count(maxRecipes * 2, 4));
        for (auto &program: programs) {
            program.resize(in.count(kMaxProgramWords, 4));
            for (auto &word: program)
                word = in.word();
        }
        std::vector<std::shared_ptr<const Latte::ShaderOrigin>> origins(in.count(maxRecipes * 2, std::size_t{22} * 4));
        for (auto &slot: origins) {
            Latte::ShaderOrigin origin;
            const auto program = in.word();
            const auto flags = in.word();
            origin.vertex = flags & 1;
            origin.onlyColorZero = flags & 2;
            origin.colorExport = in.word();
            origin.positionRegister = in.word();
            origin.bufferWindow = in.word();
            for (auto &window: origin.bufferWindows)
                window = in.word();
            origin.resourceTypes.resize(in.count(kMaxResourceTypes, 4));
            for (auto &type: origin.resourceTypes) {
                const auto value = in.word();
                if (value > std::uint32_t(Latte::TextureType::Cube))
                    in.ok = false;
                else
                    type = Latte::TextureType(value);
            }
            if (!in.ok || program >= programs.size() || flags > 3)
                return std::nullopt;
            origin.words = programs[program];
            slot = std::make_shared<const Latte::ShaderOrigin>(std::move(origin));
        }
        std::vector<PipelineRecipe> recipes(in.count(maxRecipes, 16));
        for (auto &recipe: recipes) {
            const auto vertex = in.word();
            const auto fragment = in.word();
            recipe.fragmentVariant = in.word();
            recipe.state.resize(in.count(kMaxStateWords, 4));
            for (auto &word: recipe.state)
                word = in.word();
            if (!in.ok || fragment >= origins.size() || origins[fragment]->vertex ||
                (vertex != kBuiltinVertex && (vertex >= origins.size() || !origins[vertex]->vertex)))
                return std::nullopt;
            recipe.fragment = origins[fragment];
            if (vertex != kBuiltinVertex)
                recipe.vertex = origins[vertex];
        }
        if (!in.ok || in.offset != payload.size())
            return std::nullopt;
        return recipes;
    }
} // namespace Core::Gfx
