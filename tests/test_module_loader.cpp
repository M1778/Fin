#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "Corpus.hpp"
#include "Pipeline.hpp"
#include "ast/decls/Program.hpp"
#include "ast/stmts/Import.hpp"
#include "diagnostics/DiagnosticEngine.hpp"
#include "semantics/Scope.hpp"
#include "semantics/SemanticAnalyzer.hpp"
#include "utils/ModuleLoader.hpp"

namespace fs = std::filesystem;
using namespace fin::testing;

namespace {

// A throwaway directory of .fin modules.
class TempModuleDir {
public:
    TempModuleDir() {
        dir_ = uniqueTempPath("fin_modules");
        fs::create_directories(dir_);
    }
    ~TempModuleDir() { std::error_code ec; fs::remove_all(dir_, ec); }

    void write(const std::string& name, const std::string& contents) {
        fs::path p = dir_ / name;
        fs::create_directories(p.parent_path());
        std::ofstream f(p, std::ios::binary);
        f.write(contents.data(), (std::streamsize)contents.size());
    }
    std::string path() const { return dir_.string(); }

private:
    fs::path dir_;
};

} // namespace

TEST(ModuleLoader, LoadsAModuleAndPublishesItsScope) {
    TempModuleDir d;
    d.write("lib.fin", "fun helper() <noret> {}\n");

    fin::DiagnosticEngine diag("", "<test>");
    diag.setColorMode(fin::ColorMode::Never);

    fin::ModuleLoader loader(d.path());
    loader.setDiagnostics(&diag);
    loader.addSearchPath(d.path());

    auto scope = loader.loadModule("lib", true);
    ASSERT_NE(scope, nullptr) << "errors: " << diag.getErrorCount();
    EXPECT_NE(scope->resolve("helper"), nullptr);
    EXPECT_FALSE(diag.hasErrors());
}

TEST(ModuleLoader, ReportsAMissingModuleThroughTheDiagnosticEngine) {
    TempModuleDir d;
    fin::DiagnosticEngine diag("", "<test>");
    diag.setColorMode(fin::ColorMode::Never);

    fin::ModuleLoader loader(d.path());
    loader.setDiagnostics(&diag);
    loader.addSearchPath(d.path());

    auto scope = loader.loadModule("no_such_module", true);
    EXPECT_EQ(scope, nullptr);
    // ModuleLoader.cpp:104 used to fmt::print outside the engine, so this count
    // stayed at zero while a raw line appeared on the terminal.
    EXPECT_TRUE(diag.hasErrors())
        << "a missing module must be counted by the engine, not just printed";
    ASSERT_FALSE(diag.getDiagnostics().empty());
    EXPECT_NE(diag.getDiagnostics().front().message.find("module not found"),
              std::string::npos);
}

TEST(ModuleLoader, ReportsAMissingModuleExactlyOnce) {
    // The observed behaviour for one bad import was the raw line twice plus the
    // real diagnostic.
    TempModuleDir d;
    fin::DiagnosticEngine diag("", "<test>");
    diag.setColorMode(fin::ColorMode::Never);

    fin::ModuleLoader loader(d.path());
    loader.setDiagnostics(&diag);
    loader.addSearchPath(d.path());
    loader.loadModule("no_such_module", true);

    EXPECT_EQ(diag.getErrorCount(), 1) << "one bad import, one diagnostic";
}

TEST(ModuleLoader, ReportsAModuleThatDoesNotParse) {
    TempModuleDir d;
    d.write("broken.fin", "fun ( ) ) {\n");

    fin::DiagnosticEngine diag("", "<test>");
    diag.setColorMode(fin::ColorMode::Never);

    fin::ModuleLoader loader(d.path());
    loader.setDiagnostics(&diag);
    loader.addSearchPath(d.path());

    auto scope = loader.loadModule("broken", true);
    EXPECT_EQ(scope, nullptr);
    EXPECT_TRUE(diag.hasErrors());
}

TEST(ModuleLoader, CachesAModuleSoASecondLoadReturnsTheSameScope) {
    TempModuleDir d;
    d.write("lib.fin", "fun helper() <noret> {}\n");

    fin::DiagnosticEngine diag("", "<test>");
    diag.setColorMode(fin::ColorMode::Never);

    fin::ModuleLoader loader(d.path());
    loader.setDiagnostics(&diag);
    loader.addSearchPath(d.path());

    auto a = loader.loadModule("lib", true);
    auto b = loader.loadModule("lib", true);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a, b) << "the second load must come from the cache";
}

TEST(ModuleLoader, ResolvesADottedPackagePathToNestedDirectories) {
    TempModuleDir d;
    d.write("pkg/inner.fin", "fun deep() <noret> {}\n");

    fin::DiagnosticEngine diag("", "<test>");
    diag.setColorMode(fin::ColorMode::Never);

    fin::ModuleLoader loader(d.path());
    loader.setDiagnostics(&diag);
    loader.addSearchPath(d.path());

    auto scope = loader.loadModule("pkg.inner", true);
    ASSERT_NE(scope, nullptr) << "errors: " << diag.getErrorCount();
    EXPECT_NE(scope->resolve("deep"), nullptr);
}

TEST(ModuleLoader, ResolvesADirectoryIndexFile) {
    TempModuleDir d;
    d.write("pkg/index.fin", "fun indexed() <noret> {}\n");

    fin::DiagnosticEngine diag("", "<test>");
    diag.setColorMode(fin::ColorMode::Never);

    fin::ModuleLoader loader(d.path());
    loader.setDiagnostics(&diag);
    loader.addSearchPath(d.path());

    auto scope = loader.loadModule("pkg", true);
    ASSERT_NE(scope, nullptr) << "errors: " << diag.getErrorCount();
    EXPECT_NE(scope->resolve("indexed"), nullptr);
}

TEST(ModuleLoader, IgnoresASearchPathThatIsNotADirectory) {
    TempModuleDir d;
    d.write("lib.fin", "fun helper() <noret> {}\n");

    fin::DiagnosticEngine diag("", "<test>");
    fin::ModuleLoader loader(d.path());
    loader.setDiagnostics(&diag);
    loader.addSearchPath(d.path() + "/lib.fin");   // a file, not a directory
    loader.addSearchPath("/nonexistent/nowhere");
    loader.addSearchPath(d.path());

    EXPECT_NE(loader.loadModule("lib", true), nullptr);
}

TEST(ModuleLoader, InheritsTheCallersDiagnosticFormat) {
    // A module-local engine defaulting to the human renderer would spray
    // non-JSON bytes onto stderr in JSON mode and break the consumer's parser.
    TempModuleDir d;
    d.write("broken.fin", "fun ( ) ) {\n");

    fin::DiagnosticEngine diag("", "<test>");
    diag.setFormat(fin::DiagnosticFormat::Json);
    diag.setColorMode(fin::ColorMode::Never);
    EXPECT_EQ(diag.getFormat(), fin::DiagnosticFormat::Json);

    fin::ModuleLoader loader(d.path());
    loader.setDiagnostics(&diag);
    loader.addSearchPath(d.path());
    loader.loadModule("broken", true);
    EXPECT_TRUE(diag.hasErrors());
}

// --- Consuming the import ---------------------------------------------------
//
// An import is a compile-time name-binding directive with no runtime meaning: the
// loader reads the module, the analyzer copies the names it asks for into this
// scope, and after that the statement has said everything it has to say. The
// backend agreed and said so -- `an import (the module loader did not consume it)
// is not lowered yet` -- so the front end has to be the one that takes it out of
// the tree. It only does so for an import that was fully consumed; one that named
// a module or a symbol that does not exist stays in the tree behind its own
// diagnostic, because a construct the compiler could not handle is refused and
// never quietly dropped.

namespace {

int countImports(const fin::Program& program) {
    int n = 0;
    for (const auto& stmt : program.statements)
        if (dynamic_cast<const fin::ImportModule*>(stmt.get())) ++n;
    return n;
}

// Parse `code`, then analyse it with a loader pointed at `dir`. Returns the
// number of ImportModule statements the analyzer left behind.
struct ImportAnalysis {
    bool parsed = false;
    int importsBefore = 0;
    int importsAfter = 0;
    int errorCount = 0;
};

ImportAnalysis analyzeWithLoader(const std::string& code, const std::string& dir) {
    ImportAnalysis r;
    fin::DiagnosticEngine diag("", "<test>");
    diag.setColorMode(fin::ColorMode::Never);

    auto parsed = fin::testing::parseSource(code, diag);
    r.parsed = parsed.parsed;
    if (!r.parsed) return r;
    r.importsBefore = countImports(*parsed.ast);

    fin::ModuleLoader loader(dir);
    loader.setDiagnostics(&diag);
    loader.addSearchPath(dir);

    fin::SemanticAnalyzer analyzer(diag, false);
    analyzer.setModuleLoader(&loader);
    analyzer.visit(*parsed.ast);

    r.importsAfter = countImports(*parsed.ast);
    r.errorCount = diag.getErrorCount();
    return r;
}

} // namespace

TEST(ModuleLoader, ANamedImportIsConsumedAndLeavesTheTree) {
    TempModuleDir d;
    d.write("lib.fin", "pub fun helper() <noret> {}\n");

    auto r = analyzeWithLoader("import { helper } from lib;\n"
                               "fun main() <noret> { helper(); }\n",
                               d.path());
    ASSERT_TRUE(r.parsed);
    EXPECT_EQ(r.importsBefore, 1);
    EXPECT_EQ(r.errorCount, 0);
    EXPECT_EQ(r.importsAfter, 0)
        << "a consumed import must not survive into the backend";
}

TEST(ModuleLoader, ANamespaceImportIsConsumedAndLeavesTheTree) {
    TempModuleDir d;
    d.write("lib.fin", "pub fun helper() <noret> {}\n");

    auto r = analyzeWithLoader("import lib as l;\n"
                               "fun main() <noret> {}\n",
                               d.path());
    ASSERT_TRUE(r.parsed);
    EXPECT_EQ(r.importsBefore, 1);
    EXPECT_EQ(r.errorCount, 0);
    EXPECT_EQ(r.importsAfter, 0);
}

TEST(ModuleLoader, AStarImportIsConsumedAndLeavesTheTree) {
    TempModuleDir d;
    d.write("lib.fin", "pub fun helper() <noret> {}\n");

    auto r = analyzeWithLoader("import * from lib;\n"
                               "fun main() <noret> { helper(); }\n",
                               d.path());
    ASSERT_TRUE(r.parsed);
    EXPECT_EQ(r.importsBefore, 1);
    EXPECT_EQ(r.errorCount, 0);
    EXPECT_EQ(r.importsAfter, 0);
}

TEST(ModuleLoader, AnImportOfAMissingModuleIsNotConsumed) {
    TempModuleDir d;

    auto r = analyzeWithLoader("import { helper } from no_such_module;\n"
                               "fun main() <noret> {}\n",
                               d.path());
    ASSERT_TRUE(r.parsed);
    EXPECT_EQ(r.importsBefore, 1);
    EXPECT_GT(r.errorCount, 0);
    EXPECT_EQ(r.importsAfter, 1)
        << "an import that resolved nothing is refused, not dropped";
}

TEST(ModuleLoader, AnImportOfASymbolTheModuleDoesNotExportIsNotConsumed) {
    TempModuleDir d;
    d.write("lib.fin", "pub fun helper() <noret> {}\n");

    auto r = analyzeWithLoader("import { absent } from lib;\n"
                               "fun main() <noret> {}\n",
                               d.path());
    ASSERT_TRUE(r.parsed);
    EXPECT_EQ(r.importsBefore, 1);
    EXPECT_GT(r.errorCount, 0);
    EXPECT_EQ(r.importsAfter, 1)
        << "one unexported name leaves the whole import in the tree";
}
