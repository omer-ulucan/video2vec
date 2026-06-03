add_library(video2vec-core
    logger.cpp
    thread_pool.cpp
    cancellation.cpp
    config.cpp
    metrics.cpp
    memory.cpp
)

add_library(video2vec::core ALIAS video2vec-core)

target_include_directories(video2vec-core
    PUBLIC
        $<BUILD_INTERFACE:${VIDEO2VEC_PUBLIC_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${VIDEO2VEC_PRIVATE_INCLUDE_DIR}
)

target_link_libraries(video2vec-core
    PUBLIC Threads::Threads
    PRIVATE spdlog::spdlog nlohmann_json::nlohmann_json yaml-cpp
)

set_target_properties(video2vec-core PROPERTIES
    OUTPUT_NAME video2vec-core
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(TARGETS video2vec-core
    EXPORT video2vecTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
