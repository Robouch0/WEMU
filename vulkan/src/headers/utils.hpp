//
// Created by nicolas on 11/7/25.
//

#ifndef WEMU_UTILS_H
#define WEMU_UTILS_H
#include <stdexcept>
#include <vector>
#include <string>
#include <fstream>

class Utils {
public:
    static std::vector<char> readFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            throw std::runtime_error("failed to open file!");
        }

        const size_t fileSize = (size_t) file.tellg();
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();

        return buffer;
    }

};

#endif //WEMU_UTILS_H