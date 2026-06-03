# video2vec API Reference

## Core API

### Result<T, E>

```cpp
auto result = some_operation();
if (result) {
    auto value = result.value();
} else {
    auto error = result.error();
    std::cerr << error.message << "\n";
}
```

### Logger

```cpp
video2vec::core::Logger::initialize("myapp");
video2vec::core::Logger::info("message", {module="my_module"});
```

### Config

```cpp
auto cfg = video2vec::core::Config::from_yaml("config.yaml");
int window_ms = cfg.get_value<int>("windowing.duration_ms", 45000);
```

## FFmpeg API

### Demuxer

```cpp
video2vec::ffmpeg::Demuxer demuxer;
demuxer.open("video.mp4");
auto props = demuxer.video_properties();
```

### Decoder

```cpp
video2vec::ffmpeg::Decoder decoder;
decoder.initialize(codec_params);
```

## ASR API

```cpp
auto backend = std::make_unique<video2vec::asr::WhisperBackend>();
backend->initialize("ggml-base.bin");
auto result = backend->transcribe(audio_buffer.float_data(), 16000);
```

## OCR API

```cpp
auto backend = std::make_unique<video2vec::ocr::TesseractBackend>();
backend->initialize("eng+tur");
auto result = backend->recognize(image_data, width, height, 3);
```

## Embedding API

```cpp
auto backend = std::make_unique<video2vec::embedding::ONNXBackend>();
backend->initialize("model.onnx", config);
auto embeddings = backend->encode_images(image_data, width, height);
```

## Vector Store API

```cpp
auto store = std::make_shared<video2vec::index::FAISSStore>();
store->initialize("index.faiss", 512, "cosine");
store->add(records);
auto results = store->search(query_vector, config);
```

## Query API

```cpp
video2vec::query::QueryEngine engine(store, embedder);
auto results = engine.search({"what is marginal cost", 8});
```
