#include "Loader.hpp"
#include <filesystem>
#include <fstream>

using namespace core::utils;

Loader::Loader(const std::string& filepath) : _filePath(filepath) {}

bool Loader::loadFile() {
    std::ifstream file(_filePath, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open: " << _filePath << "\n";
        return false;
    }

    std::vector<uint8_t> buffer((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());

    if (buffer.empty()) {
        std::cerr << "File is empty or unreadable\n";
        return false;
    }

    //  just load the entire file as a single segment for now
    return loadSegments(buffer);
}

bool Loader::loadSegments(const std::vector<uint8_t>& fileData) {
    _segments.clear();

    LoadedSegment seg;
    seg.vaddr = 0x10000000; // placeholder
    seg.data = fileData;

    _segments.push_back(std::move(seg));

    std::cout << "Loaded " << fileData.size() << " bytes at virtual address 0x"
              << std::hex << _segments.back().vaddr << std::dec << "\n";

    return true;
}

const std::vector<LoadedSegment>& Loader::getSegments() const {
    return _segments;
}
