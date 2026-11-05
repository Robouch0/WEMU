# Pin the upstream release and its contents; no system audio SDK is required.
include(FetchContent)
set(SOUNDSTRETCH OFF)
set(SOUNDTOUCH_DLL OFF)
set(INTEGER_SAMPLES OFF)
set(OPENMP OFF)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
FetchContent_Declare(wemu_soundtouch
    URL https://codeberg.org/soundtouch/soundtouch/archive/2.4.0.tar.gz
    URL_HASH SHA256=b54ca9724afcf0b8c5326a0afb1b1676f2b892ee01c57d5312232ce4bc27a077
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(wemu_soundtouch)
