cmake_minimum_required(VERSION 3.21)

project(tinlog VERSION 0.1.0 LANGUAGES CXX)

add_library(tinlog INTERFACE)

target_compile_features(tinlog INTERFACE cxx_std_23)

target_include_directories(tinlog INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

option(TINLOG_BUILD_EXAMPLE "Build tinlog test application" OFF)

if(TINLOG_BUILD_EXAMPLE)
  add_executable(tinlog_example main.cpp)

  target_link_libraries(tinlog_example PRIVATE tinlog)
endif()
