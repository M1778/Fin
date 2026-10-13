// libFuzzer harness for the lexer/parser entry point (issue #84).
//
// Mirrors ParserTest::parseString (tests/test_parser.cpp): reset lexer,
// preprocess, install diagnostics, scan, parse, clean up. Anything the
// compiler does with attacker-controlled bytes before semantic analysis runs
// here, which is exactly the surface that parses untrusted source.
//
// Scope: preprocessor + lexer + parser only. No semantic analysis, no codegen
// (no LLVM needed at runtime beyond the link), so an LLVM-attributed report is
// a harness bug, not a compiler finding. Combine with -DFIN_SANITIZE=ON for a
// fully instrumented fin_core; without it only this TU is instrumented and the
// fuzzer still catches crashes, hangs, and OOMs.
//
// Run (Clang only): get seeds from the normative corpus, e.g.
//   cmake -B build-fuzz -S . -DCMAKE_BUILD_TYPE=Debug -DFIN_FUZZ=ON \
//     -DCMAKE_CXX_COMPILER=clang++ [...]
//   cmake --build build-fuzz --target fin_parse_fuzz
//   mkdir -p /tmp/opencode/fuzz-seeds && cp tests/samples/basic.fin /tmp/opencode/fuzz-seeds/
//   ./build-fuzz/fuzz/fin_parse_fuzz -max_len=8192 -max_total_time=60 /tmp/opencode/fuzz-seeds
//
// Measured 2026-10-13: 8104 runs in 61s, no crashes (mixed-toolchain proof:
// clang harness + gcc-built libfin_core). Two environment notes from that run:
// ASAN_OPTIONS=alloc_dealloc_mismatch=0 and LSAN_OPTIONS=detect_leaks=0 were
// needed, both LLVM/exit-time-attributed (libLLVM static init mismatches an
// aligned new with free; fin Type objects leak once at exit -- see the leak
// follow-up issue), neither in the preprocessor/lexer/parser under test.
#include <cstddef>
#include <cstdint>
#include <string>

#include "parser.hpp"
#include "lexer/lexer.hpp"
#include "diagnostics/DiagnosticEngine.hpp"
#include "preprocessor/Preprocessor.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Unbounded inputs only measure the allocator. 8 KiB covers every sample
    // in tests/samples/ that parses in under a second.
    if (size == 0 || size > 8192) return 0;
    std::string code(reinterpret_cast<const char*>(data), size);
    try {
        fin::reset_lexer_location();
        fin::Preprocessor pp;
        std::string clean = pp.process(code);
        fin::DiagnosticEngine diag(clean, "<fuzz>");
        fin::setLexerDiagnostics(&diag);
        YY_BUFFER_STATE buffer = yy_scan_string(clean.c_str());
        fin::parser parser(diag);
        parser.parse();
        yy_delete_buffer(buffer);
        fin::setLexerDiagnostics(nullptr);
    } catch (...) {
        // Malformed input must diagnose, never unwind through the fuzzer.
        fin::setLexerDiagnostics(nullptr);
    }
    return 0;
}
