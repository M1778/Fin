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
    bool runCodeGen(Program& ast); // Code generation placeholder
    bool runLinker();              // Not yet implemented
};

}
