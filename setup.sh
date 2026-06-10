#!/usr/bin/env bash
# ==============================================================================
# WEMU — setup.sh
# ------------------------------------------------------------------------------
# One-shot installer & builder for WEMU on Linux.
#
# What this script does:
#   1. Detects your distribution (Ubuntu, Debian, Fedora, Arch).
#   2. Installs every system dependency the project needs to build AND run:
#        - Compilers + CMake + Ninja + ccache
#        - Qt6 (base + declarative + multimedia) + QML runtime modules
#        - Vulkan loader + headers + glslang
#        - GLFW3, SDL2, zlib
#        - GoogleTest (build-time, fetched by CMake)
#   3. Configures and builds:
#        - core/   (PowerPC interpreter, ELF loader, tests)
#        - gui/    (Qt6/QML launcher)
#        - vulkan/ (standalone Vulkan/GLFW renderer)
#   4. Optionally runs the unit tests (--test).
#   5. Prints the full elapsed time. Targets < 7 minutes on a 4-core VM.
#
# Usage:
#   ./setup.sh                         # install deps + build everything
#   ./setup.sh --no-build              # install deps only
#   ./setup.sh --test                  # build + run tests
#   ./setup.sh --jobs 8                # override parallel jobs
#   ./setup.sh --build-type Release    # default is Debug (matches README)
#   ./setup.sh --clean                 # wipe build dirs before configuring
#   ./setup.sh -h | --help
# ==============================================================================

set -euo pipefail

# ----- defaults ---------------------------------------------------------------
BUILD_TYPE="Debug"
JOBS="$(nproc 2>/dev/null || echo 4)"
DO_BUILD=1
DO_TEST=0
DO_CLEAN=0

# ----- arg parsing ------------------------------------------------------------
while [[ $# -gt 0 ]]; do
  case "$1" in
    --no-build)   DO_BUILD=0; shift ;;
    --test)       DO_TEST=1;  shift ;;
    --clean)      DO_CLEAN=1; shift ;;
    --jobs)       JOBS="$2"; shift 2 ;;
    --build-type) BUILD_TYPE="$2"; shift 2 ;;
    -h|--help)
      sed -n '2,/^# ===/{ /^# ===/q; s/^# \{0,1\}//; p; }' "$0"
      exit 0 ;;
    *) echo "Unknown argument: $1" >&2; exit 1 ;;
  esac
done

# ----- pretty logging ---------------------------------------------------------
log()   { printf '\e[1;34m[%s]\e[0m %s\n' "$(date +%H:%M:%S)" "$*"; }
warn()  { printf '\e[1;33m[%s][WARN]\e[0m %s\n' "$(date +%H:%M:%S)" "$*"; }
die()   { printf '\e[1;31m[ERROR]\e[0m %s\n' "$*" >&2; exit 1; }

START_TS=$(date +%s)
trap 'echo; printf "\e[1;36mTotal elapsed: %ss\e[0m\n" "$(( $(date +%s) - START_TS ))"' EXIT

# ----- sanity checks ----------------------------------------------------------
[[ "$EUID" -eq 0 ]] && die "Don't run this as root. The script will sudo where needed."
command -v sudo >/dev/null || die "sudo is required but not installed."

# We must be in the repo root (where the top CMakeLists.txt lives, with core/ + gui/).
if [[ ! -f CMakeLists.txt ]] || [[ ! -d core ]] || [[ ! -d gui ]]; then
  die "Run this script from the WEMU repo root (where CMakeLists.txt + core/ + gui/ are)."
fi

# ----- distro detection -------------------------------------------------------
if [[ ! -f /etc/os-release ]]; then
  die "Cannot detect distribution: /etc/os-release missing."
fi
. /etc/os-release
DISTRO_ID="${ID:-unknown}"
DISTRO_LIKE="${ID_LIKE:-}"

log "Detected: ${PRETTY_NAME:-$DISTRO_ID}"

# ----- package lists per distro ----------------------------------------------
# These cover EVERYTHING WEMU needs:
#   - build tools, cmake, ninja, ccache, git, pkg-config
#   - Qt6 dev (base + declarative + multimedia + tools + 5compat shim)
#   - QML *runtime* modules (the ones you discovered were missing!)
#   - Vulkan SDK pieces (headers, loader, validation, glslang)
#   - GLFW3, SDL2, zlib
#   - GoogleTest (CMake will FetchContent if needed; package speeds it up)

# Debian / Ubuntu shared list
declare -a APT_PKGS=(
  build-essential
  g++
  cmake
  ninja-build
  ccache
  git
  pkg-config

  # Core deps
  zlib1g-dev

  # Vulkan stack
  libvulkan-dev
  vulkan-tools
  vulkan-validationlayers
  spirv-tools
  glslang-tools
  glslc
  libglm-dev

  # Window/input/graphics deps
  libglfw3-dev
  libsdl2-dev
  libglm-dev

  # Qt6 dev
  qt6-base-dev
  qt6-declarative-dev
  qt6-multimedia-dev
  qt6-tools-dev
  qt6-tools-dev-tools
  libqt6core5compat6-dev

  # Qt6 QML runtime modules — required for ./appgui to start without
  # 'module "QtQuick" is not installed' errors.
  qml6-module-qtquick
  qml6-module-qtquick-controls
  qml6-module-qtquick-window
  qml6-module-qtquick-layouts
  qml6-module-qtquick-templates
  qml6-module-qtqml
  qml6-module-qtqml-workerscript
  qml6-module-qtquick-dialogs
  qml6-module-qtmultimedia
  qml6-module-qt-labs-platform

  # Tests
  libgtest-dev
  libgmock-dev
)

# Fedora list
declare -a DNF_PKGS=(
  gcc-c++ make cmake ninja-build ccache git pkgconf-pkg-config
  zlib-devel
  vulkan-loader-devel vulkan-tools vulkan-validation-layers glslang glslc
  glfw-devel SDL2-devel glm-devel
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtmultimedia-devel
  qt6-qttools-devel qt6-qt5compat-devel
  qt6-qtdeclarative qt6-qtquickcontrols2 qt6-qtmultimedia
  gtest-devel gmock-devel
)

# Arch list
declare -a PACMAN_PKGS=(
  base-devel cmake ninja ccache git pkgconf
  zlib
  vulkan-headers vulkan-icd-loader vulkan-tools vulkan-validation-layers glslang shaderc
  glfw sdl2 glm
  qt6-base qt6-declarative qt6-multimedia qt6-tools qt6-5compat
  gtest
)

# ----- install dependencies ---------------------------------------------------
install_deps_apt() {
  log "Updating apt index..."
  sudo apt-get update -qq

  log "Installing ${#APT_PKGS[@]} dependencies via apt..."
  sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends "${APT_PKGS[@]}"
}

install_deps_dnf() {
  log "Installing ${#DNF_PKGS[@]} dependencies via dnf..."
  sudo dnf install -y "${DNF_PKGS[@]}"
}

install_deps_pacman() {
  log "Synchronizing pacman index and installing ${#PACMAN_PKGS[@]} dependencies..."
  sudo pacman -Sy --noconfirm --needed "${PACMAN_PKGS[@]}"
}

case "$DISTRO_ID" in
  ubuntu|debian|linuxmint|pop|elementary|zorin)
    install_deps_apt ;;
  fedora|rhel|centos|rocky|almalinux)
    install_deps_dnf ;;
  arch|manjaro|endeavouros|cachyos)
    install_deps_pacman ;;
  *)
    # Fall back to ID_LIKE
    if [[ "$DISTRO_LIKE" == *debian* ]] || [[ "$DISTRO_LIKE" == *ubuntu* ]]; then
      install_deps_apt
    elif [[ "$DISTRO_LIKE" == *fedora* ]] || [[ "$DISTRO_LIKE" == *rhel* ]]; then
      install_deps_dnf
    elif [[ "$DISTRO_LIKE" == *arch* ]]; then
      install_deps_pacman
    else
      die "Unsupported distribution: $DISTRO_ID. Add a package list to setup.sh."
    fi ;;
esac

# ----- ccache: hooks compilers, makes rebuilds nearly instant -----------------
export PATH="/usr/lib/ccache:/usr/lib/ccache/bin:$PATH"
export CCACHE_DIR="${CCACHE_DIR:-$HOME/.ccache}"
ccache --max-size=2G >/dev/null 2>&1 || true

if [[ "$DO_BUILD" -eq 0 ]]; then
  log "Dependency install complete. Skipping build (--no-build)."
  exit 0
fi

# ----- build helper -----------------------------------------------------------
configure_and_build() {
  local source_dir="$1"
  local build_dir="$2"
  local label="$3"

  if [[ "$DO_CLEAN" -eq 1 ]] && [[ -d "$build_dir" ]]; then
    log "Cleaning $label build directory..."
    rm -rf "$build_dir"
  fi

  log "Configuring $label ($BUILD_TYPE, Ninja, ccache)..."
  cmake -S "$source_dir" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache \
    -DCMAKE_CXX_COMPILER_LAUNCHER=ccache

  log "Building $label with $JOBS parallel jobs..."
  cmake --build "$build_dir" -j "$JOBS"
}

# ----- 1. core + gui (root CMakeLists pulls both) -----------------------------
log "===== Stage 1/2 : core + gui ====="
configure_and_build "." "cmake-build-debug" "WEMU (core + gui)"

# ----- 2. vulkan (standalone tree) --------------------------------------------
if [[ -f vulkan/CMakeLists.txt ]]; then
  log "===== Stage 2/2 : vulkan renderer ====="
  configure_and_build "vulkan" "vulkan/build" "Vulkan renderer"
else
  warn "vulkan/CMakeLists.txt not found — skipping standalone Vulkan build."
fi

# ----- tests ------------------------------------------------------------------
if [[ "$DO_TEST" -eq 1 ]]; then
  log "===== Running unit tests ====="
  if [[ -f cmake-build-debug/CTestTestfile.cmake ]]; then
    ( cd cmake-build-debug && ctest --output-on-failure --parallel "$JOBS" )
  else
    warn "No CTest configuration found in cmake-build-debug — skipping ctest."
    warn "If tests live in core/build, run: cd core/build && ctest"
  fi
fi

# ----- summary ----------------------------------------------------------------
log "Build complete."
echo
echo "Artifacts:"
echo "  GUI binary:    $(pwd)/cmake-build-debug/gui/appgui"
[[ -d vulkan/build ]] && echo "  Vulkan binary: $(pwd)/vulkan/build/  (look for the executable)"
echo
echo "To run the GUI:    ./cmake-build-debug/gui/appgui"
echo "To re-run tests:   ./setup.sh --test"
echo "To rebuild clean:  ./setup.sh --clean"
