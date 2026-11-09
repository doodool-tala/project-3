cmake_minimum_required(VERSION 3.16)
project(b3physical CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_subdirectory(engine)

option(B3_BUILD_TOOLS "Build host test/bench and demo renderer" ON)
if(B3_BUILD_TOOLS)
  enable_testing()

  add_executable(b3_tests tools/b3_tests.cpp)
  target_link_libraries(b3_tests PRIVATE b3engine)
  add_test(NAME b3_tests COMMAND b3_tests)

  add_executable(b3_render tools/render_demo.cpp)
  target_link_libraries(b3_render PRIVATE b3engine)
endif()
