add_library(video2vec-windowing windowing.cpp)
add_library(video2vec::windowing ALIAS video2vec-windowing)
target_include_directories(video2vec-windowing
    PUBLIC $<BUILD_INTERFACE:${VIDEO2VEC_PUBLIC_INCLUDE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${VIDEO2VEC_PRIVATE_INCLUDE_DIR})
target_link_libraries(video2vec-windowing PUBLIC video2vec::core)
set_target_properties(video2vec-windowing PROPERTIES
    OUTPUT_NAME video2vec-windowing
    EXPORT_NAME windowing
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
install(TARGETS video2vec-windowing EXPORT video2vecTargets LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
