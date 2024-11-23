cmake_minimum_required(VERSION 3.20)
project(fiber_runtime LANGUAGES CXX ASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -fsanitize=address,undefined")

option(FIBER_TSAN "Build TSAN configuration for stress tests" OFF)
if(FIBER_TSAN)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=thread -g -O1")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=thread")
endif()

# Select architecture-specific assembly file
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
  set(ASM_SOURCE src/context_switch_arm64.S)
  message(STATUS "Target: ARM64 (Apple Silicon)")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
  set(ASM_SOURCE src/context_switch_x86_64.S)
  message(STATUS "Target: x86_64")
else()
  message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
endif()

set(FIBER_CORE_SOURCES
  ${ASM_SOURCE}
  src/fiber.cpp
  src/scheduler.cpp
)

add_executable(fiber_runtime
  ${FIBER_CORE_SOURCES}
  src/main.cpp
)

target_include_directories(fiber_runtime PRIVATE src)

find_package(Threads REQUIRED)
target_link_libraries(fiber_runtime PRIVATE Threads::Threads)

# Catch2 (test-only dependency)
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.5.4
)
FetchContent_MakeAvailable(Catch2)

add_executable(fp_abi_test
  ${FIBER_CORE_SOURCES}
  tests/fp_abi_test.cpp
)
target_include_directories(fp_abi_test PRIVATE src)
target_link_libraries(fp_abi_test PRIVATE Threads::Threads Catch2::Catch2WithMain)

add_executable(deque_stress_test
  tests/deque_stress_test.cpp
)
target_include_directories(deque_stress_test PRIVATE src)
target_link_libraries(deque_stress_test PRIVATE Catch2::Catch2WithMain)

enable_testing()
add_test(NAME fp_abi_test COMMAND fp_abi_test)
add_test(NAME deque_stress_test COMMAND deque_stress_test)

add_custom_target(check
  COMMAND ctest --output-on-failure
  DEPENDS fp_abi_test deque_stress_test
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Running ASAN/UBSAN-friendly tests (Debug build recommended)"
)

add_custom_target(tsan-check
  COMMAND deque_stress_test
  DEPENDS deque_stress_test
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Run after cmake -DFIBER_TSAN=ON"
)

# Convenience targets
add_custom_target(run
  COMMAND fiber_runtime --workers 4 --fibers 10000 --yield-every 1000 --rounds 10
  DEPENDS fiber_runtime
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Running default benchmark (4 workers, 10 000 fibers)"
)

add_custom_target(latency
  COMMAND fiber_runtime --latency-probe
  DEPENDS fiber_runtime
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Measuring raw context-switch latency"
)

add_custom_target(stress
  COMMAND fiber_runtime --workers 8 --fibers 50000 --yield-every 500 --rounds 20
  DEPENDS fiber_runtime
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Stress test (8 workers, 50 000 fibers)"
)

add_custom_target(grow-stress
  COMMAND fiber_runtime --workers 8 --fibers 100000 --yield-every 100 --rounds 5
  DEPENDS fiber_runtime
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  COMMENT "Deque growth stress (100 000 fibers)"
)