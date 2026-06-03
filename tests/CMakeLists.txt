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

add_executable(test_real_backends integration/test_real_backends.cpp)
target_compile_definitions(test_real_backends PRIVATE TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(test_real_backends PRIVATE video2vec::asr video2vec::ocr video2vec::embedding video2vec::ffmpeg GTest::gtest_main)

add_executable(test_e2e integration/test_e2e.cpp)
target_compile_definitions(test_e2e PRIVATE TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(test_e2e PRIVATE ${VIDEO2VEC_PRIVATE_INCLUDE_DIR})
target_link_libraries(test_e2e PRIVATE video2vec::asr video2vec::ocr video2vec::embedding video2vec::ffmpeg GTest::gtest_main)

add_test(NAME core COMMAND test_core)
add_test(NAME sync COMMAND test_sync)
add_test(NAME windowing COMMAND test_windowing)
add_test(NAME pipeline COMMAND test_pipeline)
add_test(NAME real_backends COMMAND test_real_backends)
add_test(NAME e2e COMMAND test_e2e)

# SDK consumer test: verify find_package(video2vec) works after install
set(SDK_CONSUMER_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/sdk_consumer_install")
set(SDK_CONSUMER_BUILD "${CMAKE_CURRENT_BINARY_DIR}/sdk_consumer_build")
add_test(NAME sdk_consumer_install
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${SDK_CONSUMER_PREFIX}
)
add_test(NAME sdk_consumer_configure
    COMMAND ${CMAKE_COMMAND}
        -S ${CMAKE_CURRENT_SOURCE_DIR}/sdk_consumer
        -B ${SDK_CONSUMER_BUILD}
        -DCMAKE_PREFIX_PATH=${SDK_CONSUMER_PREFIX}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
    )
add_test(NAME sdk_consumer_build
    COMMAND ${CMAKE_COMMAND} --build ${SDK_CONSUMER_BUILD}
)
add_test(NAME sdk_consumer_run
    COMMAND ${SDK_CONSUMER_BUILD}/consumer_test
)
set_tests_properties(sdk_consumer_configure PROPERTIES DEPENDS sdk_consumer_install)
set_tests_properties(sdk_consumer_build PROPERTIES DEPENDS sdk_consumer_configure)
set_tests_properties(sdk_consumer_run PROPERTIES DEPENDS sdk_consumer_build)
