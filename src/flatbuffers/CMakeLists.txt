add_library(video2vec-flatbuffers packager.cpp)
add_library(video2vec::flatbuffers ALIAS video2vec-flatbuffers)
target_include_directories(video2vec-flatbuffers
    PUBLIC $<BUILD_INTERFACE:${VIDEO2VEC_PUBLIC_INCLUDE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${VIDEO2VEC_PRIVATE_INCLUDE_DIR})
if(FlatBuffers_FOUND OR FLATBUFFERS_FOUND)
    target_link_libraries(video2vec-flatbuffers PUBLIC video2vec::core video2vec::asr video2vec::ocr video2vec::vision video2vec::embedding PRIVATE flatbuffers::flatbuffers)
else()
    target_link_libraries(video2vec-flatbuffers PUBLIC video2vec::core video2vec::asr video2vec::ocr video2vec::vision video2vec::embedding)
endif()
set_target_properties(video2vec-flatbuffers PROPERTIES
    OUTPUT_NAME video2vec-flatbuffers
    EXPORT_NAME flatbuffers
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
install(TARGETS video2vec-flatbuffers EXPORT video2vecTargets LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
