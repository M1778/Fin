#pragma once

#include <string>
#include <optional>
#include <unordered_map>
#include <memory>
#include <vector>
#include <set>

namespace fin {
    class Scope;
    class Program;
    class DiagnosticEngine;
}

namespace fin {

class ModuleLoader {
public:
    ModuleLoader(const std::string& basePath);

    void addSearchPath(const std::string& path);

    // The loader reports through the caller's engine rather than writing to a
    // stream itself, so one bad import produces one diagnostic in one format
    // and no non-JSON byte reaches stderr in JSON mode (ADR 0009).
    void setDiagnostics(DiagnosticEngine* d) { diags = d; }

    // The file named on the command line is compiled by the driver, not loaded through
    // `loadModule`, so the loader would otherwise have no idea it exists. Told about it,
    // the loader treats an import of it as the circular dependency it is; not told, it
    // loaded the root a second time as though it were an ordinary module and every
    // diagnostic in the root was reported twice at the same position. Never lifted again:
    // the root is being compiled for the whole run.
    void beginRootFile(const std::string& path);

    std::shared_ptr<Scope> loadModule(const std::string& importPath, bool isPackage);

private:
    DiagnosticEngine* diags = nullptr;
    std::string rootBasePath; // Directory of the current file
    std::vector<std::string> searchPaths; // Global search paths (FIN_LIBS, -I)
    
    std::unordered_map<std::string, std::shared_ptr<Scope>> moduleCache;
    std::set<std::string> loadingStack;

    // Imports already reported as unresolvable. `moduleCache` remembers only
    // successes -- a failure returns before it is consulted -- and both the
    // macro expander and the analyzer ask for every import, so without this a
    // single bad import is diagnosed once per pass.
    std::set<std::string> failedModules;

    // Modules that resolved but failed to parse or to analyse. Re-parsing one
    // repeats every diagnostic it already raised, and -- because the lexer's
    // location is file-scope -- advances its line counter a second time, so the
    // repeat also lands on a different wrong line. The caller still reports its
    // own located error at each import site.
    std::set<std::string> failedPaths;

    // Cycles already stated. An import cycle is one fact about the program.
    std::set<std::string> reportedCycles;

    // Every search path the driver handed over, including ones that do not
    // exist: `addSearchPath` keeps only real directories to search, but a
    // misspelled one is exactly what the failure needs to be able to name.
    std::vector<std::string> requestedPaths;
    std::vector<std::unique_ptr<Program>> astStorage;

    std::string resolvePath(const std::string& importPath, bool isPackage);
    // Empty on nothing: a file the loader was not allowed to open used to be
    // indistinguishable from a file that is empty, and an unreadable module was
    // then reported as a module that exports nothing. Matches `Driver::readFile`.
    std::optional<std::string> readFile(const std::string& path);

    // Which file on disk a resolved path refers to, as opposed to how it was spelled.
    // The caches below are keyed on this and never on the path text: one file reached
    // by two names is one module, and parsing it twice reports its diagnostics twice.
    std::string identityOf(const std::string& path) const;
    std::string describeSearchedPaths(bool isPackage) const;
    void report(const std::string& msg);
    void report(const std::string& msg, const std::string& help);
};

}