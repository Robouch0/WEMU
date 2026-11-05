#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace Core {
    // Cafe language IDs and MCP region flags. Configuration is captured once
    // per Interpreter; changing launcher settings affects the next session.
    struct SystemConfig {
            std::uint32_t language{1}; // English
            std::uint8_t platformRegion{4}, titleRegion{4}; // Europe
            static constexpr std::array<std::string_view, 12> languages{"ja", "en", "fr", "de", "it", "es", "zh", "ko", "nl", "pt", "ru", "tw"};
            static constexpr bool validLanguage(unsigned value) { return value < languages.size(); }
            static constexpr bool validRegion(unsigned value)
            {
                return value == 1 || value == 2 || value == 4 || value == 16 || value == 32 || value == 64;
            }
            static unsigned chooseTitleRegion(unsigned mask)
            {
                for (unsigned region: {4u, 2u, 1u, 16u, 32u, 64u})
                    if (mask & region)
                        return region;
                return 4;
            }
            static SystemConfig fromOptions(std::string_view language, std::string_view region, std::string_view title)
            {
                SystemConfig result;
                const auto number = [](std::string_view text, unsigned fallback) {
                    if (text.empty())
                        return fallback;
                    unsigned value{};
                    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
                    return parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() ? value : fallback;
                };
                unsigned languageId = number(language, result.language);
                for (unsigned i = 0; i < languages.size(); ++i)
                    if (language == languages[i])
                        languageId = i;
                if (validLanguage(languageId))
                    result.language = languageId;
                const auto titleId = number(title, result.titleRegion);
                if (validRegion(titleId))
                    result.titleRegion = std::uint8_t(titleId);
                const auto regionId = number(region, result.titleRegion);
                result.platformRegion = std::uint8_t(validRegion(regionId) ? regionId : result.titleRegion);
                return result;
            }
            static SystemConfig fromEnvironment()
            {
                const auto option = [](const char *name) -> std::string_view {
                    const auto *value = std::getenv(name);
                    return value ? std::string_view(value) : std::string_view{};
                };
                return fromOptions(option("WEMU_SYSTEM_LANGUAGE"), option("WEMU_SYSTEM_REGION"), option("WEMU_TITLE_REGION"));
            }
    };
} // namespace Core
