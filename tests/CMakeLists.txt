enable_testing()
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    find_package(googletest QUIET)
endif()
if(NOT GTest_FOUND AND NOT googletest_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)
    if(NOT TARGET GTest::gtest_main)
        add_library(GTest::gtest_main ALIAS gtest_main)
    endif()
endif()

add_executable(test_core unit/test_core.cpp)
target_link_libraries(test_core PRIVATE video2vec::core GTest::gtest_main)
add_executable(test_sync unit/test_sync.cpp)
target_link_libraries(test_sync PRIVATE video2vec::sync GTest::gtest_main)
add_executable(test_windowing unit/test_windowing.cpp)
target_link_libraries(test_windowing PRIVATE video2vec::windowing GTest::gtest_main)
add_executable(test_pipeline integration/test_pipeline.cpp)
target_link_libraries(test_pipeline PRIVATE video2vec::core video2vec::ffmpeg video2vec::vision video2vec::ocr video2vec::flatbuffers GTest::gtest_main)

add_test(NAME core COMMAND test_core)
add_test(NAME sync COMMAND test_sync)
add_test(NAME windowing COMMAND test_windowing)
add_test(NAME pipeline COMMAND test_pipeline)
