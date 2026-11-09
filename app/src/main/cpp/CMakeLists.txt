cmake_minimum_required(VERSION 3.22.1)
project(b3jni CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The portable engine lives at the repository root and is shared with the
# host tests / demo renderer.
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../../../engine
                 ${CMAKE_CURRENT_BINARY_DIR}/b3engine)

find_package(oboe REQUIRED CONFIG)

add_library(b3jni SHARED b3_jni.cpp)

target_compile_options(b3jni PRIVATE -O3 -fno-math-errno -Wall -Wextra
  -Wno-unused-parameter)

target_link_libraries(b3jni PRIVATE
  b3engine
  oboe::oboe
  android
  log)
