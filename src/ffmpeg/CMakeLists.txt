add_library(video2vec-ffmpeg
    demuxer.cpp
    decoder.cpp
    media.cpp
    audio_buffer.cpp
)

add_library(video2vec::ffmpeg ALIAS video2vec-ffmpeg)

target_include_directories(video2vec-ffmpeg
    PUBLIC
        $<BUILD_INTERFACE:${VIDEO2VEC_PUBLIC_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${VIDEO2VEC_PRIVATE_INCLUDE_DIR}
        ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(video2vec-ffmpeg
    PUBLIC video2vec::core
    PRIVATE ${FFMPEG_LIBRARIES}
)

target_compile_options(video2vec-ffmpeg PRIVATE ${FFMPEG_CFLAGS_OTHER})

set_target_properties(video2vec-ffmpeg PROPERTIES
    OUTPUT_NAME video2vec-ffmpeg
    EXPORT_NAME ffmpeg
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)

install(TARGETS video2vec-ffmpeg
    EXPORT video2vecTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
