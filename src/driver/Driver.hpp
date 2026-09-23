#pragma once
#include "CompilerOptions.hpp"
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Forward declarations to keep header clean
namespace fin {
    class Program;
    class DiagnosticEngine;
}

namespace fin {

// Whether compiling `processedSource` (the root file after preprocessing)
// requires the ambient `#[global] printf` from the bundled stdio module, so
// the driver must load it before analysis. True when the file has an import
// (an imported module is analysed against the same shared scope and may reach
// the ambient name), when it spells a bare `printf` without declaring its own
// top-level `@define printf` (which would shadow the ambient), or when it
// queries ambient state with `@defined`. Comments, strings and character
// literals are skipped, so a mention that is not code never forces the load,
// while an `@define printf` inside one never excuses it. Anything this cannot
// prove needless loads, so the answer errs towards loading, never skipping.
bool needsAmbientStdio(const std::string& processedSource);

class Driver {
public:
    Driver(CompilerOptions opts);
    ~Driver();

    // Main entry point for the compiler. The return value is a member of
    // ExitCode and is part of the machine contract (ADR 0009).
    int compile();

    // An empty file and a missing file are different states. `std::nullopt`
    // means the file could not be read; an empty string means the file is
    // empty, which is a legal Fin program.
    static std::optional<std::string> readFile(const std::string& path);

private:
    CompilerOptions options;

    // Pipeline Stages
    std::string runPreprocessor(const std::string& source, DiagnosticEngine& diag);
    bool runParser(const std::string& source, std::unique_ptr<Program>& outAST, DiagnosticEngine& diag);
    // Emits an object file and links it into `options.outputPath`. A no-op that
    // returns true when `-o` was not written: `finc x.fin` checks a program and
    // `finc x.fin -o x` builds one (see CompilerOptions::outputPathGiven).
    // `modules` are the loader's successfully analysed Programs (ADR 0032):
    // borrowed for registration only, owned by the caller's loader.
    bool runCodeGen(Program& ast, DiagnosticEngine& diag,
                    const std::vector<const Program*>& modules);
    // `cc <object> -o <outputPath>`. Separate from runCodeGen because the object
    // is what the backend owns and the executable is what a C toolchain does --
    // and because `finn` will eventually want the object without the link.
    bool runLinker(const std::string& objectPath, DiagnosticEngine& diag);
};

}
