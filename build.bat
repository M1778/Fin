echo "MSVC environment loaded."
echo "cl location: $(which cl || echo NOT FOUND)"
echo "Installing packages..."
uvx conan install . --output-folder=. -c tools.cmake.cmaketoolchain:generator=Ninja
echo "Using preset..."
cmake --preset conan-release
echo "Building..."
cmake --build --preset conan-release 
