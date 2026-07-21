# nt4-toolchain.cmake — CMake cross toolchain for dependencies (mbedTLS, libwebp).
# Usage: cmake -B build-nt4 -DCMAKE_TOOLCHAIN_FILE=../../nt4-toolchain.cmake ...
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR i686)
set(CMAKE_C_COMPILER   i686-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER i686-w64-mingw32-g++)
set(CMAKE_RC_COMPILER  i686-w64-mingw32-windres)

# Cap the API surface at NT4 so post-NT4 symbols fail at header stage.
add_compile_definitions(_WIN32_WINNT=0x0400 WINVER=0x0400)

# Emit NT4-loadable PE (subsystem/OS version 4.0), no libgcc DLL dependency.
add_link_options(
  -Wl,--major-subsystem-version=4,--minor-subsystem-version=0
  -Wl,--major-os-version=4,--minor-os-version=0
  -static -static-libgcc)

# Only look for libs/headers in the target sysroot, not the host.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
