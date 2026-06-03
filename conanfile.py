from conan import ConanFile
from conan.tools.cmake import CMakeToolchain, CMake, cmake_layout

class Video2VecConan(ConanFile):
    name = "video2vec"
    version = "0.1.0"
    settings = "os", "compiler", "build_type", "arch"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {"shared": True, "fPIC": True}
    generators = "CMakeDeps"
    exports_sources = "CMakeLists.txt", "src/*", "include/*", "cmake/*", "tests/*", "examples/*", "benchmarks/*"

    def requirements(self):
        self.requires("spdlog/1.14.1")
        self.requires("nlohmann_json/3.11.3")
        self.requires("yaml-cpp/0.8.0")
        self.requires("ffmpeg/6.1")
        self.requires("gtest/1.14.0")
        self.requires("benchmark/1.8.3")

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["video2vec-core", "video2vec-ffmpeg", "video2vec-sync",
                              "video2vec-windowing", "video2vec-asr", "video2vec-ocr",
                              "video2vec-vision", "video2vec-embedding", "video2vec-flatbuffers",
                              "video2vec-index", "video2vec-query"]
