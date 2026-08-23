#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "Corpus.hpp"
#include "ast/decls/Program.hpp"
#include "diagnostics/DiagnosticEngine.hpp"
#include "semantics/Scope.hpp"
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
