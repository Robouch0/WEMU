#pragma once
#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <cstdint>

namespace core {
    namespace utils {

        struct LoadedSegment {
            uint32_t vaddr;
            std::vector<uint8_t> data;
            bool executable;
        };

        class Loader {
            public:
                explicit Loader(const std::string &filepath);

                bool loadFile();
                const std::vector<LoadedSegment>& getSegments() const;

            private:
                std::vector<LoadedSegment> _segments;
                std::string _filePath;

                bool loadSegments(const std::vector<uint8_t>& fileData);
        };

    } // namespace utils
} // namespace core
