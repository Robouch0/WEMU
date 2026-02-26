Package: glfw3:x64-linux@3.4#1

**Host Environment**

- Host: x64-linux
- Compiler: GNU 13.3.0
- CMake Version: 3.31.10
-    vcpkg-tool version: 2025-12-16-44bb3ce006467fc13ba37ca099f64077b8bbf84d
    vcpkg-scripts version: b2f068faf4 2026-02-25 (6 hours ago)

**To Reproduce**

`vcpkg install `

**Failure logs**

```
-- Using cached glfw-glfw-3.4.tar.gz
-- Cleaning sources at /home/nicolas/.local/share/vcpkg/buildtrees/glfw3/src/3.4-2448ff4533.clean. Use --editable to skip cleaning for the packages you specify.
-- Extracting source /home/nicolas/.local/share/vcpkg/downloads/glfw-glfw-3.4.tar.gz
-- Using source at /home/nicolas/.local/share/vcpkg/buildtrees/glfw3/src/3.4-2448ff4533.clean
GLFW3 currently requires the following libraries from the system package manager:
    xinerama
    xcursor
    xorg
    libglu1-mesa
    pkg-config

These can be installed on Ubuntu systems via sudo apt install libxinerama-dev libxcursor-dev xorg-dev libglu1-mesa-dev pkg-config

Alternatively, when targeting the Wayland display server, use the packages listed in the GLFW documentation here:

https://www.glfw.org/docs/3.3/compile.html#compile_deps_wayland
-- Configuring x64-linux
CMake Error at scripts/cmake/vcpkg_execute_required_process.cmake:127 (message):
    Command failed: /home/nicolas/.local/share/vcpkg/downloads/tools/ninja-1.13.2-linux/ninja -v
    Working Directory: /home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel/vcpkg-parallel-configure
    Error code: 1
    See logs for more information:
      /home/nicolas/.local/share/vcpkg/buildtrees/glfw3/config-x64-linux-dbg-CMakeCache.txt.log
      /home/nicolas/.local/share/vcpkg/buildtrees/glfw3/config-x64-linux-rel-CMakeCache.txt.log
      /home/nicolas/.local/share/vcpkg/buildtrees/glfw3/config-x64-linux-dbg-CMakeConfigureLog.yaml.log
      /home/nicolas/.local/share/vcpkg/buildtrees/glfw3/config-x64-linux-rel-CMakeConfigureLog.yaml.log
      /home/nicolas/.local/share/vcpkg/buildtrees/glfw3/config-x64-linux-out.log

Call Stack (most recent call first):
  /home/nicolas/WEMU/vcpkg_installed/x64-linux/share/vcpkg-cmake/vcpkg_cmake_configure.cmake:269 (vcpkg_execute_required_process)
  ports/glfw3/portfile.cmake:38 (vcpkg_cmake_configure)
  scripts/ports.cmake:206 (include)



```

<details><summary>/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/config-x64-linux-out.log</summary>

```
[1/2] "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/bin/cmake" -E chdir "../../x64-linux-dbg" "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/bin/cmake" "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/src/3.4-2448ff4533.clean" "-G" "Ninja" "-DCMAKE_BUILD_TYPE=Debug" "-DCMAKE_INSTALL_PREFIX=/home/nicolas/.local/share/vcpkg/packages/glfw3_x64-linux/debug" "-DFETCHCONTENT_FULLY_DISCONNECTED=ON" "-DGLFW_BUILD_EXAMPLES=OFF" "-DGLFW_BUILD_TESTS=OFF" "-DGLFW_BUILD_DOCS=OFF" "-DGLFW_BUILD_WAYLAND=OFF" "-DCMAKE_MAKE_PROGRAM=/home/nicolas/.local/share/vcpkg/downloads/tools/ninja-1.13.2-linux/ninja" "-DCMAKE_SYSTEM_NAME=Linux" "-DBUILD_SHARED_LIBS=OFF" "-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=/home/nicolas/.local/share/vcpkg/scripts/toolchains/linux.cmake" "-DVCPKG_TARGET_TRIPLET=x64-linux" "-DVCPKG_SET_CHARSET_FLAG=ON" "-DVCPKG_PLATFORM_TOOLSET=external" "-DCMAKE_EXPORT_NO_PACKAGE_REGISTRY=ON" "-DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON" "-DCMAKE_FIND_PACKAGE_NO_SYSTEM_PACKAGE_REGISTRY=ON" "-DCMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP=TRUE" "-DCMAKE_VERBOSE_MAKEFILE=ON" "-DVCPKG_APPLOCAL_DEPS=OFF" "-DCMAKE_TOOLCHAIN_FILE=/home/nicolas/.local/share/vcpkg/scripts/buildsystems/vcpkg.cmake" "-DCMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION=ON" "-DVCPKG_CXX_FLAGS=" "-DVCPKG_CXX_FLAGS_RELEASE=" "-DVCPKG_CXX_FLAGS_DEBUG=" "-DVCPKG_C_FLAGS=" "-DVCPKG_C_FLAGS_RELEASE=" "-DVCPKG_C_FLAGS_DEBUG=" "-DVCPKG_CRT_LINKAGE=dynamic" "-DVCPKG_LINKER_FLAGS=" "-DVCPKG_LINKER_FLAGS_RELEASE=" "-DVCPKG_LINKER_FLAGS_DEBUG=" "-DVCPKG_TARGET_ARCHITECTURE=x64" "-DCMAKE_INSTALL_LIBDIR:STRING=lib" "-DCMAKE_INSTALL_BINDIR:STRING=bin" "-D_VCPKG_ROOT_DIR=/home/nicolas/.local/share/vcpkg" "-D_VCPKG_INSTALLED_DIR=/home/nicolas/WEMU/vcpkg_installed" "-DVCPKG_MANIFEST_INSTALL=OFF"
FAILED: [code=1] ../../x64-linux-dbg/CMakeCache.txt 
"/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/bin/cmake" -E chdir "../../x64-linux-dbg" "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/bin/cmake" "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/src/3.4-2448ff4533.clean" "-G" "Ninja" "-DCMAKE_BUILD_TYPE=Debug" "-DCMAKE_INSTALL_PREFIX=/home/nicolas/.local/share/vcpkg/packages/glfw3_x64-linux/debug" "-DFETCHCONTENT_FULLY_DISCONNECTED=ON" "-DGLFW_BUILD_EXAMPLES=OFF" "-DGLFW_BUILD_TESTS=OFF" "-DGLFW_BUILD_DOCS=OFF" "-DGLFW_BUILD_WAYLAND=OFF" "-DCMAKE_MAKE_PROGRAM=/home/nicolas/.local/share/vcpkg/downloads/tools/ninja-1.13.2-linux/ninja" "-DCMAKE_SYSTEM_NAME=Linux" "-DBUILD_SHARED_LIBS=OFF" "-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=/home/nicolas/.local/share/vcpkg/scripts/toolchains/linux.cmake" "-DVCPKG_TARGET_TRIPLET=x64-linux" "-DVCPKG_SET_CHARSET_FLAG=ON" "-DVCPKG_PLATFORM_TOOLSET=external" "-DCMAKE_EXPORT_NO_PACKAGE_REGISTRY=ON" "-DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON" "-DCMAKE_FIND_PACKAGE_NO_SYSTEM_PACKAGE_REGISTRY=ON" "-DCMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP=TRUE" "-DCMAKE_VERBOSE_MAKEFILE=ON" "-DVCPKG_APPLOCAL_DEPS=OFF" "-DCMAKE_TOOLCHAIN_FILE=/home/nicolas/.local/share/vcpkg/scripts/buildsystems/vcpkg.cmake" "-DCMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION=ON" "-DVCPKG_CXX_FLAGS=" "-DVCPKG_CXX_FLAGS_RELEASE=" "-DVCPKG_CXX_FLAGS_DEBUG=" "-DVCPKG_C_FLAGS=" "-DVCPKG_C_FLAGS_RELEASE=" "-DVCPKG_C_FLAGS_DEBUG=" "-DVCPKG_CRT_LINKAGE=dynamic" "-DVCPKG_LINKER_FLAGS=" "-DVCPKG_LINKER_FLAGS_RELEASE=" "-DVCPKG_LINKER_FLAGS_DEBUG=" "-DVCPKG_TARGET_ARCHITECTURE=x64" "-DCMAKE_INSTALL_LIBDIR:STRING=lib" "-DCMAKE_INSTALL_BINDIR:STRING=bin" "-D_VCPKG_ROOT_DIR=/home/nicolas/.local/share/vcpkg" "-D_VCPKG_INSTALLED_DIR=/home/nicolas/WEMU/vcpkg_installed" "-DVCPKG_MANIFEST_INSTALL=OFF"
-- The C compiler identification is GNU 13.3.0
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /usr/bin/cc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD - Success
-- Found Threads: TRUE
-- Including X11 support
-- Found X11: /usr/include
-- Looking for XOpenDisplay in /usr/lib/x86_64-linux-gnu/libX11.so;/usr/lib/x86_64-linux-gnu/libXext.so
-- Looking for XOpenDisplay in /usr/lib/x86_64-linux-gnu/libX11.so;/usr/lib/x86_64-linux-gnu/libXext.so - found
-- Looking for gethostbyname
-- Looking for gethostbyname - found
-- Looking for connect
-- Looking for connect - found
-- Looking for remove
-- Looking for remove - found
-- Looking for shmat
-- Looking for shmat - found
CMake Error at src/CMakeLists.txt:192 (message):
  Xinerama headers not found; install libxinerama development package


-- Configuring incomplete, errors occurred!
[2/2] "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/bin/cmake" -E chdir ".." "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/bin/cmake" "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/src/3.4-2448ff4533.clean" "-G" "Ninja" "-DCMAKE_BUILD_TYPE=Release" "-DCMAKE_INSTALL_PREFIX=/home/nicolas/.local/share/vcpkg/packages/glfw3_x64-linux" "-DFETCHCONTENT_FULLY_DISCONNECTED=ON" "-DGLFW_BUILD_EXAMPLES=OFF" "-DGLFW_BUILD_TESTS=OFF" "-DGLFW_BUILD_DOCS=OFF" "-DGLFW_BUILD_WAYLAND=OFF" "-DCMAKE_MAKE_PROGRAM=/home/nicolas/.local/share/vcpkg/downloads/tools/ninja-1.13.2-linux/ninja" "-DCMAKE_SYSTEM_NAME=Linux" "-DBUILD_SHARED_LIBS=OFF" "-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=/home/nicolas/.local/share/vcpkg/scripts/toolchains/linux.cmake" "-DVCPKG_TARGET_TRIPLET=x64-linux" "-DVCPKG_SET_CHARSET_FLAG=ON" "-DVCPKG_PLATFORM_TOOLSET=external" "-DCMAKE_EXPORT_NO_PACKAGE_REGISTRY=ON" "-DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON" "-DCMAKE_FIND_PACKAGE_NO_SYSTEM_PACKAGE_REGISTRY=ON" "-DCMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP=TRUE" "-DCMAKE_VERBOSE_MAKEFILE=ON" "-DVCPKG_APPLOCAL_DEPS=OFF" "-DCMAKE_TOOLCHAIN_FILE=/home/nicolas/.local/share/vcpkg/scripts/buildsystems/vcpkg.cmake" "-DCMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION=ON" "-DVCPKG_CXX_FLAGS=" "-DVCPKG_CXX_FLAGS_RELEASE=" "-DVCPKG_CXX_FLAGS_DEBUG=" "-DVCPKG_C_FLAGS=" "-DVCPKG_C_FLAGS_RELEASE=" "-DVCPKG_C_FLAGS_DEBUG=" "-DVCPKG_CRT_LINKAGE=dynamic" "-DVCPKG_LINKER_FLAGS=" "-DVCPKG_LINKER_FLAGS_RELEASE=" "-DVCPKG_LINKER_FLAGS_DEBUG=" "-DVCPKG_TARGET_ARCHITECTURE=x64" "-DCMAKE_INSTALL_LIBDIR:STRING=lib" "-DCMAKE_INSTALL_BINDIR:STRING=bin" "-D_VCPKG_ROOT_DIR=/home/nicolas/.local/share/vcpkg" "-D_VCPKG_INSTALLED_DIR=/home/nicolas/WEMU/vcpkg_installed" "-DVCPKG_MANIFEST_INSTALL=OFF"
FAILED: [code=1] ../CMakeCache.txt 
"/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/bin/cmake" -E chdir ".." "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/bin/cmake" "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/src/3.4-2448ff4533.clean" "-G" "Ninja" "-DCMAKE_BUILD_TYPE=Release" "-DCMAKE_INSTALL_PREFIX=/home/nicolas/.local/share/vcpkg/packages/glfw3_x64-linux" "-DFETCHCONTENT_FULLY_DISCONNECTED=ON" "-DGLFW_BUILD_EXAMPLES=OFF" "-DGLFW_BUILD_TESTS=OFF" "-DGLFW_BUILD_DOCS=OFF" "-DGLFW_BUILD_WAYLAND=OFF" "-DCMAKE_MAKE_PROGRAM=/home/nicolas/.local/share/vcpkg/downloads/tools/ninja-1.13.2-linux/ninja" "-DCMAKE_SYSTEM_NAME=Linux" "-DBUILD_SHARED_LIBS=OFF" "-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=/home/nicolas/.local/share/vcpkg/scripts/toolchains/linux.cmake" "-DVCPKG_TARGET_TRIPLET=x64-linux" "-DVCPKG_SET_CHARSET_FLAG=ON" "-DVCPKG_PLATFORM_TOOLSET=external" "-DCMAKE_EXPORT_NO_PACKAGE_REGISTRY=ON" "-DCMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY=ON" "-DCMAKE_FIND_PACKAGE_NO_SYSTEM_PACKAGE_REGISTRY=ON" "-DCMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP=TRUE" "-DCMAKE_VERBOSE_MAKEFILE=ON" "-DVCPKG_APPLOCAL_DEPS=OFF" "-DCMAKE_TOOLCHAIN_FILE=/home/nicolas/.local/share/vcpkg/scripts/buildsystems/vcpkg.cmake" "-DCMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION=ON" "-DVCPKG_CXX_FLAGS=" "-DVCPKG_CXX_FLAGS_RELEASE=" "-DVCPKG_CXX_FLAGS_DEBUG=" "-DVCPKG_C_FLAGS=" "-DVCPKG_C_FLAGS_RELEASE=" "-DVCPKG_C_FLAGS_DEBUG=" "-DVCPKG_CRT_LINKAGE=dynamic" "-DVCPKG_LINKER_FLAGS=" "-DVCPKG_LINKER_FLAGS_RELEASE=" "-DVCPKG_LINKER_FLAGS_DEBUG=" "-DVCPKG_TARGET_ARCHITECTURE=x64" "-DCMAKE_INSTALL_LIBDIR:STRING=lib" "-DCMAKE_INSTALL_BINDIR:STRING=bin" "-D_VCPKG_ROOT_DIR=/home/nicolas/.local/share/vcpkg" "-D_VCPKG_INSTALLED_DIR=/home/nicolas/WEMU/vcpkg_installed" "-DVCPKG_MANIFEST_INSTALL=OFF"
-- The C compiler identification is GNU 13.3.0
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - done
-- Check for working C compiler: /usr/bin/cc - skipped
-- Detecting C compile features
-- Detecting C compile features - done
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD
-- Performing Test CMAKE_HAVE_LIBC_PTHREAD - Success
-- Found Threads: TRUE
-- Including X11 support
-- Found X11: /usr/include
-- Looking for XOpenDisplay in /usr/lib/x86_64-linux-gnu/libX11.so;/usr/lib/x86_64-linux-gnu/libXext.so
-- Looking for XOpenDisplay in /usr/lib/x86_64-linux-gnu/libX11.so;/usr/lib/x86_64-linux-gnu/libXext.so - found
-- Looking for gethostbyname
-- Looking for gethostbyname - found
-- Looking for connect
-- Looking for connect - found
-- Looking for remove
-- Looking for remove - found
-- Looking for shmat
-- Looking for shmat - found
CMake Error at src/CMakeLists.txt:192 (message):
  Xinerama headers not found; install libxinerama development package


-- Configuring incomplete, errors occurred!
ninja: build stopped: subcommand failed.
```
</details>

<details><summary>/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/config-x64-linux-rel-CMakeCache.txt.log</summary>

```
# This is the CMakeCache file.
# For build in directory: /home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel
# It was generated by CMake: /home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/bin/cmake
# You can edit this file to change values found and used by cmake.
# If you do not want to change any of the values, simply exit the editor.
# If you do want to change a value, simply edit, save, and exit the editor.
# The syntax for the file is as follows:
# KEY:TYPE=VALUE
# KEY is the name of a variable in the cache.
# TYPE is a hint to GUIs for the type of VALUE, DO NOT EDIT TYPE!.
# VALUE is the current value for the KEY.

########################
# EXTERNAL cache entries
########################

//Build shared libraries
BUILD_SHARED_LIBS:BOOL=OFF

//Path to a program.
CMAKE_ADDR2LINE:FILEPATH=/usr/bin/addr2line

//Path to a program.
CMAKE_AR:FILEPATH=/usr/bin/ar

//Choose the type of build, options are: None Debug Release RelWithDebInfo
// MinSizeRel ...
CMAKE_BUILD_TYPE:STRING=Release

CMAKE_CROSSCOMPILING:BOOL=OFF

//C compiler
CMAKE_C_COMPILER:FILEPATH=/usr/bin/cc

//A wrapper around 'ar' adding the appropriate '--plugin' option
// for the GCC compiler
CMAKE_C_COMPILER_AR:FILEPATH=/usr/bin/gcc-ar-13

//A wrapper around 'ranlib' adding the appropriate '--plugin' option
// for the GCC compiler
CMAKE_C_COMPILER_RANLIB:FILEPATH=/usr/bin/gcc-ranlib-13

//Flags used by the C compiler during all build types.
CMAKE_C_FLAGS:STRING=-fPIC

//Flags used by the C compiler during DEBUG builds.
CMAKE_C_FLAGS_DEBUG:STRING=-g

//Flags used by the C compiler during MINSIZEREL builds.
CMAKE_C_FLAGS_MINSIZEREL:STRING=-Os -DNDEBUG

//Flags used by the C compiler during RELEASE builds.
CMAKE_C_FLAGS_RELEASE:STRING=-O3 -DNDEBUG

//Flags used by the C compiler during RELWITHDEBINFO builds.
CMAKE_C_FLAGS_RELWITHDEBINFO:STRING=-O2 -g -DNDEBUG

//Path to a program.
CMAKE_DLLTOOL:FILEPATH=CMAKE_DLLTOOL-NOTFOUND

//No help, variable specified on the command line.
CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION:UNINITIALIZED=ON

//Flags used by the linker during all build types.
CMAKE_EXE_LINKER_FLAGS:STRING=

//Flags used by the linker during DEBUG builds.
CMAKE_EXE_LINKER_FLAGS_DEBUG:STRING=

//Flags used by the linker during MINSIZEREL builds.
CMAKE_EXE_LINKER_FLAGS_MINSIZEREL:STRING=

//Flags used by the linker during RELEASE builds.
CMAKE_EXE_LINKER_FLAGS_RELEASE:STRING=

//Flags used by the linker during RELWITHDEBINFO builds.
CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO:STRING=

//Enable/Disable output of build database during the build.
CMAKE_EXPORT_BUILD_DATABASE:BOOL=

//Enable/Disable output of compile commands during generation.
CMAKE_EXPORT_COMPILE_COMMANDS:BOOL=

//No help, variable specified on the command line.
CMAKE_EXPORT_NO_PACKAGE_REGISTRY:UNINITIALIZED=ON

//No help, variable specified on the command line.
CMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY:UNINITIALIZED=ON

//No help, variable specified on the command line.
CMAKE_FIND_PACKAGE_NO_SYSTEM_PACKAGE_REGISTRY:UNINITIALIZED=ON

//Value Computed by CMake.
CMAKE_FIND_PACKAGE_REDIRECTS_DIR:STATIC=/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel/CMakeFiles/pkgRedirects

//No help, variable specified on the command line.
CMAKE_INSTALL_BINDIR:STRING=bin

//Read-only architecture-independent data (DATAROOTDIR)
CMAKE_INSTALL_DATADIR:PATH=

//Read-only architecture-independent data root (share)
CMAKE_INSTALL_DATAROOTDIR:PATH=share

//Documentation root (DATAROOTDIR/doc/PROJECT_NAME)
CMAKE_INSTALL_DOCDIR:PATH=

//C header files (include)
CMAKE_INSTALL_INCLUDEDIR:PATH=include

//Info documentation (DATAROOTDIR/info)
CMAKE_INSTALL_INFODIR:PATH=

//No help, variable specified on the command line.
CMAKE_INSTALL_LIBDIR:STRING=lib

//Program executables (libexec)
CMAKE_INSTALL_LIBEXECDIR:PATH=libexec

//Locale-dependent data (DATAROOTDIR/locale)
CMAKE_INSTALL_LOCALEDIR:PATH=

//Modifiable single-machine data (var)
CMAKE_INSTALL_LOCALSTATEDIR:PATH=var

//Man documentation (DATAROOTDIR/man)
CMAKE_INSTALL_MANDIR:PATH=

//C header files for non-gcc (/usr/include)
CMAKE_INSTALL_OLDINCLUDEDIR:PATH=/usr/include

//Install path prefix, prepended onto install directories.
CMAKE_INSTALL_PREFIX:PATH=/home/nicolas/.local/share/vcpkg/packages/glfw3_x64-linux

//Run-time variable data (LOCALSTATEDIR/run)
CMAKE_INSTALL_RUNSTATEDIR:PATH=

//System admin executables (sbin)
CMAKE_INSTALL_SBINDIR:PATH=sbin

//Modifiable architecture-independent data (com)
CMAKE_INSTALL_SHAREDSTATEDIR:PATH=com

//Read-only single-machine data (etc)
CMAKE_INSTALL_SYSCONFDIR:PATH=etc

//No help, variable specified on the command line.
CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP:UNINITIALIZED=TRUE

//Path to a program.
CMAKE_LINKER:FILEPATH=/usr/bin/ld

//No help, variable specified on the command line.
CMAKE_MAKE_PROGRAM:UNINITIALIZED=/home/nicolas/.local/share/vcpkg/downloads/tools/ninja-1.13.2-linux/ninja

//Flags used by the linker during the creation of modules during
// all build types.
CMAKE_MODULE_LINKER_FLAGS:STRING=

//Flags used by the linker during the creation of modules during
...
Skipped 902 lines
...
X11_Xft_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xft_LIB
X11_Xft_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xi_INCLUDE_PATH
X11_Xi_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xi_LIB
X11_Xi_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xinerama_INCLUDE_PATH
X11_Xinerama_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xinerama_LIB
X11_Xinerama_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xkb_INCLUDE_PATH
X11_Xkb_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xkblib_INCLUDE_PATH
X11_Xkblib_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xlib_INCLUDE_PATH
X11_Xlib_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xmu_INCLUDE_PATH
X11_Xmu_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xmu_LIB
X11_Xmu_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xpm_INCLUDE_PATH
X11_Xpm_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xpm_LIB
X11_Xpm_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xrandr_INCLUDE_PATH
X11_Xrandr_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xrandr_LIB
X11_Xrandr_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xrender_INCLUDE_PATH
X11_Xrender_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xrender_LIB
X11_Xrender_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xshape_INCLUDE_PATH
X11_Xshape_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xss_INCLUDE_PATH
X11_Xss_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xss_LIB
X11_Xss_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xt_INCLUDE_PATH
X11_Xt_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xt_LIB
X11_Xt_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xtst_INCLUDE_PATH
X11_Xtst_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xtst_LIB
X11_Xtst_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xutil_INCLUDE_PATH
X11_Xutil_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xv_INCLUDE_PATH
X11_Xv_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xv_LIB
X11_Xv_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xxf86misc_INCLUDE_PATH
X11_Xxf86misc_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xxf86misc_LIB
X11_Xxf86misc_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xxf86vm_INCLUDE_PATH
X11_Xxf86vm_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xxf86vm_LIB
X11_Xxf86vm_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_dpms_INCLUDE_PATH
X11_dpms_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_INCLUDE_PATH
X11_xcb_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_LIB
X11_xcb_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_composite_INCLUDE_PATH
X11_xcb_composite_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_composite_LIB
X11_xcb_composite_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_cursor_INCLUDE_PATH
X11_xcb_cursor_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_cursor_LIB
X11_xcb_cursor_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_damage_INCLUDE_PATH
X11_xcb_damage_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_damage_LIB
X11_xcb_damage_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_dpms_INCLUDE_PATH
X11_xcb_dpms_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_dpms_LIB
X11_xcb_dpms_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_dri2_INCLUDE_PATH
X11_xcb_dri2_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_dri2_LIB
X11_xcb_dri2_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_dri3_INCLUDE_PATH
X11_xcb_dri3_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_dri3_LIB
X11_xcb_dri3_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_errors_INCLUDE_PATH
X11_xcb_errors_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_errors_LIB
X11_xcb_errors_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_ewmh_INCLUDE_PATH
X11_xcb_ewmh_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_ewmh_LIB
X11_xcb_ewmh_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_glx_INCLUDE_PATH
X11_xcb_glx_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_glx_LIB
X11_xcb_glx_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_icccm_INCLUDE_PATH
X11_xcb_icccm_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_icccm_LIB
X11_xcb_icccm_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_image_INCLUDE_PATH
X11_xcb_image_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_image_LIB
X11_xcb_image_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_keysyms_INCLUDE_PATH
X11_xcb_keysyms_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_keysyms_LIB
X11_xcb_keysyms_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_present_INCLUDE_PATH
X11_xcb_present_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_present_LIB
X11_xcb_present_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_randr_INCLUDE_PATH
X11_xcb_randr_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_randr_LIB
X11_xcb_randr_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_record_INCLUDE_PATH
X11_xcb_record_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_record_LIB
X11_xcb_record_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_render_INCLUDE_PATH
X11_xcb_render_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_render_LIB
X11_xcb_render_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_render_util_INCLUDE_PATH
X11_xcb_render_util_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_render_util_LIB
X11_xcb_render_util_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_res_INCLUDE_PATH
X11_xcb_res_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_res_LIB
X11_xcb_res_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_screensaver_INCLUDE_PATH
X11_xcb_screensaver_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_screensaver_LIB
X11_xcb_screensaver_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_shape_INCLUDE_PATH
X11_xcb_shape_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_shape_LIB
X11_xcb_shape_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_shm_INCLUDE_PATH
X11_xcb_shm_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_shm_LIB
X11_xcb_shm_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_sync_INCLUDE_PATH
X11_xcb_sync_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_sync_LIB
X11_xcb_sync_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_util_INCLUDE_PATH
X11_xcb_util_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_util_LIB
X11_xcb_util_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xf86dri_INCLUDE_PATH
X11_xcb_xf86dri_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xf86dri_LIB
X11_xcb_xf86dri_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xfixes_INCLUDE_PATH
X11_xcb_xfixes_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xfixes_LIB
X11_xcb_xfixes_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xinerama_INCLUDE_PATH
X11_xcb_xinerama_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xinerama_LIB
X11_xcb_xinerama_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xinput_INCLUDE_PATH
X11_xcb_xinput_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xinput_LIB
X11_xcb_xinput_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xkb_LIB
X11_xcb_xkb_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xrm_INCLUDE_PATH
X11_xcb_xrm_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xrm_LIB
X11_xcb_xrm_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xtest_INCLUDE_PATH
X11_xcb_xtest_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xtest_LIB
X11_xcb_xtest_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xv_INCLUDE_PATH
X11_xcb_xv_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xv_LIB
X11_xcb_xv_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xvmc_INCLUDE_PATH
X11_xcb_xvmc_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xvmc_LIB
X11_xcb_xvmc_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xkbcommon_INCLUDE_PATH
X11_xkbcommon_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xkbcommon_LIB
X11_xkbcommon_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xkbcommon_X11_INCLUDE_PATH
X11_xkbcommon_X11_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xkbcommon_X11_LIB
X11_xkbcommon_X11_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xkbfile_INCLUDE_PATH
X11_xkbfile_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xkbfile_LIB
X11_xkbfile_LIB-ADVANCED:INTERNAL=1
//Making sure VCPKG_MANIFEST_MODE doesn't change
Z_VCPKG_CHECK_MANIFEST_MODE:INTERNAL=OFF
//Vcpkg root directory
Z_VCPKG_ROOT_DIR:INTERNAL=/home/nicolas/.local/share/vcpkg
//CMAKE_INSTALL_PREFIX during last run
_GNUInstallDirs_LAST_CMAKE_INSTALL_PREFIX:INTERNAL=/home/nicolas/.local/share/vcpkg/packages/glfw3_x64-linux
```
</details>

<details><summary>/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/config-x64-linux-dbg-CMakeCache.txt.log</summary>

```
# This is the CMakeCache file.
# For build in directory: /home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-dbg
# It was generated by CMake: /home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/bin/cmake
# You can edit this file to change values found and used by cmake.
# If you do not want to change any of the values, simply exit the editor.
# If you do want to change a value, simply edit, save, and exit the editor.
# The syntax for the file is as follows:
# KEY:TYPE=VALUE
# KEY is the name of a variable in the cache.
# TYPE is a hint to GUIs for the type of VALUE, DO NOT EDIT TYPE!.
# VALUE is the current value for the KEY.

########################
# EXTERNAL cache entries
########################

//Build shared libraries
BUILD_SHARED_LIBS:BOOL=OFF

//Path to a program.
CMAKE_ADDR2LINE:FILEPATH=/usr/bin/addr2line

//Path to a program.
CMAKE_AR:FILEPATH=/usr/bin/ar

//Choose the type of build, options are: None Debug Release RelWithDebInfo
// MinSizeRel ...
CMAKE_BUILD_TYPE:STRING=Debug

CMAKE_CROSSCOMPILING:BOOL=OFF

//C compiler
CMAKE_C_COMPILER:FILEPATH=/usr/bin/cc

//A wrapper around 'ar' adding the appropriate '--plugin' option
// for the GCC compiler
CMAKE_C_COMPILER_AR:FILEPATH=/usr/bin/gcc-ar-13

//A wrapper around 'ranlib' adding the appropriate '--plugin' option
// for the GCC compiler
CMAKE_C_COMPILER_RANLIB:FILEPATH=/usr/bin/gcc-ranlib-13

//Flags used by the C compiler during all build types.
CMAKE_C_FLAGS:STRING=-fPIC

//Flags used by the C compiler during DEBUG builds.
CMAKE_C_FLAGS_DEBUG:STRING=-g

//Flags used by the C compiler during MINSIZEREL builds.
CMAKE_C_FLAGS_MINSIZEREL:STRING=-Os -DNDEBUG

//Flags used by the C compiler during RELEASE builds.
CMAKE_C_FLAGS_RELEASE:STRING=-O3 -DNDEBUG

//Flags used by the C compiler during RELWITHDEBINFO builds.
CMAKE_C_FLAGS_RELWITHDEBINFO:STRING=-O2 -g -DNDEBUG

//Path to a program.
CMAKE_DLLTOOL:FILEPATH=CMAKE_DLLTOOL-NOTFOUND

//No help, variable specified on the command line.
CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION:UNINITIALIZED=ON

//Flags used by the linker during all build types.
CMAKE_EXE_LINKER_FLAGS:STRING=

//Flags used by the linker during DEBUG builds.
CMAKE_EXE_LINKER_FLAGS_DEBUG:STRING=

//Flags used by the linker during MINSIZEREL builds.
CMAKE_EXE_LINKER_FLAGS_MINSIZEREL:STRING=

//Flags used by the linker during RELEASE builds.
CMAKE_EXE_LINKER_FLAGS_RELEASE:STRING=

//Flags used by the linker during RELWITHDEBINFO builds.
CMAKE_EXE_LINKER_FLAGS_RELWITHDEBINFO:STRING=

//Enable/Disable output of build database during the build.
CMAKE_EXPORT_BUILD_DATABASE:BOOL=

//Enable/Disable output of compile commands during generation.
CMAKE_EXPORT_COMPILE_COMMANDS:BOOL=

//No help, variable specified on the command line.
CMAKE_EXPORT_NO_PACKAGE_REGISTRY:UNINITIALIZED=ON

//No help, variable specified on the command line.
CMAKE_FIND_PACKAGE_NO_PACKAGE_REGISTRY:UNINITIALIZED=ON

//No help, variable specified on the command line.
CMAKE_FIND_PACKAGE_NO_SYSTEM_PACKAGE_REGISTRY:UNINITIALIZED=ON

//Value Computed by CMake.
CMAKE_FIND_PACKAGE_REDIRECTS_DIR:STATIC=/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-dbg/CMakeFiles/pkgRedirects

//No help, variable specified on the command line.
CMAKE_INSTALL_BINDIR:STRING=bin

//Read-only architecture-independent data (DATAROOTDIR)
CMAKE_INSTALL_DATADIR:PATH=

//Read-only architecture-independent data root (share)
CMAKE_INSTALL_DATAROOTDIR:PATH=share

//Documentation root (DATAROOTDIR/doc/PROJECT_NAME)
CMAKE_INSTALL_DOCDIR:PATH=

//C header files (include)
CMAKE_INSTALL_INCLUDEDIR:PATH=include

//Info documentation (DATAROOTDIR/info)
CMAKE_INSTALL_INFODIR:PATH=

//No help, variable specified on the command line.
CMAKE_INSTALL_LIBDIR:STRING=lib

//Program executables (libexec)
CMAKE_INSTALL_LIBEXECDIR:PATH=libexec

//Locale-dependent data (DATAROOTDIR/locale)
CMAKE_INSTALL_LOCALEDIR:PATH=

//Modifiable single-machine data (var)
CMAKE_INSTALL_LOCALSTATEDIR:PATH=var

//Man documentation (DATAROOTDIR/man)
CMAKE_INSTALL_MANDIR:PATH=

//C header files for non-gcc (/usr/include)
...
Skipped 977 lines
...
X11_Xtst_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xtst_LIB
X11_Xtst_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xutil_INCLUDE_PATH
X11_Xutil_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xv_INCLUDE_PATH
X11_Xv_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xv_LIB
X11_Xv_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xxf86misc_INCLUDE_PATH
X11_Xxf86misc_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xxf86misc_LIB
X11_Xxf86misc_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xxf86vm_INCLUDE_PATH
X11_Xxf86vm_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_Xxf86vm_LIB
X11_Xxf86vm_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_dpms_INCLUDE_PATH
X11_dpms_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_INCLUDE_PATH
X11_xcb_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_LIB
X11_xcb_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_composite_INCLUDE_PATH
X11_xcb_composite_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_composite_LIB
X11_xcb_composite_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_cursor_INCLUDE_PATH
X11_xcb_cursor_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_cursor_LIB
X11_xcb_cursor_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_damage_INCLUDE_PATH
X11_xcb_damage_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_damage_LIB
X11_xcb_damage_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_dpms_INCLUDE_PATH
X11_xcb_dpms_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_dpms_LIB
X11_xcb_dpms_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_dri2_INCLUDE_PATH
X11_xcb_dri2_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_dri2_LIB
X11_xcb_dri2_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_dri3_INCLUDE_PATH
X11_xcb_dri3_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_dri3_LIB
X11_xcb_dri3_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_errors_INCLUDE_PATH
X11_xcb_errors_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_errors_LIB
X11_xcb_errors_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_ewmh_INCLUDE_PATH
X11_xcb_ewmh_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_ewmh_LIB
X11_xcb_ewmh_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_glx_INCLUDE_PATH
X11_xcb_glx_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_glx_LIB
X11_xcb_glx_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_icccm_INCLUDE_PATH
X11_xcb_icccm_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_icccm_LIB
X11_xcb_icccm_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_image_INCLUDE_PATH
X11_xcb_image_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_image_LIB
X11_xcb_image_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_keysyms_INCLUDE_PATH
X11_xcb_keysyms_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_keysyms_LIB
X11_xcb_keysyms_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_present_INCLUDE_PATH
X11_xcb_present_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_present_LIB
X11_xcb_present_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_randr_INCLUDE_PATH
X11_xcb_randr_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_randr_LIB
X11_xcb_randr_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_record_INCLUDE_PATH
X11_xcb_record_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_record_LIB
X11_xcb_record_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_render_INCLUDE_PATH
X11_xcb_render_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_render_LIB
X11_xcb_render_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_render_util_INCLUDE_PATH
X11_xcb_render_util_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_render_util_LIB
X11_xcb_render_util_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_res_INCLUDE_PATH
X11_xcb_res_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_res_LIB
X11_xcb_res_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_screensaver_INCLUDE_PATH
X11_xcb_screensaver_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_screensaver_LIB
X11_xcb_screensaver_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_shape_INCLUDE_PATH
X11_xcb_shape_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_shape_LIB
X11_xcb_shape_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_shm_INCLUDE_PATH
X11_xcb_shm_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_shm_LIB
X11_xcb_shm_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_sync_INCLUDE_PATH
X11_xcb_sync_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_sync_LIB
X11_xcb_sync_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_util_INCLUDE_PATH
X11_xcb_util_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_util_LIB
X11_xcb_util_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xf86dri_INCLUDE_PATH
X11_xcb_xf86dri_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xf86dri_LIB
X11_xcb_xf86dri_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xfixes_INCLUDE_PATH
X11_xcb_xfixes_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xfixes_LIB
X11_xcb_xfixes_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xinerama_INCLUDE_PATH
X11_xcb_xinerama_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xinerama_LIB
X11_xcb_xinerama_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xinput_INCLUDE_PATH
X11_xcb_xinput_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xinput_LIB
X11_xcb_xinput_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xkb_LIB
X11_xcb_xkb_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xrm_INCLUDE_PATH
X11_xcb_xrm_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xrm_LIB
X11_xcb_xrm_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xtest_INCLUDE_PATH
X11_xcb_xtest_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xtest_LIB
X11_xcb_xtest_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xv_INCLUDE_PATH
X11_xcb_xv_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xv_LIB
X11_xcb_xv_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xvmc_INCLUDE_PATH
X11_xcb_xvmc_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xcb_xvmc_LIB
X11_xcb_xvmc_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xkbcommon_INCLUDE_PATH
X11_xkbcommon_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xkbcommon_LIB
X11_xkbcommon_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xkbcommon_X11_INCLUDE_PATH
X11_xkbcommon_X11_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xkbcommon_X11_LIB
X11_xkbcommon_X11_LIB-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xkbfile_INCLUDE_PATH
X11_xkbfile_INCLUDE_PATH-ADVANCED:INTERNAL=1
//ADVANCED property for variable: X11_xkbfile_LIB
X11_xkbfile_LIB-ADVANCED:INTERNAL=1
//Making sure VCPKG_MANIFEST_MODE doesn't change
Z_VCPKG_CHECK_MANIFEST_MODE:INTERNAL=OFF
//Vcpkg root directory
Z_VCPKG_ROOT_DIR:INTERNAL=/home/nicolas/.local/share/vcpkg
//CMAKE_INSTALL_PREFIX during last run
_GNUInstallDirs_LAST_CMAKE_INSTALL_PREFIX:INTERNAL=/home/nicolas/.local/share/vcpkg/packages/glfw3_x64-linux/debug
```
</details>

<details><summary>/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/config-x64-linux-dbg-CMakeConfigureLog.yaml.log</summary>

```

---
events:
  -
    kind: "message-v1"
    backtrace:
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/CMakeDetermineSystem.cmake:205 (message)"
      - "CMakeLists.txt:3 (project)"
    message: |
      The system is: Linux -  - x86_64
  -
    kind: "message-v1"
    backtrace:
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/CMakeDetermineCompilerId.cmake:17 (message)"
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/CMakeDetermineCompilerId.cmake:64 (__determine_compiler_id_test)"
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/CMakeDetermineCCompiler.cmake:123 (CMAKE_DETERMINE_COMPILER_ID)"
      - "CMakeLists.txt:3 (project)"
    message: |
      Compiling the C compiler identification source file "CMakeCCompilerId.c" succeeded.
      Compiler: /usr/bin/cc 
      Build flags: -fPIC
      Id flags:  
      
      The output was:
      0
      
      
      Compilation of the C compiler identification source "CMakeCCompilerId.c" produced "a.out"
      
      The C compiler identification is GNU, found in:
        /home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-dbg/CMakeFiles/3.31.10/CompilerIdC/a.out
      
  -
    kind: "try_compile-v1"
    backtrace:
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/CMakeDetermineCompilerABI.cmake:74 (try_compile)"
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/CMakeTestCCompiler.cmake:26 (CMAKE_DETERMINE_COMPILER_ABI)"
      - "CMakeLists.txt:3 (project)"
    checks:
      - "Detecting C compiler ABI info"
    directories:
      source: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-dbg/CMakeFiles/CMakeScratch/TryCompile-KNtBWH"
      binary: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-dbg/CMakeFiles/CMakeScratch/TryCompile-KNtBWH"
    cmakeVariables:
      CMAKE_C_FLAGS: "-fPIC"
      CMAKE_C_FLAGS_DEBUG: "-g"
      CMAKE_EXE_LINKER_FLAGS: ""
      VCPKG_CHAINLOAD_TOOLCHAIN_FILE: "/home/nicolas/.local/share/vcpkg/scripts/toolchains/linux.cmake"
      VCPKG_CRT_LINKAGE: "dynamic"
      VCPKG_CXX_FLAGS: ""
      VCPKG_CXX_FLAGS_DEBUG: ""
      VCPKG_CXX_FLAGS_RELEASE: ""
      VCPKG_C_FLAGS: ""
      VCPKG_C_FLAGS_DEBUG: ""
      VCPKG_C_FLAGS_RELEASE: ""
      VCPKG_INSTALLED_DIR: "/home/nicolas/WEMU/vcpkg_installed"
      VCPKG_LINKER_FLAGS: ""
      VCPKG_LINKER_FLAGS_DEBUG: ""
      VCPKG_LINKER_FLAGS_RELEASE: ""
      VCPKG_PREFER_SYSTEM_LIBS: "OFF"
      VCPKG_TARGET_ARCHITECTURE: "x64"
      VCPKG_TARGET_TRIPLET: "x64-linux"
      Z_VCPKG_ROOT_DIR: "/home/nicolas/.local/share/vcpkg"
    buildResult:
      variable: "CMAKE_C_ABI_COMPILED"
      cached: true
      stdout: |
        Change Dir: '/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-dbg/CMakeFiles/CMakeScratch/TryCompile-KNtBWH'
        
        Run Build Command(s): /home/nicolas/.local/share/vcpkg/downloads/tools/ninja-1.13.2-linux/ninja -v cmTC_9b0d7
        [1/2] /usr/bin/cc   -fPIC    -v -o CMakeFiles/cmTC_9b0d7.dir/CMakeCCompilerABI.c.o -c /home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/CMakeCCompilerABI.c
        Using built-in specs.
        COLLECT_GCC=/usr/bin/cc
        OFFLOAD_TARGET_NAMES=nvptx-none:amdgcn-amdhsa
        OFFLOAD_TARGET_DEFAULT=1
        Target: x86_64-linux-gnu
...
Skipped 333 lines
...
      VCPKG_TARGET_TRIPLET: "x64-linux"
      Z_VCPKG_ROOT_DIR: "/home/nicolas/.local/share/vcpkg"
    buildResult:
      variable: "CMAKE_HAVE_GETHOSTBYNAME"
      cached: true
      stdout: |
        Change Dir: '/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-dbg/CMakeFiles/CMakeScratch/TryCompile-9DyHZ6'
        
        Run Build Command(s): /home/nicolas/.local/share/vcpkg/downloads/tools/ninja-1.13.2-linux/ninja -v cmTC_baa56
        [1/2] /usr/bin/cc   -fPIC -DCHECK_FUNCTION_EXISTS=gethostbyname -o CMakeFiles/cmTC_baa56.dir/CheckFunctionExists.c.o -c /home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-dbg/CMakeFiles/CMakeScratch/TryCompile-9DyHZ6/CheckFunctionExists.c
        [2/2] : && /usr/bin/cc -fPIC -DCHECK_FUNCTION_EXISTS=gethostbyname  CMakeFiles/cmTC_baa56.dir/CheckFunctionExists.c.o -o cmTC_baa56   && :
        
      exitCode: 0
  -
    kind: "try_compile-v1"
    backtrace:
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/CheckFunctionExists.cmake:93 (try_compile)"
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/FindX11.cmake:722 (check_function_exists)"
      - "/home/nicolas/.local/share/vcpkg/scripts/buildsystems/vcpkg.cmake:908 (_find_package)"
      - "src/CMakeLists.txt:181 (find_package)"
    checks:
      - "Looking for connect"
    directories:
      source: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-dbg/CMakeFiles/CMakeScratch/TryCompile-f1Pxg9"
      binary: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-dbg/CMakeFiles/CMakeScratch/TryCompile-f1Pxg9"
    cmakeVariables:
      CMAKE_C_FLAGS: "-fPIC"
      CMAKE_C_FLAGS_DEBUG: "-g"
      CMAKE_EXE_LINKER_FLAGS: ""
      CMAKE_MODULE_PATH: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/src/3.4-2448ff4533.clean/CMake/modules"
      VCPKG_CHAINLOAD_TOOLCHAIN_FILE: "/home/nicolas/.local/share/vcpkg/scripts/toolchains/linux.cmake"
      VCPKG_CRT_LINKAGE: "dynamic"
      VCPKG_CXX_FLAGS: ""
      VCPKG_CXX_FLAGS_DEBUG: ""
      VCPKG_CXX_FLAGS_RELEASE: ""
      VCPKG_C_FLAGS: ""
      VCPKG_C_FLAGS_DEBUG: ""
      VCPKG_C_FLAGS_RELEASE: ""
      VCPKG_INSTALLED_DIR: "/home/nicolas/WEMU/vcpkg_installed"
      VCPKG_LINKER_FLAGS: ""
      VCPKG_LINKER_FLAGS_DEBUG: ""
      VCPKG_LINKER_FLAGS_RELEASE: ""
      VCPKG_PREFER_SYSTEM_LIBS: "OFF"
      VCPKG_TARGET_ARCHITECTURE: "x64"
      VCPKG_TARGET_TRIPLET: "x64-linux"
      Z_VCPKG_ROOT_DIR: "/home/nicolas/.local/share/vcpkg"
    buildResult:
      variable: "CMAKE_HAVE_CONNECT"
      cached: true
      stdout: |
        Change Dir: '/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-dbg/CMakeFiles/CMakeScratch/TryCompile-f1Pxg9'
        
        Run Build Command(s): /home/nicolas/.local/share/vcpkg/downloads/tools/ninja-1.13.2-linux/ninja -v cmTC_06596
        [1/2] /usr/bin/cc   -fPIC -DCHECK_FUNCTION_EXISTS=connect -o CMakeFiles/cmTC_06596.dir/CheckFunctionExists.c.o -c /home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-dbg/CMakeFiles/CMakeScratch/TryCompile-f1Pxg9/CheckFunctionExists.c
        [2/2] : && /usr/bin/cc -fPIC -DCHECK_FUNCTION_EXISTS=connect  CMakeFiles/cmTC_06596.dir/CheckFunctionExists.c.o -o cmTC_06596   && :
        
      exitCode: 0
  -
    kind: "try_compile-v1"
    backtrace:
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/CheckFunctionExists.cmake:93 (try_compile)"
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/FindX11.cmake:731 (check_function_exists)"
      - "/home/nicolas/.local/share/vcpkg/scripts/buildsystems/vcpkg.cmake:908 (_find_package)"
      - "src/CMakeLists.txt:181 (find_package)"
    checks:
      - "Looking for remove"
    directories:
      source: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-dbg/CMakeFiles/CMakeScratch/TryCompile-idIEvk"
      binary: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-dbg/CMakeFiles/CMakeScratch/TryCompile-idIEvk"
    cmakeVariables:
      CMAKE_C_FLAGS: "-fPIC"
      CMAKE_C_FLAGS_DEBUG: "-g"
      CMAKE_EXE_LINKER_FLAGS: ""
      CMAKE_MODULE_PATH: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/src/3.4-2448ff4533.clean/CMake/modules"
      VCPKG_CHAINLOAD_TOOLCHAIN_FILE: "/home/nicolas/.local/share/vcpkg/scripts/toolchains/linux.cmake"
      VCPKG_CRT_LINKAGE: "dynamic"
      VCPKG_CXX_FLAGS: ""
      VCPKG_CXX_FLAGS_DEBUG: ""
      VCPKG_CXX_FLAGS_RELEASE: ""
      VCPKG_C_FLAGS: ""
      VCPKG_C_FLAGS_DEBUG: ""
      VCPKG_C_FLAGS_RELEASE: ""
      VCPKG_INSTALLED_DIR: "/home/nicolas/WEMU/vcpkg_installed"
      VCPKG_LINKER_FLAGS: ""
      VCPKG_LINKER_FLAGS_DEBUG: ""
      VCPKG_LINKER_FLAGS_RELEASE: ""
      VCPKG_PREFER_SYSTEM_LIBS: "OFF"
      VCPKG_TARGET_ARCHITECTURE: "x64"
      VCPKG_TARGET_TRIPLET: "x64-linux"
      Z_VCPKG_ROOT_DIR: "/home/nicolas/.local/share/vcpkg"
    buildResult:
      variable: "CMAKE_HAVE_REMOVE"
      cached: true
      stdout: |
        Change Dir: '/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-dbg/CMakeFiles/CMakeScratch/TryCompile-idIEvk'
        
        Run Build Command(s): /home/nicolas/.local/share/vcpkg/downloads/tools/ninja-1.13.2-linux/ninja -v cmTC_b25a4
        [1/2] /usr/bin/cc   -fPIC -DCHECK_FUNCTION_EXISTS=remove -o CMakeFiles/cmTC_b25a4.dir/CheckFunctionExists.c.o -c /home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-dbg/CMakeFiles/CMakeScratch/TryCompile-idIEvk/CheckFunctionExists.c
        [2/2] : && /usr/bin/cc -fPIC -DCHECK_FUNCTION_EXISTS=remove  CMakeFiles/cmTC_b25a4.dir/CheckFunctionExists.c.o -o cmTC_b25a4   && :
        
      exitCode: 0
  -
    kind: "try_compile-v1"
    backtrace:
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/CheckFunctionExists.cmake:93 (try_compile)"
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/FindX11.cmake:740 (check_function_exists)"
      - "/home/nicolas/.local/share/vcpkg/scripts/buildsystems/vcpkg.cmake:908 (_find_package)"
      - "src/CMakeLists.txt:181 (find_package)"
    checks:
      - "Looking for shmat"
    directories:
      source: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-dbg/CMakeFiles/CMakeScratch/TryCompile-pUa3l3"
      binary: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-dbg/CMakeFiles/CMakeScratch/TryCompile-pUa3l3"
    cmakeVariables:
      CMAKE_C_FLAGS: "-fPIC"
      CMAKE_C_FLAGS_DEBUG: "-g"
      CMAKE_EXE_LINKER_FLAGS: ""
      CMAKE_MODULE_PATH: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/src/3.4-2448ff4533.clean/CMake/modules"
      VCPKG_CHAINLOAD_TOOLCHAIN_FILE: "/home/nicolas/.local/share/vcpkg/scripts/toolchains/linux.cmake"
      VCPKG_CRT_LINKAGE: "dynamic"
      VCPKG_CXX_FLAGS: ""
      VCPKG_CXX_FLAGS_DEBUG: ""
      VCPKG_CXX_FLAGS_RELEASE: ""
      VCPKG_C_FLAGS: ""
      VCPKG_C_FLAGS_DEBUG: ""
      VCPKG_C_FLAGS_RELEASE: ""
      VCPKG_INSTALLED_DIR: "/home/nicolas/WEMU/vcpkg_installed"
      VCPKG_LINKER_FLAGS: ""
      VCPKG_LINKER_FLAGS_DEBUG: ""
      VCPKG_LINKER_FLAGS_RELEASE: ""
      VCPKG_PREFER_SYSTEM_LIBS: "OFF"
      VCPKG_TARGET_ARCHITECTURE: "x64"
      VCPKG_TARGET_TRIPLET: "x64-linux"
      Z_VCPKG_ROOT_DIR: "/home/nicolas/.local/share/vcpkg"
    buildResult:
      variable: "CMAKE_HAVE_SHMAT"
      cached: true
      stdout: |
        Change Dir: '/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-dbg/CMakeFiles/CMakeScratch/TryCompile-pUa3l3'
        
        Run Build Command(s): /home/nicolas/.local/share/vcpkg/downloads/tools/ninja-1.13.2-linux/ninja -v cmTC_10127
        [1/2] /usr/bin/cc   -fPIC -DCHECK_FUNCTION_EXISTS=shmat -o CMakeFiles/cmTC_10127.dir/CheckFunctionExists.c.o -c /home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-dbg/CMakeFiles/CMakeScratch/TryCompile-pUa3l3/CheckFunctionExists.c
        [2/2] : && /usr/bin/cc -fPIC -DCHECK_FUNCTION_EXISTS=shmat  CMakeFiles/cmTC_10127.dir/CheckFunctionExists.c.o -o cmTC_10127   && :
        
      exitCode: 0
...
```
</details>

<details><summary>/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/config-x64-linux-rel-CMakeConfigureLog.yaml.log</summary>

```

---
events:
  -
    kind: "message-v1"
    backtrace:
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/CMakeDetermineSystem.cmake:205 (message)"
      - "CMakeLists.txt:3 (project)"
    message: |
      The system is: Linux -  - x86_64
  -
    kind: "message-v1"
    backtrace:
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/CMakeDetermineCompilerId.cmake:17 (message)"
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/CMakeDetermineCompilerId.cmake:64 (__determine_compiler_id_test)"
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/CMakeDetermineCCompiler.cmake:123 (CMAKE_DETERMINE_COMPILER_ID)"
      - "CMakeLists.txt:3 (project)"
    message: |
      Compiling the C compiler identification source file "CMakeCCompilerId.c" succeeded.
      Compiler: /usr/bin/cc 
      Build flags: -fPIC
      Id flags:  
      
      The output was:
      0
      
      
      Compilation of the C compiler identification source "CMakeCCompilerId.c" produced "a.out"
      
      The C compiler identification is GNU, found in:
        /home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel/CMakeFiles/3.31.10/CompilerIdC/a.out
      
  -
    kind: "try_compile-v1"
    backtrace:
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/CMakeDetermineCompilerABI.cmake:74 (try_compile)"
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/CMakeTestCCompiler.cmake:26 (CMAKE_DETERMINE_COMPILER_ABI)"
      - "CMakeLists.txt:3 (project)"
    checks:
      - "Detecting C compiler ABI info"
    directories:
      source: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel/CMakeFiles/CMakeScratch/TryCompile-ZYtsfp"
      binary: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel/CMakeFiles/CMakeScratch/TryCompile-ZYtsfp"
    cmakeVariables:
      CMAKE_C_FLAGS: "-fPIC"
      CMAKE_C_FLAGS_DEBUG: "-g"
      CMAKE_EXE_LINKER_FLAGS: ""
      VCPKG_CHAINLOAD_TOOLCHAIN_FILE: "/home/nicolas/.local/share/vcpkg/scripts/toolchains/linux.cmake"
      VCPKG_CRT_LINKAGE: "dynamic"
      VCPKG_CXX_FLAGS: ""
      VCPKG_CXX_FLAGS_DEBUG: ""
      VCPKG_CXX_FLAGS_RELEASE: ""
      VCPKG_C_FLAGS: ""
      VCPKG_C_FLAGS_DEBUG: ""
      VCPKG_C_FLAGS_RELEASE: ""
      VCPKG_INSTALLED_DIR: "/home/nicolas/WEMU/vcpkg_installed"
      VCPKG_LINKER_FLAGS: ""
      VCPKG_LINKER_FLAGS_DEBUG: ""
      VCPKG_LINKER_FLAGS_RELEASE: ""
      VCPKG_PREFER_SYSTEM_LIBS: "OFF"
      VCPKG_TARGET_ARCHITECTURE: "x64"
      VCPKG_TARGET_TRIPLET: "x64-linux"
      Z_VCPKG_ROOT_DIR: "/home/nicolas/.local/share/vcpkg"
    buildResult:
      variable: "CMAKE_C_ABI_COMPILED"
      cached: true
      stdout: |
        Change Dir: '/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel/CMakeFiles/CMakeScratch/TryCompile-ZYtsfp'
        
        Run Build Command(s): /home/nicolas/.local/share/vcpkg/downloads/tools/ninja-1.13.2-linux/ninja -v cmTC_07029
        [1/2] /usr/bin/cc   -fPIC    -v -o CMakeFiles/cmTC_07029.dir/CMakeCCompilerABI.c.o -c /home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/CMakeCCompilerABI.c
        Using built-in specs.
        COLLECT_GCC=/usr/bin/cc
        OFFLOAD_TARGET_NAMES=nvptx-none:amdgcn-amdhsa
        OFFLOAD_TARGET_DEFAULT=1
        Target: x86_64-linux-gnu
...
Skipped 327 lines
...
      VCPKG_INSTALLED_DIR: "/home/nicolas/WEMU/vcpkg_installed"
      VCPKG_LINKER_FLAGS: ""
      VCPKG_LINKER_FLAGS_DEBUG: ""
      VCPKG_LINKER_FLAGS_RELEASE: ""
      VCPKG_PREFER_SYSTEM_LIBS: "OFF"
      VCPKG_TARGET_ARCHITECTURE: "x64"
      VCPKG_TARGET_TRIPLET: "x64-linux"
      Z_VCPKG_ROOT_DIR: "/home/nicolas/.local/share/vcpkg"
    buildResult:
      variable: "CMAKE_HAVE_GETHOSTBYNAME"
      cached: true
      stdout: |
        Change Dir: '/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel/CMakeFiles/CMakeScratch/TryCompile-Sa6gmI'
        
        Run Build Command(s): /home/nicolas/.local/share/vcpkg/downloads/tools/ninja-1.13.2-linux/ninja -v cmTC_9bc40
        [1/2] /usr/bin/cc   -fPIC -DCHECK_FUNCTION_EXISTS=gethostbyname -o CMakeFiles/cmTC_9bc40.dir/CheckFunctionExists.c.o -c /home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel/CMakeFiles/CMakeScratch/TryCompile-Sa6gmI/CheckFunctionExists.c
        [2/2] : && /usr/bin/cc -fPIC -DCHECK_FUNCTION_EXISTS=gethostbyname  CMakeFiles/cmTC_9bc40.dir/CheckFunctionExists.c.o -o cmTC_9bc40   && :
        
      exitCode: 0
  -
    kind: "try_compile-v1"
    backtrace:
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/CheckFunctionExists.cmake:93 (try_compile)"
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/FindX11.cmake:722 (check_function_exists)"
      - "/home/nicolas/.local/share/vcpkg/scripts/buildsystems/vcpkg.cmake:908 (_find_package)"
      - "src/CMakeLists.txt:181 (find_package)"
    checks:
      - "Looking for connect"
    directories:
      source: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel/CMakeFiles/CMakeScratch/TryCompile-LDQrpR"
      binary: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel/CMakeFiles/CMakeScratch/TryCompile-LDQrpR"
    cmakeVariables:
      CMAKE_C_FLAGS: "-fPIC"
      CMAKE_C_FLAGS_DEBUG: "-g"
      CMAKE_EXE_LINKER_FLAGS: ""
      CMAKE_MODULE_PATH: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/src/3.4-2448ff4533.clean/CMake/modules"
      VCPKG_CHAINLOAD_TOOLCHAIN_FILE: "/home/nicolas/.local/share/vcpkg/scripts/toolchains/linux.cmake"
      VCPKG_CRT_LINKAGE: "dynamic"
      VCPKG_CXX_FLAGS: ""
      VCPKG_CXX_FLAGS_DEBUG: ""
      VCPKG_CXX_FLAGS_RELEASE: ""
      VCPKG_C_FLAGS: ""
      VCPKG_C_FLAGS_DEBUG: ""
      VCPKG_C_FLAGS_RELEASE: ""
      VCPKG_INSTALLED_DIR: "/home/nicolas/WEMU/vcpkg_installed"
      VCPKG_LINKER_FLAGS: ""
      VCPKG_LINKER_FLAGS_DEBUG: ""
      VCPKG_LINKER_FLAGS_RELEASE: ""
      VCPKG_PREFER_SYSTEM_LIBS: "OFF"
      VCPKG_TARGET_ARCHITECTURE: "x64"
      VCPKG_TARGET_TRIPLET: "x64-linux"
      Z_VCPKG_ROOT_DIR: "/home/nicolas/.local/share/vcpkg"
    buildResult:
      variable: "CMAKE_HAVE_CONNECT"
      cached: true
      stdout: |
        Change Dir: '/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel/CMakeFiles/CMakeScratch/TryCompile-LDQrpR'
        
        Run Build Command(s): /home/nicolas/.local/share/vcpkg/downloads/tools/ninja-1.13.2-linux/ninja -v cmTC_94ca3
        [1/2] /usr/bin/cc   -fPIC -DCHECK_FUNCTION_EXISTS=connect -o CMakeFiles/cmTC_94ca3.dir/CheckFunctionExists.c.o -c /home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel/CMakeFiles/CMakeScratch/TryCompile-LDQrpR/CheckFunctionExists.c
        [2/2] : && /usr/bin/cc -fPIC -DCHECK_FUNCTION_EXISTS=connect  CMakeFiles/cmTC_94ca3.dir/CheckFunctionExists.c.o -o cmTC_94ca3   && :
        
      exitCode: 0
  -
    kind: "try_compile-v1"
    backtrace:
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/CheckFunctionExists.cmake:93 (try_compile)"
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/FindX11.cmake:731 (check_function_exists)"
      - "/home/nicolas/.local/share/vcpkg/scripts/buildsystems/vcpkg.cmake:908 (_find_package)"
      - "src/CMakeLists.txt:181 (find_package)"
    checks:
      - "Looking for remove"
    directories:
      source: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel/CMakeFiles/CMakeScratch/TryCompile-fLbSjo"
      binary: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel/CMakeFiles/CMakeScratch/TryCompile-fLbSjo"
    cmakeVariables:
      CMAKE_C_FLAGS: "-fPIC"
      CMAKE_C_FLAGS_DEBUG: "-g"
      CMAKE_EXE_LINKER_FLAGS: ""
      CMAKE_MODULE_PATH: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/src/3.4-2448ff4533.clean/CMake/modules"
      VCPKG_CHAINLOAD_TOOLCHAIN_FILE: "/home/nicolas/.local/share/vcpkg/scripts/toolchains/linux.cmake"
      VCPKG_CRT_LINKAGE: "dynamic"
      VCPKG_CXX_FLAGS: ""
      VCPKG_CXX_FLAGS_DEBUG: ""
      VCPKG_CXX_FLAGS_RELEASE: ""
      VCPKG_C_FLAGS: ""
      VCPKG_C_FLAGS_DEBUG: ""
      VCPKG_C_FLAGS_RELEASE: ""
      VCPKG_INSTALLED_DIR: "/home/nicolas/WEMU/vcpkg_installed"
      VCPKG_LINKER_FLAGS: ""
      VCPKG_LINKER_FLAGS_DEBUG: ""
      VCPKG_LINKER_FLAGS_RELEASE: ""
      VCPKG_PREFER_SYSTEM_LIBS: "OFF"
      VCPKG_TARGET_ARCHITECTURE: "x64"
      VCPKG_TARGET_TRIPLET: "x64-linux"
      Z_VCPKG_ROOT_DIR: "/home/nicolas/.local/share/vcpkg"
    buildResult:
      variable: "CMAKE_HAVE_REMOVE"
      cached: true
      stdout: |
        Change Dir: '/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel/CMakeFiles/CMakeScratch/TryCompile-fLbSjo'
        
        Run Build Command(s): /home/nicolas/.local/share/vcpkg/downloads/tools/ninja-1.13.2-linux/ninja -v cmTC_9ac65
        [1/2] /usr/bin/cc   -fPIC -DCHECK_FUNCTION_EXISTS=remove -o CMakeFiles/cmTC_9ac65.dir/CheckFunctionExists.c.o -c /home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel/CMakeFiles/CMakeScratch/TryCompile-fLbSjo/CheckFunctionExists.c
        [2/2] : && /usr/bin/cc -fPIC -DCHECK_FUNCTION_EXISTS=remove  CMakeFiles/cmTC_9ac65.dir/CheckFunctionExists.c.o -o cmTC_9ac65   && :
        
      exitCode: 0
  -
    kind: "try_compile-v1"
    backtrace:
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/CheckFunctionExists.cmake:93 (try_compile)"
      - "/home/nicolas/.local/share/vcpkg/downloads/tools/cmake-3.31.10-linux/cmake-3.31.10-linux-x86_64/share/cmake-3.31/Modules/FindX11.cmake:740 (check_function_exists)"
      - "/home/nicolas/.local/share/vcpkg/scripts/buildsystems/vcpkg.cmake:908 (_find_package)"
      - "src/CMakeLists.txt:181 (find_package)"
    checks:
      - "Looking for shmat"
    directories:
      source: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel/CMakeFiles/CMakeScratch/TryCompile-LRNAxZ"
      binary: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel/CMakeFiles/CMakeScratch/TryCompile-LRNAxZ"
    cmakeVariables:
      CMAKE_C_FLAGS: "-fPIC"
      CMAKE_C_FLAGS_DEBUG: "-g"
      CMAKE_EXE_LINKER_FLAGS: ""
      CMAKE_MODULE_PATH: "/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/src/3.4-2448ff4533.clean/CMake/modules"
      VCPKG_CHAINLOAD_TOOLCHAIN_FILE: "/home/nicolas/.local/share/vcpkg/scripts/toolchains/linux.cmake"
      VCPKG_CRT_LINKAGE: "dynamic"
      VCPKG_CXX_FLAGS: ""
      VCPKG_CXX_FLAGS_DEBUG: ""
      VCPKG_CXX_FLAGS_RELEASE: ""
      VCPKG_C_FLAGS: ""
      VCPKG_C_FLAGS_DEBUG: ""
      VCPKG_C_FLAGS_RELEASE: ""
      VCPKG_INSTALLED_DIR: "/home/nicolas/WEMU/vcpkg_installed"
      VCPKG_LINKER_FLAGS: ""
      VCPKG_LINKER_FLAGS_DEBUG: ""
      VCPKG_LINKER_FLAGS_RELEASE: ""
      VCPKG_PREFER_SYSTEM_LIBS: "OFF"
      VCPKG_TARGET_ARCHITECTURE: "x64"
      VCPKG_TARGET_TRIPLET: "x64-linux"
      Z_VCPKG_ROOT_DIR: "/home/nicolas/.local/share/vcpkg"
    buildResult:
      variable: "CMAKE_HAVE_SHMAT"
      cached: true
      stdout: |
        Change Dir: '/home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel/CMakeFiles/CMakeScratch/TryCompile-LRNAxZ'
        
        Run Build Command(s): /home/nicolas/.local/share/vcpkg/downloads/tools/ninja-1.13.2-linux/ninja -v cmTC_51287
        [1/2] /usr/bin/cc   -fPIC -DCHECK_FUNCTION_EXISTS=shmat -o CMakeFiles/cmTC_51287.dir/CheckFunctionExists.c.o -c /home/nicolas/.local/share/vcpkg/buildtrees/glfw3/x64-linux-rel/CMakeFiles/CMakeScratch/TryCompile-LRNAxZ/CheckFunctionExists.c
        [2/2] : && /usr/bin/cc -fPIC -DCHECK_FUNCTION_EXISTS=shmat  CMakeFiles/cmTC_51287.dir/CheckFunctionExists.c.o -o cmTC_51287   && :
        
      exitCode: 0
...
```
</details>

**Additional context**

<details><summary>vcpkg.json</summary>

```
{
  "dependencies": [
    "xorg-macros",
    "pkgconf",
    "glfw3",
    "glm"
  ]
}

```
</details>
