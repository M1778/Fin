from conan import ConanFile
from conan.tools.cmake import cmake_layout

class FinConan(ConanFile):
    name = "fin"
    version = "0.1"
    settings = "os", "arch", "compiler", "build_type"

    requires = (
        #"llvm-core/19.1.7", -> DO NOT USE FOR LINUX
        "fmt/10.2.1",
        "gtest/1.14.0",
    )

    generators = ("CMakeDeps", "CMakeToolchain")
    build_requires = ["ninja/1.11.1"]

    def layout(self):
        cmake_layout(self)
