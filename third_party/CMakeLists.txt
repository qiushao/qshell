set(CMAKE_CXX_CLANG_TIDY "" CACHE STRING "" FORCE)  # third party code, disable Clang-Tidy

add_subdirectory(utf8proc-2.11.2)
add_subdirectory(qtermwidget)
add_subdirectory(ptyqt)
add_subdirectory(lua-5.4.8)
add_subdirectory(sol2-3.3.0)
if (CMAKE_SYSTEM_NAME MATCHES "Windows")
    set(CRYPTO_BACKEND "WinCNG" CACHE STRING "" FORCE)
else ()
    set(CRYPTO_BACKEND "OpenSSL" CACHE STRING "" FORCE)
endif ()
# Upstream SSH integration tests need their own server; keep qshell's tests separate.
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(BUILD_TESTING OFF)
add_subdirectory(libssh2-1.11.1)
