cmake_minimum_required(VERSION 3.20)
project(QBMouseFix VERSION 1.0.0 LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: the .asi must not depend on a particular VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Quantum Break is x64-only; configure with -A x64")
endif()

add_library(minhook STATIC
  third_party/minhook/src/buffer.c
  third_party/minhook/src/hook.c
  third_party/minhook/src/trampoline.c
  third_party/minhook/src/hde/hde64.c)
target_include_directories(minhook PUBLIC third_party/minhook/include)

add_library(QBMouseFix SHARED
  src/main.cpp
  src/config.cpp
  src/log.cpp
  src/pattern.cpp
  src/iat.cpp
  src/mousefix.cpp
  src/rawinput.cpp
  src/latency.cpp
  src/version.rc)
configure_file(src/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h @ONLY)
target_include_directories(QBMouseFix PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_link_libraries(QBMouseFix PRIVATE minhook)
target_compile_definitions(QBMouseFix PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_compile_options(QBMouseFix PRIVATE /W4 /permissive- /utf-8)
set_target_properties(QBMouseFix PROPERTIES SUFFIX ".asi")
