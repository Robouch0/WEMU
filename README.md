# Wemu - Wii U Emulator

A high-performance, open-source Wii U emulator for Windows, macOS, and Linux.

## Features

- **Game Compatibility**: Play your favorite Wii U games with high accuracy
- **Enhanced Graphics**: Upscaling, anti-aliasing, and improved visual quality
- **Multiple Input Options**: Support for GamePad, Pro Controller, and keyboard/mouse
- **Save States**: Save and load game progress at any point
- **Shader Cache**: Reduced stuttering with precompiled shader cache
- **Audio Enhancement**: High-quality audio processing and surround sound support
- **Modding Support**: Load game mods and custom content

## System Requirements

### Minimum Requirements
- **OS**: Windows 10 64-bit / macOS 10.15 / Ubuntu 18.04+
- **CPU**: Intel Core i5-4430 / AMD FX-6300
- **GPU**: NVIDIA GTX 750 Ti / AMD Radeon R7 260X
- **RAM**: 4 GB
- **Storage**: 2 GB free space

### Recommended Requirements
- **OS**: Windows 11 64-bit / macOS 12+ / Ubuntu 20.04+
- **CPU**: Intel Core i7-8700K / AMD Ryzen 5 2600
- **GPU**: NVIDIA GTX 1060 6GB / AMD RX 580 8GB
- **RAM**: 8 GB
- **Storage**: 4 GB free space (SSD recommended)

## Installation

### Windows
1. Download the latest release from the [Releases](https://github.com/username/wemu/releases) page
2. Extract the ZIP file to your desired location
3. Run `Wemu.exe`

### macOS
1. Download the latest `.dmg` file from [Releases](https://github.com/username/wemu/releases)
2. Mount the DMG and drag Wemu to Applications
3. Launch from Applications folder

### Linux
```bash
# Ubuntu/Debian
sudo apt update
sudo apt install wemu

# Arch Linux
yay -S wemu

# Or build from source
git clone https://github.com/username/wemu.git
cd wemu
mkdir build && cd build
cmake ..
make -j$(nproc)
```

## Quick Start

1. **Install Wii U System Files**: Place your legally dumped Wii U system files in the `sys/` directory
2. **Add Games**: Copy your game files to the `games/` directory or use File → Add Game Directory
3. **Configure Input**: Go to Settings → Input to set up your controllers
4. **Start Playing**: Double-click a game in the game list to launch

## Configuration

### Graphics Settings
- **Renderer**: Vulkan (recommended) or OpenGL
- **Resolution**: Up to 4K upscaling
- **Anti-aliasing**: FXAA, SMAA, or MSAA
- **Anisotropic Filtering**: Up to 16x

### Audio Settings
- **Backend**: DirectSound, XAudio2, or ALSA
- **Latency**: Adjustable buffer size
- **Channels**: Stereo or 5.1 surround

### GamePad Settings
- **Screen Layout**: Single screen, dual screen, or picture-in-picture
- **Touch Input**: Mouse or touch screen support
- **Motion Controls**: Gyroscope and accelerometer emulation

## Game Compatibility

Check our [Compatibility Database](https://github.com/Robouch0/WEMU/wiki/Game-Compatibility-Database) for the latest game compatibility status.

**Compatibility Ratings:**
- 🟢 **Perfect**: Fully playable with no issues
- 🟡 **Playable**: Playable with minor issues
- 🟠 **Ingame**: Starts but has major issues
- 🔴 **Broken**: Doesn't boot or crashes immediately

## Legal Notice

This emulator requires legally obtained Wii U system files and game dumps. We do not provide copyrighted content.

**To legally use this emulator, you must:**
- Own a physical Wii U console
- Dump your own system files using homebrew tools
- Dump your own game discs

## Building from Source

### Prerequisites
- CMake 3.15+
- C++17 compatible compiler
- Git

### Dependencies
- OpenGL 4.5+ / Vulkan 1.1+
- SDL2
- libpng
- zlib
- OpenAL

### Build Instructions
```bash
git clone --recursive https://github.com/username/wemu.git
cd wemu
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## Contributing

We welcome contributions! Please read our [Contributing Guidelines](CONTRIBUTING.md) before submitting pull requests.

### Development Setup
1. Fork the repository
2. Create a feature branch: `git checkout -b feature-name`
3. Make your changes and test thoroughly
4. Submit a pull request with a detailed description

### Code Style
- Follow the existing code style
- Use meaningful variable names
- Comment complex algorithms
- Run tests before submitting

## Troubleshooting

### Common Issues

**Black Screen on Launch**
- Update your graphics drivers
- Try switching between Vulkan and OpenGL renderers
- Check that system files are properly installed

**Poor Performance**
- Enable shader cache
- Lower resolution and graphics settings
- Close unnecessary background applications
- Ensure you meet the minimum system requirements

**Controller Not Working**
- Check Settings → Input configuration
- Ensure your controller is properly connected
- Try different input backends

### Getting Help
- Check our [FAQ](https://github.com/username/wemu/wiki/FAQ)
- Search existing [Issues](https://github.com/username/wemu/issues)
- Join our [Discord Server](https://discord.gg/wemu)

## Changelog

See [CHANGELOG.md](CHANGELOG.md) for detailed version history.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

```
MIT License

Copyright (c) 2025 Wemu Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Acknowledgments

- Nintendo for creating the Wii U console
- The homebrew community for reverse engineering efforts
- All contributors and testers
- Special thanks to the Dolphin and Cemu teams for inspiration

## Links

- **Website**: https://wemu.vercel.app
- **Discord**: https://discord.gg/wemu
- **Compatibility Database**: https://github.com/Robouch0/WEMU/wiki/Game-Compatibility-Database

---

**Disclaimer**: This emulator is for educational and preservation purposes. Users are responsible for ensuring they comply with all applicable laws regarding game preservation and emulation in their jurisdiction.