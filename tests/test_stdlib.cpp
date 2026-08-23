#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Corpus.hpp"
#include "driver/SearchPaths.hpp"

// The bundled standard library: `lib/std/*.fin`, found with no flag and no
// environment variable, because `bundledLibraryPaths()` puts `<exe dir>/../lib/std`
// on the search path of every build (src/driver/SearchPaths.hpp).
//
// What the library must contain is not a design decision made here. Sixteen of the
// fifty samples lead with `module not found`, and every one of them names a module and
// imports named symbols from it -- so the corpus states the inventory and this file
// only holds it to account. The table below was extracted from the corpus's own
// `import` lines; if a sample gains an import, the row is owed.
//
// Two suites, the same convention as test_soundness.cpp: `Soundness_*` must pass
// forever, `KnownDefect_*` asserts what is wrong today and a failure is good news.
//
// Note what these tests deliberately do *not* assert: the shape of any library type.
// `Collection<T>` having a `get` that returns `T` is tested by
// tests/samples/stdlib/collection.fin and by the eleven samples that use it, which is
// where a shape assertion belongs -- the corpus is the specification (ADR 0008) and a
// second copy of it here would be a second thing to keep in step. What lives here is
// the *inventory* and the *plumbing*: that the module resolves off the default search
// path, and that the names the corpus imports are the names it exports.

namespace fs = std::filesystem;
using namespace fin::testing;

namespace {

class Src {
public:
    explicit Src(const std::string& contents) {
        path_ = uniqueTempPath("fin_stdlib", ".fin");
        std::ofstream f(path_, std::ios::binary);
        f.write(contents.data(), (std::streamsize)contents.size());
    }
    ~Src() { std::error_code ec; fs::remove(path_, ec); }
    std::string str() const { return path_.string(); }

private:
    fs::path path_;
};

// Compiles a string with *no* library flags and no FIN_LIBS, so the only way an
// import can resolve is the bundled path. Passing --fin-libs here would test the
// flag, which test_cli.cpp already does, and would say nothing about the bundle.
FincRun compileBundled(const std::string& code) {
    Src s(code);
    return runFinc({s.str()}, {{"FIN_LIBS", ""}});
}

size_t errorCount(const std::string& stripped) {
    size_t n = 0;
    for (size_t i = 0; i < stripped.size();) {
        size_t eol = stripped.find('\n', i);
        if (eol == std::string::npos) eol = stripped.size();
        if (stripped.compare(i, 7, "error: ") == 0) ++n;
        i = eol + 1;
    }
    return n;
}

// One row per `import` spelling in the corpus. `path` is written exactly as the
// corpus writes it, `symbols` are the names it selects, and `sample` is where to
// look when a row fails.
//
// The `sample` column is not asserted on -- it is the message a failure prints -- so it
// went wrong quietly: six rows named a sample that does not import them, and one named
// no sample at all ("stdlib/operators.fin's own consumer", which is lambdas.fin). The
// authority is `grep -rn "from operators::std" tests/samples/`, not memory.
struct Row {
    const char* path;      // as written after `from`, e.g. "error::std"
    const char* symbols;   // as written inside the braces
    const char* sample;    // the corpus file that imports it this way
};

const std::vector<Row>& corpusImports() {
    static const std::vector<Row> rows = {
        {"error::std",     "Error",                "enums.fin, readonly.fin, and five more"},
        {"collection::std","Collection",           "prototype_test.fin, stdlib/hashmap.fin"},
        {"hashmap::std",   "HashMap",              "deeptest4.fin, prototype_test.fin"},
        {"operators::std", "Index, IndexAssign",   "stdlib/collection.fin, stdlib/hashmap.fin"},
        {"operators::std", "Addable",              "lambdas.fin"},
        {"stdptr::std",    "rptr",                 "const.fin"},
        {"types::std",     "resolve_type",         "stdlib/prototypes.fin"},
        {"types::std",     "Any",                  "stdlib/stdio.fin"},
        {"types",          "number2str",           "stdlib/memory.fin"},
        {"enums::std",     "getkeyid, keyidof",    "stdlib/typing.fin"},
        {"typing::std",    "IResult",              "stdlib/stdio.fin"},
        {"stdio",          "printf",               "complex.fin, importing.fin"},
    };
    return rows;
}

} // namespace

// The plumbing, end to end, with the real binary: a module in the bundled library
// resolves with nothing on the command line and nothing in the environment.
//
// test_cli.cpp's BundledLibraries tests cover `bundledLibraryPathsFor` as a function
// over temp trees, which is why they pass on a repository with no `lib/std` at all.
// This one fails until the directory exists and the Driver wires it in, and that is
// the point: a search path nobody reads is not a search path.
TEST(Soundness_BundledStdlib, AModuleInTheBundledLibraryResolvesWithNoFlags) {
    const FincRun r = compileBundled(
        "import { Error } from error::std;\n"
        "fun main() <int> { return 0; }\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(err.find("module not found"), std::string::npos)
        << "the bundled stdlib is not on the default search path. Either lib/std does\n"
           "not exist beside the binary's parent directory, or Driver.cpp stopped\n"
           "calling bundledLibraryPaths(). Both of the tests in test_cli.cpp named\n"
           "BundledLibraries.* pass either way, because they test the rule and not the\n"
           "directory.\n"
        << err;
    EXPECT_EQ(r.exitCode, 0) << err;
}

// The inventory. One EXPECT per corpus import spelling, each naming the sample that
// owes it, so a failure says which sample regresses rather than just "stdlib".
TEST(Soundness_BundledStdlib, EverySymbolTheCorpusImportsIsExported) {
    for (const Row& row : corpusImports()) {
        const std::string code =
            std::string("import { ") + row.symbols + " } from " + row.path + ";\n"
            "fun main() <int> { return 0; }\n";
        const std::string err = stripAnsi(compileBundled(code).err);
        EXPECT_EQ(errorCount(err), 0u)
            << "import { " << row.symbols << " } from " << row.path << ";\n"
            << "wanted by " << row.sample << "\n"
            << err;
    }
}

// `import * from somelib;` and `import networking;` -- the two whole-module forms the
// corpus uses, which resolve a module without naming anything inside it.
TEST(Soundness_BundledStdlib, TheWholeModuleImportFormsResolve) {
    for (const char* code : {
             "import networking;\nfun main() <int> { return 0; }\n",
             "import networking as net;\nfun main() <int> { return 0; }\n",
             "import * from somelib;\nfun main() <int> { return 0; }\n",
             "import stdio::std as stdio;\nfun main() <int> { return 0; }\n",
         }) {
        const std::string err = stripAnsi(compileBundled(code).err);
        EXPECT_EQ(errorCount(err), 0u) << code << err;
    }
}

// The `::` tail is parsed and dropped (parser.y's note on `namespace_block`), so
// `error::std` and `error::nosuchnamespace` load the same file and neither complains.
// That is the state of namespaces today, and it is asserted rather than left implicit
// because the bundled library is written *with* `namespace std { ... }` blocks -- the
// spelling the corpus's own drafts use -- and it would be easy to conclude from a
// green suite that the selector was doing work.
TEST(KnownDefect_BundledStdlib, TheNamespaceSelectorInAnImportIsNotChecked) {
    const std::string err = stripAnsi(compileBundled(
        "import { Error } from error::nosuchnamespace;\n"
        "fun main() <int> { return 0; }\n").err);
    EXPECT_EQ(errorCount(err), 0u)
        << "GOOD NEWS: the namespace selector in an import is resolved. Invert this\n"
           "test -- `error::nosuchnamespace` must report an unknown namespace, and\n"
           "`error::std` must still compile clean -- and rename it\n"
           "Soundness_BundledStdlib.TheNamespaceSelectorInAnImportIsChecked.\n"
           "Doing that needs a NamespaceDeclaration node with a scope; today\n"
           "namespace_block splices its contents and discards the name, and\n"
           "ImportModule::namespace_path carries the tail to no reader\n"
           "(`grep -rn namespace_path src/` finds only the declaration).\n"
        << err;
}
