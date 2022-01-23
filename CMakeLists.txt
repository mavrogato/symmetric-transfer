
cmake_minimum_required(VERSION 3.14)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_COMPILER "icpx")

set(PROJ "symmetric-transfer")
project(${PROJ})

add_executable(${PROJ} main.cc)

target_compile_options(${PROJ}
  PRIVATE
  -std=c++20
  -fcoroutines-ts)

add_custom_target(run
  DEPENDS ${PROJ}
  COMMAND ${PROJ})
