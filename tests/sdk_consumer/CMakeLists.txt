cmake_minimum_required(VERSION 3.25)
project(video2vec_consumer_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(video2vec REQUIRED)

add_executable(consumer_test consumer_test.cpp)
target_link_libraries(consumer_test PRIVATE video2vec::ffmpeg video2vec::core)
