#include "Driver.hpp"
#include "lexer/lexer.hpp"
#include "parser.hpp"
#include "../preprocessor/Preprocessor.hpp"
#include "../semantics/SemanticAnalyzer.hpp"
#include "../diagnostics/DiagnosticEngine.hpp"
#include "../ast/ASTPrinter.hpp"
#include "../macros/MacroExpander.hpp"
#include "../utils/ModuleLoader.hpp"

#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <fmt/core.h>
#include <fmt/color.h>

namespace fin {

extern std::unique_ptr<Program> root;

Driver::Driver(CompilerOptions opts) : options(std::move(opts)) {}
Driver::~Driver() {}

std::string Driver::readFile(const std::string& path) {
    std::ifstream t(path);
    if (!t.is_open()) return "";
    std::stringstream buffer;
    buffer << t.rdbuf();
    return buffer.str();
}

// Helper to configure the loader
void configureLoader(ModuleLoader& loader, const CompilerOptions& options) {
    // 1. Add CLI Include Paths
    for (const auto& path : options.includePaths) {
        loader.addSearchPath(path);
    }

    // 2. Add Environment Paths
    if (const char* envLibs = std::getenv("FIN_LIBS")) {
        std::string libsStr = envLibs;
        std::stringstream ss(libsStr);
        std::string path;
        while (std::getline(ss, path, ':')) {
            loader.addSearchPath(path);
        }
    }
    
    // 3. Add Default Test Paths
    loader.addSearchPath("tests/samples/stdlib"); 
    loader.addSearchPath("."); 
}

int Driver::compile() {
    // 1. Read Source
    std::string source = readFile(options.inputFile);
    if (source.empty()) {
        fmt::print(fg(fmt::color::red), "[ERROR] Could not read file: {}\n", options.inputFile);
        return 1;
    }

    // 2. Preprocessor
    std::string processedCode = runPreprocessor(source);
    
    DiagnosticEngine diag(processedCode, options.inputFile);

    // 3. Parser
    std::unique_ptr<Program> ast;
    if (!runParser(processedCode, ast, diag)) {
        return 1;
    }

    // --- SHARED MODULE LOADER ---
    std::filesystem::path p(options.inputFile);
    std::string basePath = p.parent_path().string();
    if(basePath.empty()) basePath = ".";
    
    ModuleLoader loader(basePath);
    configureLoader(loader, options);
    // ----------------------------

    // 3.5 Macro Expansion
    if (options.debugParser) fmt::print("[INFO] Running Macro Expansion...\n");
    
    auto macroScope = std::make_shared<Scope>(nullptr);
    MacroExpander expander(diag, macroScope.get());
    expander.setModuleLoader(&loader); // Use configured loader
    expander.expand(*ast);

    if (options.debugParser) {
        fmt::print("\n[DEBUG] AST Structure:\n");
        ASTPrinter printer;
        printer.print(*ast);
        fmt::print("\n");
    }

    // 4. Semantic Analysis
    if (!options.skipSemantics) {
        if (options.debugSema) fmt::print("[INFO] Running Semantic Analysis...\n");
        
        SemanticAnalyzer analyzer(diag, options.debugSema);
        analyzer.setModuleLoader(&loader); // Use same loader
        analyzer.visit(*ast);
        
        if (analyzer.hasError) {
            return 1;
        }
        
        if (options.debugSema) fmt::print(fg(fmt::color::green), "[SUCCESS] Semantics Verified.\n");
    }

    // 5. CodeGen
    if (!options.skipCodegen) {
        if (!runCodeGen(*ast)) {
            return 1;
        }
    }

    fmt::print(fg(fmt::color::green) | fmt::emphasis::bold, "Build Successful.\n");
    return 0;
}

std::string Driver::runPreprocessor(const std::string& source) {
    if (options.debugParser) fmt::print("[INFO] Running Preprocessor...\n");
    Preprocessor pp;
    return pp.process(source);
}

bool Driver::runParser(const std::string& source, std::unique_ptr<Program>& outAST, DiagnosticEngine& diag) {
    fin::root = nullptr;
    YY_BUFFER_STATE buffer = yy_scan_string(source.c_str());
    fin::parser parser(diag);
    int res = parser.parse();
    yy_delete_buffer(buffer);

    if (res == 0 && fin::root) {
        outAST = std::move(fin::root);
        return true;
    }
    return false;
}

bool Driver::runCodeGen(Program& ast) {
    return true;
}

} // namespace fin