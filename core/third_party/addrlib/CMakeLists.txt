add_library(wemu_addrlib STATIC
    src/addrinterface.cpp
    src/core/addrelemlib.cpp
    src/core/addrlib.cpp
    src/core/addrobject.cpp
    src/r600/r600addrlib.cpp
)
target_include_directories(wemu_addrlib SYSTEM PUBLIC include PRIVATE src)
