#!/usr/bin/env bash

# ==============================================================================
# Fin Compiler - Build Script
# ==============================================================================
# Mirrors what .github/workflows/ci.yml does, so a green local run and a green
# CI run mean the same thing.

set -euo pipefail

# --- Colors for Output ---
if [[ -t 1 && -z "${NO_COLOR:-}" ]]; then
  BOLD="\033[1m"; RED="\033[1;31m"; GREEN="\033[1;32m"
  YELLOW="\033[1;33m"; BLUE="\033[1;34m"; RESET="\033[0m"
else
  BOLD=""; RED=""; GREEN=""; YELLOW=""; BLUE=""; RESET=""
fi

# --- Configuration ---
REPO_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$REPO_ROOT/build"
BUILD_TYPE="Debug"
CONAN_PROFILE="$REPO_ROOT/conan/profiles/fin"
RUN_TESTS=true
CLEAN_BUILD=false
VERBOSE=false
WITH_LLVM=OFF

# --- Helper Functions ---
log_info() { echo -e "${BLUE}[INFO]${RESET} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${RESET} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${RESET} $1"; }
log_error() { echo -e "${RED}[ERROR]${RESET} $1" >&2; }

print_banner() {
  echo -e "${BOLD}==============================================${RESET}"
  echo -e "${BOLD}          Fin Compiler Build System           ${RESET}"
  echo -e "${BOLD}==============================================${RESET}"
}

check_dependency() {
  if ! command -v "$1" &>/dev/null; then
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
  echo "  --with-llvm     Link LLVM (ADR 0010 pins the major; see CMakeLists.txt)"
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
  --with-llvm) WITH_LLVM=ON ;;
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
check_dependency conan
check_dependency bison
check_dependency flex

if command -v ninja &>/dev/null; then
  GENERATOR="Ninja"
else
  log_warn "ninja not found; falling back to the default CMake generator."
  GENERATOR=""
fi

# 2. Clean if requested
if [ "$CLEAN_BUILD" = true ]; then
  log_warn "Cleaning build directory..."
  rm -rf "$BUILD_DIR"
fi

# 3. Conan Install
#    conan/profiles/fin does `include(default)`, so `default` has to exist. It is
#    machine-specific (it records the local compiler and its version) which is
#    exactly why it is detected here rather than committed.
if ! conan profile path default &>/dev/null; then
  log_warn "No default Conan profile. Detecting..."
  conan profile detect --force
fi

log_info "Installing dependencies with Conan ($BUILD_TYPE)..."
conan install "$REPO_ROOT" \
  --output-folder="$BUILD_DIR" \
  --build=missing \
  -pr:a="$CONAN_PROFILE" \
  -s build_type="$BUILD_TYPE"

TOOLCHAIN="$BUILD_DIR/build/$BUILD_TYPE/generators/conan_toolchain.cmake"
if [ ! -f "$TOOLCHAIN" ]; then
  # Single-config generators put the generators folder one level up.
  TOOLCHAIN="$BUILD_DIR/build/generators/conan_toolchain.cmake"
fi
if [ ! -f "$TOOLCHAIN" ]; then
  log_error "Conan toolchain not found under $BUILD_DIR/build."
  exit 1
fi

# 4. CMake Configure
log_info "Configuring CMake ($BUILD_TYPE)..."
CMAKE_ARGS=(
  -S "$REPO_ROOT"
  -B "$BUILD_DIR"
  -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN"
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
  -DFIN_WITH_LLVM="$WITH_LLVM"
)
[ -n "$GENERATOR" ] && CMAKE_ARGS+=(-G "$GENERATOR")
cmake "${CMAKE_ARGS[@]}"

# 5. Build
log_info "Compiling..."
BUILD_ARGS=(--build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel)
[ "$VERBOSE" = true ] && BUILD_ARGS+=(--verbose)
cmake "${BUILD_ARGS[@]}"
log_success "Build complete!"

# 6. Run Tests
if [ "$RUN_TESTS" = true ]; then
  log_info "Running tests..."
  ctest --test-dir "$BUILD_DIR" --build-config "$BUILD_TYPE" --output-on-failure
  log_success "All tests passed!"
else
  log_info "Skipping tests."
fi

echo ""
log_success "Fin Compiler is ready at: $BUILD_DIR/finc"
