#include <iostream>
#include <fstream>
#include <sstream>
#include <fmt/core.h>
#include "lexer/lexer.hpp"
#include "parser.hpp"
#include "ast/ASTPrinter.hpp"
#include "semantics/SemanticAnalyzer.hpp"
#include "diagnostics/DiagnosticEngine.hpp"

namespace fin { 
    extern std::unique_ptr<Program> root; 
}

std::string readFile(const std::string& path) {
    std::ifstream t(path);
    if (!t.is_open()) return "";
    std::stringstream buffer;
    buffer << t.rdbuf();
    return buffer.str();
}

int main(int argc, char** argv) {
    // FIXED: Declare 'code' here so it is visible to DiagnosticEngine
    std::string code;

    if (argc > 1) {
        code = readFile(argv[1]);
        if (code.empty()) {
            fmt::print(stderr, "Error: Could not read file: {}\n", argv[1]);
            return 1;
        }
    } else {
        // Default test code with a typo
        code = R"(
struct Point {
    x <int>,
    y <int>
}

fun main() <noret> {
    let p = Point { x: 10, y: 20 };
    retrn; // Typo!
}
)";
    }

    // 2. Setup Components
    fin::DiagnosticEngine diag(code, (argc > 1 ? argv[1] : "<input>"));
    YY_BUFFER_STATE buffer = yy_scan_string(code.c_str());
    
    // 3. Parse
    fin::parser parser(diag);
    int res = parser.parse();
    yy_delete_buffer(buffer);

    // 4. Process Result
    if(res == 0 && fin::root) {
        fmt::print("✅ Parsing Successful!\n");
        
        fin::ASTPrinter printer;
        printer.print(*fin::root);
        
        fmt::print("\n🔍 Running Semantic Analysis...\n");
        fin::SemanticAnalyzer analyzer;
        analyzer.visit(*fin::root);
        
        if (analyzer.hasError) {
            return 1;
        }
    } else {
        // DiagnosticEngine already printed the error
        return 1;
    }

    return 0;
}