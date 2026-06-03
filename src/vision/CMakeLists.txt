add_library(video2vec-vision frame_selector.cpp patch_extractor.cpp)
add_library(video2vec::vision ALIAS video2vec-vision)
target_include_directories(video2vec-vision
    PUBLIC $<BUILD_INTERFACE:${VIDEO2VEC_PUBLIC_INCLUDE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${VIDEO2VEC_PRIVATE_INCLUDE_DIR})
if(VIPS_FOUND)
    target_compile_definitions(video2vec-vision PRIVATE HAS_VIPS=1)
    target_link_libraries(video2vec-vision PUBLIC video2vec::core video2vec::ocr PRIVATE ${VIPS_LIBRARIES})
    target_include_directories(video2vec-vision PRIVATE ${VIPS_INCLUDE_DIRS})
else()
    target_link_libraries(video2vec-vision PUBLIC video2vec::core video2vec::ocr)
endif()
set_target_properties(video2vec-vision PROPERTIES
    OUTPUT_NAME video2vec-vision
    EXPORT_NAME vision
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
install(TARGETS video2vec-vision EXPORT video2vecTargets LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
