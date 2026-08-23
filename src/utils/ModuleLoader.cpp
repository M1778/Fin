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
    if (path.empty()) return;  // an empty entry would resolve against the cwd
    requestedPaths.push_back(path);
    if (fs::exists(path) && fs::is_directory(path)) {
        searchPaths.push_back(path);
    }
}

// The places `resolvePath` would have looked, in the order it looks, with the
// ones that are not there marked as such. A path that does not exist is dropped
// from `searchPaths` above and so leaves no trace in the search itself, which is
// what makes a typo in `--fin-libs` or `FIN_LIBS` indistinguishable from a
// module that genuinely is not installed.
//
// This is help text on a failure rather than a warning at the point the bad path
// is accepted, because ADR 0009 has exit `0` imply zero diagnostics: warning
// about a bogus entry on a compile that then succeeded would break that.
std::string ModuleLoader::describeSearchedPaths(bool isPackage) const {
    std::vector<std::string> places;
    // A package import consults the search paths only (resolvePath CASE A); a
    // file import tries the importing file's own directory first (CASE B).
    if (!isPackage && !rootBasePath.empty()) places.push_back(rootBasePath);
    places.insert(places.end(), requestedPaths.begin(), requestedPaths.end());

    if (places.empty()) {
        return "no library search paths were given; pass --fin-libs or set FIN_LIBS";
    }

    std::string out = "searched: ";
    for (size_t i = 0; i < places.size(); ++i) {
        if (i) out += ", ";
        out += places[i];
        std::error_code ec;
        if (!fs::is_directory(places[i], ec)) out += " (does not exist)";
    }
    return out;
}

std::string ModuleLoader::resolvePath(const std::string& rawImport, bool isPackage) {
    // Helper to check if a path exists, or if path + ".fin" exists.
    // Every hit is returned lexically normalised. This path is what the reader sees in a
    // diagnostic, and it is built by concatenating the import text onto a base directory,
    // so without this an `import "./m"` was reported against `././m.fin`. Lexical rather
    // than `fs::canonical`: it keeps the path short and relative to where the compiler was
    // run, which is what a reader and an editor both want. Identity is a separate question,
    // answered by `identityOf`.
    auto check = [](fs::path p) -> std::string {
        auto hit = [](const fs::path& found) { return found.lexically_normal().string(); };

        if (fs::exists(p) && !fs::is_directory(p)) return hit(p);

        // Try appending .fin
        fs::path pFin = p;
        pFin += ".fin";
        if (fs::exists(pFin)) return hit(pFin);
        
        // Try directory index (p/index.fin)
        fs::path pIndex = p / "index.fin";
        if (fs::exists(pIndex)) return hit(pIndex);

        // Try directory self-name (p/p.fin)
        if (p.has_filename()) {
            fs::path pSelf = p / (p.filename().string() + ".fin");
            if (fs::exists(pSelf)) return hit(pSelf);
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

void ModuleLoader::report(const std::string& msg) {
    if (diags) {
        diags->reportError(msg);
    } else {
        // No engine wired up (a unit test constructing the loader directly).
        // Still stderr, still one line.
        fmt::print(stderr, "error: {}\n", msg);
    }
}

void ModuleLoader::report(const std::string& msg, const std::string& help) {
    if (diags) {
        diags->reportError(msg, help);
    } else {
        // Same promise as above: one line, so the help goes inline rather than
        // becoming a second line this path has never emitted.
        fmt::print(stderr, "error: {} ({})\n", msg, help);
    }
}

std::optional<std::string> ModuleLoader::readFile(const std::string& path) {
    // Binary, matching `Driver::readFile`. On Linux the two modes are the same, so nothing
    // here can tell them apart -- but on a platform that translates line endings in text
    // mode, a module would disagree with the file named on the command line about which
    // column an error is in, and the encoding tests would only catch it on that platform.
    std::ifstream t(path, std::ios::binary);
    if (!t.is_open()) return std::nullopt;
    std::stringstream buffer;
    buffer << t.rdbuf();
    return buffer.str();
}

std::string ModuleLoader::identityOf(const std::string& path) const {
    // Resolves symlinks and makes the path absolute, so the two names of one file agree.
    // Never shown to anyone: `fs::canonical` returns an absolute path, and replacing a
    // short relative path with one in a diagnostic would be a regression in its own right.
    // Falls back to the lexically normal path when the file cannot be interrogated -- a
    // race, or a permission problem on a parent directory -- because a key that is merely
    // no better than the old one is still better than aborting the load over it.
    std::error_code ec;
    fs::path resolved = fs::canonical(path, ec);
    if (ec) return fs::path(path).lexically_normal().string();
    return resolved.string();
}

void ModuleLoader::beginRootFile(const std::string& path) {
    // Keyed the same way every other entry is, so the root is recognised however an
    // import spells it -- "main", "./main", or a symlink to it.
    loadingStack.insert(identityOf(path));
}

std::shared_ptr<Scope> ModuleLoader::loadModule(const std::string& importPath, bool isPackage) {
    // 1. Resolve
    std::string fullPath = resolvePath(importPath, isPackage);
    if (fullPath.empty()) {
        // Once per import, not once per pass that asks (ModuleLoader.hpp:23).
        const std::string key = (isPackage ? "p:" : "f:") + importPath;
        if (failedModules.insert(key).second) {
            report(fmt::format("module not found: {}", importPath),
                   describeSearchedPaths(isPackage));
        }
        return nullptr;
    }

    // `fullPath` from here on is for display only -- it is the spelling to show a reader.
    // Every cache is keyed on `key`, which names the file rather than the spelling, so a
    // module imported as both "m" and "./m", or through a symlink, is loaded once.
    const std::string key = identityOf(fullPath);

    // 2. Check Cache
    if (moduleCache.count(key)) return moduleCache[key];

    // 2b. Failures are remembered as well as successes. Both the macro expander
    // and the analyzer load every import, so without this the second pass
    // re-parses a broken module and repeats every diagnostic in it.
    if (failedPaths.count(key)) return nullptr;

    // 3. Check Circular Dependency. Not recorded in `failedPaths`: this is the
    // inner attempt on a module whose outer load is still in progress, and that
    // outer load is the one entitled to decide how it ends.
    if (loadingStack.count(key)) {
        if (reportedCycles.insert(key).second) {
            report(fmt::format("circular dependency detected: {}", fullPath));
        }
        return nullptr;
    }
    
    loadingStack.insert(key);

    // 4. Read & Preprocess
    std::optional<std::string> contents = readFile(fullPath);
    if (!contents) {
        // `resolvePath` already found the file, so this is not a missing module: the
        // path exists and could not be opened. Said plainly, because returning "" here
        // made the next steps diagnose an empty module, and the reader was told the
        // module does not export the name they imported -- sending them to look for a
        // typo in a file the compiler never read. Bookkeeping matches the parse failure
        // below: remembered as failed so the second pass does not repeat this, and
        // lifted off the loading stack it was pushed onto above.
        report(fmt::format("could not read module: {}", fullPath),
               "the file was found but could not be opened; check its permissions");
        failedPaths.insert(key);
        loadingStack.erase(key);
        return nullptr;
    }
    std::string source = *contents;
    Preprocessor pp;
    source = pp.process(source);

    // 5. Parse
    auto oldRoot = std::move(fin::root);
    
    // The module gets its own engine because its diagnostics point into its own
    // source, but it inherits the caller's format and colour so a module error
    // cannot break a JSON consumer's parser.
    DiagnosticEngine diag(source, fullPath);
    if (diags) {
        diag.setFormat(diags->getFormat());
        diag.setColorMode(diags->usesColor() ? ColorMode::Always : ColorMode::Never);
    }

    // Everything `diag` counts has already been printed to the shared stream, so the
    // caller has to end up owning those counts. A guard rather than a call at the end,
    // because the steps below return early on four separate failures and the summary
    // would undercount by exactly the diagnostics of whichever module failed. Declared
    // after `diag`, so it is destroyed before it.
    struct CountFold {
        DiagnosticEngine* parent;
        const DiagnosticEngine& child;
        ~CountFold() { if (parent) parent->absorbCountsOf(child); }
    } countFold{diags, diag};

    // The lexer's location is file-scope (`lexer.l:13`) and does not reset
    // itself between buffers, so without this a module's first line is numbered
    // from wherever the previous parse stopped -- and the renderer then prints a
    // blank snippet, because that line is past the end of this file.
    // `Driver.cpp:203` does the same for the file named on the command line.
    // Safe to do here because a parse is never interrupted by another parse: a
    // module is parsed at step 5 and only expanded, which is what can recurse,
    // at step 6.
    fin::reset_lexer_location();
    // Byte-counted, for the reason given at `Driver.cpp:204`.
    YY_BUFFER_STATE buffer = yy_scan_bytes(source.data(), (int)source.size());
    fin::parser parser(diag);
    int res = parser.parse();
    yy_delete_buffer(buffer);

    if (res != 0 || !fin::root) {
        report(fmt::format("failed to parse module: {}", fullPath));
        failedPaths.insert(key);
        loadingStack.erase(key);
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
        report(fmt::format("semantic errors in module: {}", fullPath));
        failedPaths.insert(key);
        loadingStack.erase(key);
        return nullptr;
    }

    // 8. Merge Results
    auto moduleScope = analyzer.getGlobalScope();
    for(auto& kv : macroScope->macros) {
        moduleScope->defineMacro(kv.first, kv.second);
    }

    astStorage.push_back(std::move(moduleAST));
    moduleCache[key] = moduleScope;
    loadingStack.erase(key);
    
    return moduleScope;
}

}