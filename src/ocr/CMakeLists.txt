if(Tesseract_FOUND OR TESSERACT_FOUND OR TARGET Tesseract::libtesseract)
    set(OCR_SOURCES tesseract_backend.cpp)
    if(TARGET Tesseract::libtesseract)
        set(OCR_LIBS Tesseract::libtesseract)
    else()
        set(OCR_LIBS Tesseract::libtesseract leptonica)
    endif()
else()
    message(FATAL_ERROR "Tesseract not found. Install tesseract and leptonica.")
endif()

add_library(video2vec-ocr ${OCR_SOURCES})
add_library(video2vec::ocr ALIAS video2vec-ocr)
target_include_directories(video2vec-ocr
    PUBLIC $<BUILD_INTERFACE:${VIDEO2VEC_PUBLIC_INCLUDE_DIR}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${VIDEO2VEC_PRIVATE_INCLUDE_DIR})
target_link_libraries(video2vec-ocr PUBLIC video2vec::core PRIVATE ${OCR_LIBS})
set_target_properties(video2vec-ocr PROPERTIES
    OUTPUT_NAME video2vec-ocr
    EXPORT_NAME ocr
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
)
install(TARGETS video2vec-ocr EXPORT video2vecTargets LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
