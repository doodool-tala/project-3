cmake_minimum_required(VERSION 3.16)
project(b3engine CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(b3engine STATIC
  src/model.cpp
  src/tonewheel.cpp
  src/scanner.cpp
  src/percussion.cpp
  src/drive.cpp
  src/leslie.cpp
  src/reverb.cpp
  src/organ.cpp
)

target_include_directories(b3engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(b3engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(NOT MSVC)
  target_compile_options(b3engine PRIVATE -O3 -fno-math-errno -Wall -Wextra
    -Wno-unused-parameter)
endif()
