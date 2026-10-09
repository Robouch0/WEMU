# Third-party dependency licenses

These dependencies are provided by the system; their source is not vendored in WEMU.

| Dependency introduced by this integration | Use | License and upstream reference |
|---|---|---|
| FFmpeg >= 6.0 (`libavcodec`, `libavutil`, `libswscale`) | H.264 decoding and video conversion | [LGPL-2.1-or-later; optional components can change the build's license](https://ffmpeg.org/legal.html) |
| SDL2_image | Game library image loading | [zlib](https://github.com/libsdl-org/SDL_image/blob/SDL2/LICENSE.txt) |
| LLVM 19.1 (optional, `WEMU_ENABLE_LLVM`) | PowerPC native compilation | [Apache-2.0 with LLVM exceptions](https://llvm.org/docs/DeveloperPolicy.html#license) |
| glslang (`glslangValidator`, optional runtime tool) | Compile lowered Latte shaders to SPIR-V | [BSD-3-Clause and component licenses](https://github.com/KhronosGroup/glslang/blob/main/LICENSE.txt) |
| SPIRV-Tools (`spirv-val`, optional runtime tool) | Validate generated SPIR-V | [Apache-2.0](https://github.com/KhronosGroup/SPIRV-Tools/blob/main/LICENSE) |
| Capstone (optional Python package) | RPX diagnostic disassembly | [BSD-3-Clause](https://github.com/capstone-engine/capstone/blob/master/LICENSE.TXT) |

Permissively licensed dependencies and LGPL FFmpeg libraries can be used alongside
WEMU's MIT-licensed source subject to their respective terms. FFmpeg's license
depends on its build options: GPL-enabled builds are GPL-covered, and nonfree
builds are not redistributable. Binary distributions must retain the applicable
notices and satisfy the terms of the specific system libraries they distribute.

`Threads::Threads` selects the platform's threading library rather than vendoring
a separate dependency.
