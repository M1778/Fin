#pragma once
#include "CompilerOptions.hpp"
#include <memory>
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

    // Main entry point for the compiler
    int compile();

private:
    CompilerOptions options;
    
    // Pipeline Stages
    std::string runPreprocessor(const std::string& source);
    bool runParser(const std::string& source, std::unique_ptr<Program>& outAST, DiagnosticEngine& diag);
    bool runCodeGen(Program& ast); // Code generation placeholder
    bool runLinker();              // Not yet implemented
    
    // Helpers
    std::string readFile(const std::string& path);
};

}
