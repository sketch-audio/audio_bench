cmake_minimum_required(VERSION 3.30)

project(audio_bench VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(audio_bench INTERFACE)
target_include_directories(audio_bench INTERFACE include)