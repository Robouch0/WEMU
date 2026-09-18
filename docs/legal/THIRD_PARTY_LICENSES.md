# Third-Party Licenses

WEMU is licensed under the MIT License. This license applies to WEMU's own source code and does not replace the licenses of third-party libraries used by the project.

The following libraries are used or installed by the WEMU build environment.

| Component                      | Purpose                            | License                                           | Notes                                                                                                                                          |
| ------------------------------ | ---------------------------------- | ------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| Qt 6                           | GUI and QML interface              | LGPL-3.0 / GPL alternatives depending on module   | WEMU currently links Qt Core, Gui, Quick and QuickControls2. New Qt modules must be checked individually because some Qt modules are GPL-only. |
| SDL2                           | Controller and input support       | zlib License                                      | Permissive and compatible with WEMU's MIT licensing.                                                                                           |
| zlib                           | RPX/ELF compressed section support | zlib License                                      | Permissive and compatible with WEMU's MIT licensing.                                                                                           |
| Vulkan-Headers / Vulkan Loader | Vulkan API and loader              | Apache-2.0 and/or MIT depending on component/file | Preserve applicable upstream notices when redistributing these components.                                                                     |
| GLFW                           | Windowing / Vulkan renderer        | zlib/libpng License                               | Permissive and compatible with WEMU's MIT licensing.                                                                                           |
| GLM                            | Graphics mathematics               | MIT / Modified MIT                                | Permissive and compatible with WEMU's MIT licensing.                                                                                           |
| GoogleTest / GoogleMock        | Unit testing                       | BSD-3-Clause                                      | Used for development and tests. Preserve the required copyright/license notice when redistributing it.                                         |

## Qt

WEMU currently uses the following Qt modules:

`Qt6::Core`
`Qt6::Gui`
`Qt6::Quick`
`Qt6::QuickControls2`

These modules are available under open-source licensing options including the GNU Lesser General Public License version 3.

Not every Qt module has the same open-source licensing terms. Some Qt modules are available to open-source users only under the GNU General Public License. Consequently, the license of any new Qt module must be reviewed before it is added to WEMU.

When distributing binaries that bundle Qt libraries, the applicable LGPL requirements and Qt third-party notices must also be respected.

Qt licensing information:
https://doc.qt.io/qt-6/licensing.html

Qt third-party licensing information:
https://doc.qt.io/qt-6/licenses-used-in-qt.html

## Distribution

Most dependencies used by the current Linux build are installed using the operating system's package manager rather than being copied into the WEMU source repository.

If WEMU begins distributing prebuilt packages, AppImages, installers, bundled shared libraries, or other binary distributions, the exact contents of those packages must be reviewed and the required third-party license texts and notices must be distributed with them.

## Ownership

All third-party libraries remain the property of their respective copyright holders.

Their inclusion or use by WEMU does not place those libraries under the WEMU MIT License.

This document is intended as an inventory of third-party licensing obligations. The authoritative license terms are the license files distributed by each upstream project.
