#pragma once
#include <string>
#include <vector>
#include "../diagnostics/DiagnosticEngine.hpp"

namespace fin {

// The process exit code, which is part of the machine contract (ADR 0009):
// `finn` branches on it, so the four classes are stable.
enum class ExitCode : int {
    Success     = 0,  // compiled, zero diagnostics
    Diagnostics = 1,  // the source was rejected
    Usage       = 2,  // argv was wrong, or the input could not be read
    Internal    = 3,  // the compiler itself failed
};

struct CompilerOptions {
    std::string inputFile;
    std::string outputPath = "a.out";
    // Whether `-o` was actually written. Separate from comparing outputPath
    // against its default because `-o a.out` is a real request and because the
    // flag is what decides whether codegen runs at all: `finc x.fin` checks a
    // program, `finc x.fin -o x` builds one. Every test that wants diagnostics,
    // and tests/tools/corpus_snapshot.sh, invoke the first form -- so the
    // default has to stay "check only", or a sample the backend cannot lower yet
    // would start failing a run that only ever asked about its types.
    bool outputPathGiven = false;

    std::vector<std::string> includePaths;

    // Library search paths from `--fin-libs`, which `finn` passes to pin the
    // environment a build compiles against (tests/samples/importing.fin:9).
    // `finLibsGiven` is separate from emptiness because `--fin-libs=` is a
    // meaningful request — "no library paths" — and must not silently fall back
    // to whatever `FIN_LIBS` happens to hold in the caller's shell.
    std::vector<std::string> finLibPaths;
    bool finLibsGiven = false;

    bool debugLexer = false;
    bool debugParser = false;
    bool debugSema = false;
    bool debugCodegen = false;

    bool skipSemantics = false;
    bool skipCodegen = false;

    int optLevel = 0;

    DiagnosticFormat diagFormat = DiagnosticFormat::Human;
    ColorMode colorMode = ColorMode::Auto;
};

}
