# SoundTouch

WEMU fetches SoundTouch **2.4.0** from its upstream release archive through
[`cmake/SoundTouch.cmake`](../cmake/SoundTouch.cmake). The archive SHA-256 is
`b54ca9724afcf0b8c5326a0afb1b1676f2b892ee01c57d5312232ce4bc27a077`.

SoundTouch is Copyright Olli Parviainen, distributed under the GNU Lesser
General Public License, version 2.1 or later. Its source headers retain the
upstream notices; the full license is in `COPYING.TXT` in the downloaded source.
No upstream source files are modified by WEMU.

- Project and documentation: https://www.surina.net/soundtouch/
- Source: https://codeberg.org/soundtouch/soundtouch
- Pinned archive: https://codeberg.org/soundtouch/soundtouch/archive/2.4.0.tar.gz

The library performs pitch-preserving tempo adjustment on the audio producer
thread. CMake builds the floating-point library without its command-line tool,
DLL wrapper, or OpenMP. Build sources are cached under `_deps/wemu_soundtouch-src`;
no separately installed SoundTouch package is needed.
