from conan import ConanFile
from conan.tools.cmake import cmake_layout

class FinConan(ConanFile):
    settings = "os", "arch", "compiler", "build_type"

    requires = (
        "llvm-core/19.1.7",
        "fmt/10.2.1",
        "gtest/1.14.0",
    )

    generators = ("CMakeDeps", "CMakeToolchain")
    build_requires = ["ninja/1.11.1"]  # <-- optional, Conan installs Ninja

    def layout(self):
        cmake_layout(self)
    def generate(self):
        tc = CMakeToolchain(self)
        tc.generator = "Ninja"
        tc.generate()
