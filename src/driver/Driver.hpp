#pragma once
#include "CompilerOptions.hpp"
#include <memory>
#include <optional>
#include <string>

// Forward declarations to keep header clean
namespace fin {
    class Program;
    class DiagnosticEngine;
}

namespace fin {

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
    bool runCodeGen(Program& ast, DiagnosticEngine& diag);
    // `cc <object> -o <outputPath>`. Separate from runCodeGen because the object
    // is what the backend owns and the executable is what a C toolchain does --
    // and because `finn` will eventually want the object without the link.
    bool runLinker(const std::string& objectPath, DiagnosticEngine& diag);
};

}
