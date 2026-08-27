#!/usr/bin/env bash

# ==============================================================================
# Fin Compiler - Build Script
# ==============================================================================
# Mirrors what .github/workflows/ci.yml does, so a green local run and a green
# CI run mean the same thing.  That now includes the backend: this script used to
# default WITH_LLVM=OFF while CI's build-script job ran `./build.sh --release`,
# so the one job whose whole purpose was "build.sh runs as committed" was the one
# job that built a compiler with no backend.
#
# Measured here, with `--no-llvm` standing in for that old default: 1257 tests,
# 961 passed, 295 skipped, 1 FAILED.  The 295 are every _Codegen suite, behind
# BACKEND_TEST, skipping with a reason ctest never echoes.  The 1 is
# MachineContract.DashOProducesTheNamedExecutable (tests/test_cli.cpp:182),
# which is not behind that macro and so fails honestly, and ctest then exits 8.
# So that job cannot have reported green as committed -- but what it reported was
# a broken `-o`, which reads as a CLI bug, not as "this compiler has no backend".
# No CI run has ever been observed from this machine, in either direction; the
# claim above is about the local build, and ci.yml now checks the rest itself.

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
# ON, matching CMakeLists.txt's own default and ADR 0010's "required from wave 5".
# `--no-llvm` is the way to say otherwise, and it says it out loud.
WITH_LLVM=ON

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
  echo "  --with-llvm     Link LLVM (the default; ADR 0010 pins the major)"
  echo "  --no-llvm       Build without a backend: CodeGen_Stub.cpp refuses every"
  echo "                  -o, and every codegen test SKIPs. Not a build to judge"
  echo "                  the compiler by."
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
  --no-llvm) WITH_LLVM=OFF ;;
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

# The pinned LLVM major, read out of CMakeLists.txt rather than repeated here.
# ADR 0010 puts the number in exactly one place and CMakeLists.txt is that place;
# a copy in this script is a copy that can drift, and a build script enforcing
# last month's pin enforces nothing.  It is then passed back on the command line,
# so what gets built against is a value this script named and not a default a
# later edit can move underneath it.
LLVM_MAJOR="$(sed -n 's/^set(FIN_LLVM_MAJOR \([0-9][0-9]*\) CACHE STRING.*/\1/p' "$REPO_ROOT/CMakeLists.txt")"
if [ -z "$LLVM_MAJOR" ]; then
  log_error "Could not read FIN_LLVM_MAJOR out of CMakeLists.txt."
  log_error "ADR 0010's pin lives there; this script will not guess it."
  exit 1
fi

# --- Locate the pinned LLVM ---------------------------------------------------
# find_package(LLVM CONFIG) searches the default prefixes, which is enough where
# the distro installs LLVM into /usr.  A second LLVM under a versioned prefix --
# apt.llvm.org's /usr/lib/llvm-N, Homebrew's keg -- is not on that path and has
# to be pointed at, and llvm-config knows where its own cmake directory is, so
# ask it rather than guessing the layout.
#
# The version is deliberately NOT judged here.  CMakeLists.txt owns the pin and
# says so in a comment; a second check in this script would be a second thing to
# keep in step, and it would report the same disagreement in a worse place.
LLVM_CMAKE_DIR=""
if [ "$WITH_LLVM" = ON ]; then
  for cfg in "llvm-config-$LLVM_MAJOR" llvm-config; do
    if command -v "$cfg" &>/dev/null; then
      LLVM_CMAKE_DIR="$("$cfg" --cmakedir)"
      log_info "LLVM $("$cfg" --version) via $(command -v "$cfg") -> $LLVM_CMAKE_DIR"
      break
    fi
  done
  if [ -z "$LLVM_CMAKE_DIR" ]; then
    log_warn "No llvm-config on PATH; leaving LLVM_DIR to find_package to resolve."
  fi
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
  -DFIN_LLVM_MAJOR="$LLVM_MAJOR"
)
[ -n "$LLVM_CMAKE_DIR" ] && CMAKE_ARGS+=(-DLLVM_DIR="$LLVM_CMAKE_DIR")
[ -n "$GENERATOR" ] && CMAKE_ARGS+=(-G "$GENERATOR")
cmake "${CMAKE_ARGS[@]}"

# A reconfigure of an existing build directory keeps cache entries it was not
# asked to change, so "I passed -DFIN_WITH_LLVM=ON" and "this build has a
# backend" are two different claims.  Read the cache back and check the second
# one, because the failure this guards against has already happened once: a
# build/ whose CMakeCache said OFF and a suite that read 961 passed / 1 failed /
# 285 skipped (recorded in ad795f7; 285 became 295 when e00034f added ten tests).
# Not one of those 285 lines, and not the one failure either, said "no backend".
CACHE="$BUILD_DIR/CMakeCache.txt"
CONFIGURED_LLVM="$(sed -n 's/^FIN_WITH_LLVM:BOOL=//p' "$CACHE")"
CONFIGURED_MAJOR="$(sed -n 's/^FIN_LLVM_MAJOR:STRING=//p' "$CACHE")"
if [ "$CONFIGURED_LLVM" != "$WITH_LLVM" ] || [ "$CONFIGURED_MAJOR" != "$LLVM_MAJOR" ]; then
  log_error "$CACHE disagrees with what was asked for:"
  log_error "  FIN_WITH_LLVM  asked $WITH_LLVM, cache says ${CONFIGURED_LLVM:-<unset>}"
  log_error "  FIN_LLVM_MAJOR asked $LLVM_MAJOR, cache says ${CONFIGURED_MAJOR:-<unset>}"
  log_error "Delete $BUILD_DIR or run with --clean."
  exit 1
fi
if [ "$WITH_LLVM" = ON ]; then
  log_success "Backend: LLVM $LLVM_MAJOR (FIN_WITH_LLVM=ON)"
else
  log_warn "No backend (FIN_WITH_LLVM=OFF): every codegen test will SKIP."
fi

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
