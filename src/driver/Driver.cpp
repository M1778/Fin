#include "Driver.hpp"
#include "lexer/lexer.hpp"
#include "parser.hpp"
#include "../preprocessor/Preprocessor.hpp"
#include "../semantics/SemanticAnalyzer.hpp"
#include "../diagnostics/DiagnosticEngine.hpp"
#include "../ast/ASTPrinter.hpp"
#include "../macros/MacroExpander.hpp"
#include "../utils/ModuleLoader.hpp"
#include "../codegen/CodeGen.hpp"
#include "SearchPaths.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <fmt/core.h>

// Only for the process id in a temporary object's name (see objectPathFor).
#ifdef _WIN32
#include <process.h>
#define FIN_GETPID _getpid
#else
#include <unistd.h>
#define FIN_GETPID getpid
#endif

namespace fin {

extern std::unique_ptr<Program> root;

Driver::Driver(CompilerOptions opts) : options(std::move(opts)) {}
Driver::~Driver() {}

std::optional<std::string> Driver::readFile(const std::string& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) return std::nullopt;
    std::ifstream t(path, std::ios::binary);
    if (!t.is_open()) return std::nullopt;
    std::stringstream buffer;
    buffer << t.rdbuf();
    if (t.bad()) return std::nullopt;
    return buffer.str();
}

// Helper to configure the loader
void configureLoader(ModuleLoader& loader, const CompilerOptions& options) {
    // 1. Add CLI Include Paths
    for (const auto& path : options.includePaths) {
        loader.addSearchPath(path);
    }

    // 2. Add Library Paths: `--fin-libs` when it was given at all, otherwise the
    // environment. The flag *replaces* rather than extends, because `finn` passes
    // it to pin the environment a build compiles against, and an ambient
    // `FIN_LIBS` leaking into a pinned build is exactly the drift it exists to
    // prevent. Both are split on the platform separator (SearchPaths.hpp). An empty
    // `FIN_LIBS` counts as unset rather than as "no paths" -- see below.
    bool librariesSpecified = false;
    if (options.finLibsGiven) {
        librariesSpecified = true;
        for (const auto& path : options.finLibPaths) {
            loader.addSearchPath(path);
        }
    } else if (const char* envLibs = std::getenv("FIN_LIBS");
               envLibs != nullptr && *envLibs != '\0') {
        // Empty is unset. `--fin-libs=` is an explicit pin to nothing and suppresses
        // the bundle below; `FIN_LIBS=` is how a shell spells "clear this variable",
        // and reading it as a pin took the standard library away from anyone with
        // `export FIN_LIBS=` in a profile -- then told them to set the variable they
        // had set. LibraryPaths.AnEmptyEnvironmentVariableIsUnsetAndDoesNotSuppressTheBundle
        // holds the two spellings apart.
        librariesSpecified = true;
        for (const auto& path : splitSearchPaths(envLibs)) {
            loader.addSearchPath(path);
        }
    }

    // 3. The bundled standard library, when nothing above named one.
    //
    // Found relative to this executable, never relative to the working
    // directory. What stood here was a hardcoded `tests/samples/stdlib`, which
    // put a path from whichever checkout did the build inside every shipped
    // binary. It was also doing nothing: the test harness runs finc from
    // `build/tests`, where that relative path does not exist, and a sample
    // importing a sibling resolves against the importing file instead
    // (ModuleLoader::resolvePath step 1).
    //
    // Suppressed when `--fin-libs` or `FIN_LIBS` named paths, including when it
    // named none. `finn` passes the flag to pin what a build compiles against,
    // and a stdlib silently appearing underneath the pin is the drift the flag
    // exists to prevent.
    if (!librariesSpecified) {
        for (const auto& path : bundledLibraryPaths()) {
            loader.addSearchPath(path);
        }
    }

    // 4. The working directory, last, and only when no library paths were named.
    //
    // Where you stand should not change what compiles. It is kept for a bare
    // `finc foo.fin` typed by hand, where the working directory is the obvious
    // thing to mean and no one is promising hermeticity — but a build that named
    // its library paths *is* promising hermeticity, and the working directory is
    // the project root, so leaving it in would let a file in the project shadow
    // a module the build pinned.
    //
    // `finn` cannot fix that from its side at any price, which is what makes it
    // this function's problem rather than a language question: no flag removes a
    // path the compiler adds unconditionally.
    if (!librariesSpecified) {
        loader.addSearchPath(".");
    }
}

int Driver::compile() {
    DiagnosticEngine diag("", options.inputFile);
    diag.setFormat(options.diagFormat);
    diag.setColorMode(options.colorMode);

    // Every return from here on goes through `finish`, so the JSON summary
    // object is written exactly once on every exit path.
    auto finish = [&](ExitCode code) {
        int value = static_cast<int>(code);
        diag.emitSummary(value);
        setLexerDiagnostics(nullptr);
        return value;
    };

    // 1. Read Source. A missing file is a usage error; an empty file is a legal
    // program that compiles to nothing.
    std::optional<std::string> source = readFile(options.inputFile);
    if (!source.has_value()) {
        diag.reportError(fmt::format("could not read file: {}", options.inputFile));
        return finish(ExitCode::Usage);
    }

    // 2. Preprocessor
    std::string processedCode = runPreprocessor(*source, diag);

    diag.setSource(processedCode, options.inputFile);
    setLexerDiagnostics(&diag);

    // 3. Parser
    std::unique_ptr<Program> ast;
    if (!runParser(processedCode, ast, diag)) {
        return finish(ExitCode::Diagnostics);
    }

    // The parser can succeed while the lexer rejected a byte that produced no
    // token at all, so the engine has to be consulted and not just the parse
    // result. This is the `Build Successful.` defect.
    if (diag.hasErrors()) {
        return finish(ExitCode::Diagnostics);
    }

    // --- SHARED MODULE LOADER ---
    std::filesystem::path p(options.inputFile);
    std::string basePath = p.parent_path().string();
    if (basePath.empty()) basePath = ".";

    ModuleLoader loader(basePath);
    loader.setDiagnostics(&diag);
    configureLoader(loader, options);
    // This file is already being compiled, so an import of it is a cycle and not a module
    // to go and load. See `ModuleLoader::beginRootFile`.
    loader.beginRootFile(options.inputFile);
    // ----------------------------

    // 3.5 Macro Expansion
    if (options.debugParser) diag.note("[INFO] Running Macro Expansion...");

    auto macroScope = std::make_shared<Scope>(nullptr);
    MacroExpander expander(diag, macroScope.get());
    expander.setModuleLoader(&loader); // Use configured loader
    expander.expand(*ast);

    if (options.debugParser) {
        diag.note("\n[DEBUG] AST Structure:");
        ASTPrinter printer;
        printer.print(*ast);
        diag.note("");
    }

    // 4. Semantic Analysis
    if (!options.skipSemantics) {
        if (options.debugSema) diag.note("[INFO] Running Semantic Analysis...");

        SemanticAnalyzer analyzer(diag, options.debugSema);
        analyzer.setModuleLoader(&loader); // Use same loader
        analyzer.visit(*ast);

        if (analyzer.hasError || diag.hasErrors()) {
            return finish(ExitCode::Diagnostics);
        }

        if (options.debugSema) diag.success("[SUCCESS] Semantics Verified.");
    }

    // 5. CodeGen
    if (!options.skipCodegen) {
        if (!runCodeGen(*ast, diag)) {
            return finish(ExitCode::Diagnostics);
        }
    }

    // Exit 0 implies zero diagnostics, so this is the last gate rather than the
    // analyzer's own flag.
    if (diag.hasErrors()) {
        return finish(ExitCode::Diagnostics);
    }

    diag.success("Build Successful.");
    return finish(ExitCode::Success);
}

std::string Driver::runPreprocessor(const std::string& source, DiagnosticEngine& diag) {
    if (options.debugParser) diag.note("[INFO] Running Preprocessor...");
    Preprocessor pp;
    return pp.process(source);
}

bool Driver::runParser(const std::string& source, std::unique_ptr<Program>& outAST, DiagnosticEngine& diag) {
    fin::root = nullptr;
    fin::reset_lexer_location();
    // Byte-counted rather than `c_str()`: a NUL byte is a byte of the file like any
    // other, and passing a C string made the lexer stop at the first one -- so a file
    // with a NUL in the middle compiled as its prefix and reported success for the whole.
    YY_BUFFER_STATE buffer = yy_scan_bytes(source.data(), (int)source.size());
    fin::parser parser(diag);
    int res = parser.parse();
    yy_delete_buffer(buffer);

    if (res == 0 && fin::root) {
        outAST = std::move(fin::root);
        return true;
    }
    return false;
}

// A path in the same directory as the executable being built, so a read-only
// /tmp or a TMPDIR on a different filesystem cannot break a build, and so the
// object sits where a `-c`-style flag would eventually want to leave it.
static std::string objectPathFor(const std::string& outputPath) {
    std::filesystem::path out(outputPath);
    std::filesystem::path dir = out.parent_path();
    std::string stem = out.stem().string();
    if (stem.empty()) stem = "a";
    // The pid keeps two concurrent builds of one target from writing the same
    // object -- which `ctest -j` does, repeatedly.
    std::string name = fmt::format(".{}.{}.fin.o", stem, (long)FIN_GETPID());
    return dir.empty() ? name : (dir / name).string();
}

bool Driver::runCodeGen(Program& ast, DiagnosticEngine& diag) {
    // No `-o`, no artifact. `finc x.fin` is a check, and making it build would
    // mean every diagnostic test and every corpus snapshot linked an executable
    // -- and would turn "the backend cannot lower this yet" into a failure of a
    // run that only asked about types.
    if (!options.outputPathGiven) return true;

    if (!backendAvailable()) {
        // The stub says this too, but saying it here means the message does not
        // depend on having reached a node the emitter refuses.
        return generateObject(ast, options.outputPath, diag, options.optLevel,
                              options.debugCodegen);
    }

    const std::string objectPath = objectPathFor(options.outputPath);
    std::error_code ec;

    // Nothing from a previous build survives a failure of this one. A stale
    // executable left in place is a `./a.out` that runs yesterday's code and
    // reports it as today's.
    std::filesystem::remove(options.outputPath, ec);

    if (!generateObject(ast, objectPath, diag, options.optLevel, options.debugCodegen)) {
        std::filesystem::remove(objectPath, ec);
        return false;
    }

    bool linked = runLinker(objectPath, diag);
    std::filesystem::remove(objectPath, ec);
    if (!linked) {
        std::filesystem::remove(options.outputPath, ec);
        return false;
    }
    return true;
}

bool Driver::runLinker(const std::string& objectPath, DiagnosticEngine& diag) {
    // `cc` rather than a linker directly: the C driver is what knows this
    // platform's crt files, its dynamic loader, and where libc is. Fin has no
    // runtime of its own to add yet, and when it does this is the one line that
    // grows a `-lfin`.
    //
    // `FIN_CC` overrides it, because a cross build and a distro whose compiler
    // is not on PATH as `cc` are both real and neither is worth a rebuild of
    // finc to accommodate.
    const char* fromEnv = std::getenv("FIN_CC");
    const std::string cc = (fromEnv && *fromEnv) ? fromEnv : "cc";

    auto quote = [](const std::string& s) {
        std::string out = "'";
        for (char c : s) {
            if (c == '\'') out += "'\\''";
            else out += c;
        }
        return out + "'";
    };

    std::string command = fmt::format("{} {} -o {}", quote(cc), quote(objectPath),
                                      quote(options.outputPath));
    if (options.debugCodegen) diag.note("[codegen] " + command);

    int rc = std::system(command.c_str());
    if (rc != 0) {
        diag.reportError(fmt::format("link failed: {} exited with {}", cc, rc),
                         "the object file was emitted, so this is the C toolchain "
                         "rather than the Fin program; set FIN_CC to name a working "
                         "compiler driver");
        return false;
    }
    return true;
}

} // namespace fin
