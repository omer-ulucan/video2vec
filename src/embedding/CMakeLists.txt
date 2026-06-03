if(onnxruntime_FOUND OR ONNXRUNTIME_FOUND OR TARGET onnxruntime::onnxruntime)
    set(EMBEDDING_SOURCES onnx_backend.cpp)
    set(EMBEDDING_LIBS onnxruntime::onnxruntime)
else()
    message(FATAL_ERROR "ONNX Runtime not found. Install onnxruntime or set ONNXRUNTIME_ROOT.")
endif()

add_library(video2vec-embedding ${EMBEDDING_SOURCES})
add_library(video2vec::embedding ALIAS video2vec-embedding)
target_include_directories(video2vec-embedding
    PUBLIC $<BUILD_INTERFACE:${VIDEO2VEC_PUBLIC_INCLUDE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${VIDEO2VEC_PRIVATE_INCLUDE_DIR})
target_link_libraries(video2vec-embedding PUBLIC video2vec::core PRIVATE ${EMBEDDING_LIBS})
set_target_properties(video2vec-embedding PROPERTIES
    OUTPUT_NAME video2vec-embedding
    EXPORT_NAME embedding
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
install(TARGETS video2vec-embedding EXPORT video2vecTargets LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
