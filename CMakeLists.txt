cmake_minimum_required(VERSION 3.20)
project(fiber_runtime LANGUAGES CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -fsanitize=address,undefined")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
  set(ASM_SOURCE src/context_switch_arm64.S)
  message(STATUS "Target: ARM64 (Apple Silicon)")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
  set(ASM_SOURCE src/context_switch_x86_64.S)
  message(STATUS "Target: x86_64")
else()
  message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

add_executable(fiber_runtime
  ${ASM_SOURCE}
  src/fiber.cpp
  src/scheduler.cpp
  src/main.cpp
)
target_include_directories(fiber_runtime PRIVATE src)
find_package(Threads REQUIRED)
target_link_libraries(fiber_runtime PRIVATE Threads::Threads)
