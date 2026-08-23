#pragma once
#include <memory>
#include <string>

#include "ast/decls/Program.hpp"
#include "diagnostics/DiagnosticEngine.hpp"

namespace fin::testing {

// Everything the old suite could not reach. `parseString` stopped at the parser,
// so SemanticAnalyzer, ModuleLoader and MacroExpander had no unit tests at all.
struct ParseResult {
    std::unique_ptr<fin::Program> ast;
    bool parsed = false;
};

// Preprocess, lex and parse a string. Never runs the analyzer.
ParseResult parseSource(const std::string& code, fin::DiagnosticEngine& diag);

}
