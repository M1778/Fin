#!/bin/bash

# ==============================================================================
# Fin Compiler - Advanced Build Script
# ==============================================================================

# Exit immediately if a command exits with a non-zero status
set -e

# --- Colors for Output ---
BOLD="\033[1m"
RED="\033[1;31m"
GREEN="\033[1;32m"
YELLOW="\033[1;33m"
BLUE="\033[1;34m"
RESET="\033[0m"

# --- Configuration ---
BUILD_DIR="build"
BUILD_TYPE="Debug"
RUN_TESTS=true
CLEAN_BUILD=false
VERBOSE=false

# --- Helper Functions ---
log_info() { echo -e "${BLUE}[INFO]${RESET} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${RESET} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${RESET} $1"; }
log_error() { echo -e "${RED}[ERROR]${RESET} $1"; }

print_banner() {
  echo -e "${BOLD}==============================================${RESET}"
  echo -e "${BOLD}       Fin Compiler Build System (Linux)      ${RESET}"
  echo -e "${BOLD}==============================================${RESET}"
}

check_dependency() {
  if ! command -v $1 &>/dev/null; then
    log_error "$1 could not be found. Please install it."
    exit 1
  fi
}

usage() {
  echo "Usage: ./build.sh [OPTIONS]"
  echo "Options:"
  echo "  --release       Build in Release mode (default: Debug)"
  echo "  --clean         Clean build directory before building"
  echo "  --no-test       Skip running tests after build"
  echo "  --verbose       Enable verbose build output"
  echo "  --help          Show this help message"
  exit 0
}

# --- Parse Arguments ---
while [[ "$#" -gt 0 ]]; do
  case $1 in
  --release) BUILD_TYPE="Release" ;;
  --clean) CLEAN_BUILD=true ;;
  --no-test) RUN_TESTS=false ;;
  --verbose) VERBOSE=true ;;
  --help) usage ;;
  *)
    log_error "Unknown parameter passed: $1"
    usage
    ;;
  esac
  shift
done

# --- Main Execution ---
print_banner

# 1. Check Environment
log_info "Checking dependencies..."
check_dependency cmake
check_dependency uvx conan
check_dependency make
check_dependency g++

# 2. Clean if requested
if [ "$CLEAN_BUILD" = true ]; then
  log_warn "Cleaning build directory..."
  rm -rf $BUILD_DIR
fi

# 3. Setup Build Directory
if [ ! -d "$BUILD_DIR" ]; then
  mkdir -p $BUILD_DIR
  FIRST_RUN=true
else
  FIRST_RUN=false
fi

# 4. Conan Install
log_info "Installing dependencies with Conan..."
# Detect profile if missing
if ! uvx conan profile list | grep -q "fin-debug"; then
  log_warn "No default Conan profile found. Detecting..."
  uvx conan profile detect --force
fi

uvx conan install . --output-folder=. --build=missing -s build_type=$BUILD_TYPE --profile=fin-debug

# 5. CMake Configure
log_info "Configuring CMake ($BUILD_TYPE)..."
cd $BUILD_DIR

CMAKE_ARGS="-DCMAKE_TOOLCHAIN_FILE=Debug/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE -GNinja"
if [ "$FIRST_RUN" = true ] || [ "$CLEAN_BUILD" = true ]; then
  cmake .. $CMAKE_ARGS
else
  # Just refresh
  cmake . $CMAKE_ARGS
fi

# 6. Build
log_info "Compiling..."
if [ "$VERBOSE" = true ]; then
  cmake --build . -- -j$(nproc)
else
  cmake --build . -- -j$(nproc)
fi

if [ $? -eq 0 ]; then
  log_success "Build complete!"
else
  log_error "Build failed."
  exit 1
fi

# 7. Run Tests
if [ "$RUN_TESTS" = true ]; then
  log_info "Running Tests..."

  # Check if test executable exists
  if [ -f "./tests/fin_tests" ]; then
    cd tests
    ./fin_tests
    TEST_EXIT_CODE=$?
    cd ..

    if [ $TEST_EXIT_CODE -eq 0 ]; then
      log_success "All tests passed!"
    else
      log_error "Some tests failed."
      exit $TEST_EXIT_CODE
    fi
  else
    log_warn "Test executable not found. Did the build succeed?"
  fi
else
  log_info "Skipping tests."
fi

echo ""
log_success "Fin Compiler is ready at: ./$BUILD_DIR/finc"
