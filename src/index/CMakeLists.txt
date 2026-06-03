add_library(video2vec-index faiss_store.cpp)
add_library(video2vec::index ALIAS video2vec-index)
target_include_directories(video2vec-index
    PUBLIC $<BUILD_INTERFACE:${VIDEO2VEC_PUBLIC_INCLUDE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${VIDEO2VEC_PRIVATE_INCLUDE_DIR})
if(faiss_FOUND)
    target_compile_definitions(video2vec-index PRIVATE HAS_FAISS=1)
    target_link_libraries(video2vec-index PUBLIC video2vec::core video2vec::embedding PRIVATE faiss)
else()
    target_link_libraries(video2vec-index PUBLIC video2vec::core video2vec::embedding)
endif()
set_target_properties(video2vec-index PROPERTIES OUTPUT_NAME video2vec-index VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
install(TARGETS video2vec-index EXPORT video2vecTargets LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
