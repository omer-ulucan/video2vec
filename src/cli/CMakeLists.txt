add_executable(video2vec-cli video2vec_cli.cpp)
add_executable(vec2index vec2index.cpp)
add_executable(ask ask.cpp)
add_executable(load-to-llm load_to_llm.cpp)
add_executable(qa qa.cpp)

target_link_libraries(video2vec-cli PRIVATE video2vec::ffmpeg video2vec::sync video2vec::windowing video2vec::asr video2vec::ocr video2vec::vision video2vec::embedding video2vec::flatbuffers cxxopts::cxxopts)
target_link_libraries(vec2index PRIVATE video2vec::index video2vec::embedding video2vec::flatbuffers video2vec::query cxxopts::cxxopts)
target_link_libraries(ask PRIVATE video2vec::index video2vec::embedding video2vec::query cxxopts::cxxopts)
target_link_libraries(load-to-llm PRIVATE video2vec::flatbuffers cxxopts::cxxopts nlohmann_json::nlohmann_json)
target_link_libraries(qa PRIVATE video2vec::index video2vec::embedding video2vec::query cxxopts::cxxopts)

set_target_properties(video2vec-cli PROPERTIES OUTPUT_NAME video2vec)
set_target_properties(vec2index PROPERTIES OUTPUT_NAME vec2index)
set_target_properties(ask PROPERTIES OUTPUT_NAME ask)
set_target_properties(load-to-llm PROPERTIES OUTPUT_NAME load-to-llm)
set_target_properties(qa PROPERTIES OUTPUT_NAME qa)

install(TARGETS video2vec-cli vec2index ask load-to-llm qa RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
