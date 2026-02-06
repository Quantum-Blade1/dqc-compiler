cmake_minimum_required(VERSION 3.13)
project(dqc-runtime LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(dqc-run
  tools/dqc-run.cpp
  src/Executor.cpp
  src/QPUSimulator.cpp
  src/MPILayer.cpp
)

target_include_directories(dqc-run PRIVATE include)

add_executable(dqc-compile
  tools/dqc-compile.cpp
)

target_include_directories(dqc-compile PRIVATE include)

# Lightweight flags
if (MSVC)
  target_compile_options(dqc-run PRIVATE /W4 /WX)
  target_compile_options(dqc-compile PRIVATE /W4 /WX)
else()
  target_compile_options(dqc-run PRIVATE -Wall -Wextra -Wpedantic -Werror)
  target_compile_options(dqc-compile PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

install(TARGETS dqc-run dqc-compile RUNTIME DESTINATION bin)
