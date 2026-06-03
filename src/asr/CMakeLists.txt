if(TARGET whisper)
    set(ASR_SOURCES whisper_backend.cpp)
    set(ASR_LIBS whisper)
else()
    set(ASR_SOURCES whisper_backend_stub.cpp)
    set(ASR_LIBS)
endif()

add_library(video2vec-asr ${ASR_SOURCES})
add_library(video2vec::asr ALIAS video2vec-asr)
target_include_directories(video2vec-asr
    PUBLIC $<BUILD_INTERFACE:${VIDEO2VEC_PUBLIC_INCLUDE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${VIDEO2VEC_PRIVATE_INCLUDE_DIR})
target_link_libraries(video2vec-asr PUBLIC video2vec::core video2vec::ffmpeg PRIVATE ${ASR_LIBS})
set_target_properties(video2vec-asr PROPERTIES OUTPUT_NAME video2vec-asr VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
install(TARGETS video2vec-asr EXPORT video2vecTargets LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
