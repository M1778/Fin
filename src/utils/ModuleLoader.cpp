#include "ModuleLoader.hpp"
#include "lexer/lexer.hpp"
#include "parser.hpp"
#include "../preprocessor/Preprocessor.hpp"
#include "../semantics/SemanticAnalyzer.hpp"
#include "../diagnostics/DiagnosticEngine.hpp"
#include "../macros/MacroExpander.hpp"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <fmt/core.h>
#include <fmt/color.h>
#include <algorithm>

namespace fs = std::filesystem;

namespace fin {

extern std::unique_ptr<Program> root;

ModuleLoader::ModuleLoader(const std::string& base) : rootBasePath(base) {
    if (!fs::is_directory(rootBasePath)) {
        rootBasePath = fs::path(rootBasePath).parent_path().string();
    }
    // Always search current directory of the file being compiled
}

void ModuleLoader::addSearchPath(const std::string& path) {
    if (fs::exists(path) && fs::is_directory(path)) {
        searchPaths.push_back(path);
    }
}

std::string ModuleLoader::resolvePath(const std::string& rawImport, bool isPackage) {
    // Helper to check if a path exists, or if path + ".fin" exists
    auto check = [](fs::path p) -> std::string {
        if (fs::exists(p) && !fs::is_directory(p)) return p.string();
        
        // Try appending .fin
        fs::path pFin = p;
        pFin += ".fin";
        if (fs::exists(pFin)) return pFin.string();
        
        // Try directory index (p/index.fin)
        fs::path pIndex = p / "index.fin";
        if (fs::exists(pIndex)) return pIndex.string();

        // Try directory self-name (p/p.fin)
        if (p.has_filename()) {
            fs::path pSelf = p / (p.filename().string() + ".fin");
            if (fs::exists(pSelf)) return pSelf.string();
        }
        
        return "";
    };

    // CASE A: Package Import (import std.io)
    if (isPackage) {
        std::string modPath = rawImport;
        std::replace(modPath.begin(), modPath.end(), '.', '/');

        for (const auto& base : searchPaths) {
            std::string res = check(fs::path(base) / modPath);
            if (!res.empty()) return res;
        }
        return "";
    }

    // CASE B: File Import (import "foo" or import "foo.fin")
    
    // 1. Check relative to current file (rootBasePath)
    std::string res = check(fs::path(rootBasePath) / rawImport);
    if (!res.empty()) return res;
    
    // 2. Check absolute path
    fs::path absP(rawImport);
    if (absP.is_absolute()) {
        res = check(absP);
        if (!res.empty()) return res;
    }

    // 3. Check in Search Paths (Treating quoted string as a library lookup)
    // This allows import "somelib" to find "stdlib/somelib.fin"
    for (const auto& base : searchPaths) {
        res = check(fs::path(base) / rawImport);
        if (!res.empty()) return res;
    }

    return "";
}

std::string ModuleLoader::readFile(const std::string& path) {
    std::ifstream t(path);
    if (!t.is_open()) return "";
    std::stringstream buffer;
    buffer << t.rdbuf();
    return buffer.str();
}

std::shared_ptr<Scope> ModuleLoader::loadModule(const std::string& importPath, bool isPackage) {
    // 1. Resolve
    std::string fullPath = resolvePath(importPath, isPackage);
    if (fullPath.empty()) {
        fmt::print(fg(fmt::color::red), "[ERROR] Module not found: {}\n", importPath);
        return nullptr;
    }

    // 2. Check Cache
    if (moduleCache.count(fullPath)) return moduleCache[fullPath];
    
    // 3. Check Circular Dependency
    if (loadingStack.count(fullPath)) {
        fmt::print(fg(fmt::color::red), "[ERROR] Circular dependency detected: {}\n", fullPath);
        return nullptr;
    }
    
    loadingStack.insert(fullPath);

    // 4. Read & Preprocess
    std::string source = readFile(fullPath);
    Preprocessor pp;
    source = pp.process(source);

    // 5. Parse
    auto oldRoot = std::move(fin::root);
    
    DiagnosticEngine diag(source, fullPath);
    YY_BUFFER_STATE buffer = yy_scan_string(source.c_str());
    fin::parser parser(diag);
    int res = parser.parse();
    yy_delete_buffer(buffer);

    if (res != 0 || !fin::root) {
        fmt::print(fg(fmt::color::red), "[ERROR] Failed to parse module: {}\n", fullPath);
        loadingStack.erase(fullPath);
        fin::root = std::move(oldRoot);
        return nullptr;
    }

    auto moduleAST = std::move(fin::root);
    fin::root = std::move(oldRoot);

    // 6. Macro Expansion
    auto macroScope = std::make_shared<Scope>(nullptr);
    MacroExpander expander(diag, macroScope.get());
    expander.setModuleLoader(this);
    expander.expand(*moduleAST);

    // 7. Semantic Analysis
    SemanticAnalyzer analyzer(diag, false);
    analyzer.setModuleLoader(this);
    analyzer.visit(*moduleAST);

    if (analyzer.hasError) {
        fmt::print(fg(fmt::color::red), "[ERROR] Semantic errors in module: {}\n", fullPath);
        loadingStack.erase(fullPath);
        return nullptr;
    }

    // 8. Merge Results
    auto moduleScope = analyzer.getGlobalScope();
    for(auto& kv : macroScope->macros) {
        moduleScope->defineMacro(kv.first, kv.second);
    }

    astStorage.push_back(std::move(moduleAST));
    moduleCache[fullPath] = moduleScope;
    loadingStack.erase(fullPath);
    
    return moduleScope;
}

}