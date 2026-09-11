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

// The bundled library, compiled to a real executable and run. Returns the program's
// stdout, or the compiler's stderr prefixed with `compile failed:` -- one string, so a
// test that expected output prints the reason it did not get any instead of an empty
// diff.
//
// Here rather than in test_codegen.cpp, which has the same shape twice over, because
// what these tests are about is the *bundle*: `build` there passes no environment and
// declares `printf` in the source, and the fact under test here is that the declaration
// arrives from lib/std/stdio.fin and the source declares nothing.
//
// `extra` goes on the command line between the source and `-o`, for the tests that need
// a second file: `-I <dir>` *adds* to the search paths where `--fin-libs` replaces them,
// and replacing them would take the bundle -- and with it the ambient `printf` those
// programs print through -- out of the run.
std::string buildAndRun(const std::string& code, const std::vector<std::string>& extra) {
    Src src(code);
    const fs::path exe = uniqueTempPath("fin_stdlib_exe");
    std::vector<std::string> args{src.str()};
    args.insert(args.end(), extra.begin(), extra.end());
    args.push_back("-o");
    args.push_back(exe.string());
    const FincRun c = runFinc(args, {{"FIN_LIBS", ""}});
    if (c.exitCode != 0 || !fs::exists(exe)) {
        return "compile failed: " + stripAnsi(c.err);
    }
    const fs::path outPath = uniqueTempPath("fin_stdlib_out");
    const std::string cmd = "'" + exe.string() + "' > '" + outPath.string() + "' 2>&1";
    const int status = std::system(cmd.c_str());
    const std::string out = readWholeFile(outPath.string());
    std::error_code ec;
    fs::remove(exe, ec);
    fs::remove(outPath, ec);
#ifdef WIFEXITED
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return "program exited non-zero, output: " + out;
#else
    if (status != 0) return "program exited non-zero, output: " + out;
#endif
    return out;
}

std::string buildBundledAndRun(const std::string& code) { return buildAndRun(code, {}); }

// The same, with a directory of extra modules on the search path.
std::string buildWithModuleAndRun(const std::string& code, const std::string& includeDir) {
    return buildAndRun(code, {"-I", includeDir});
}

// Compiles a program against the bundle and returns its ANSI-stripped diagnostics.
std::string bundledErr(const std::string& code) {
    return stripAnsi(compileBundled(code).err);
}

// finc's own diagnostics for a *build* rather than a check. The `-o` is the whole
// difference: `finc file.fin` stops after the front end, so a codegen refusal and a link
// failure are both absent from its output, and a test that expects either one cannot use
// `compileBundled`.
std::string buildErr(const std::string& code, const std::vector<std::string>& extra = {}) {
    Src src(code);
    const fs::path exe = uniqueTempPath("fin_stdlib_exe");
    std::vector<std::string> args{src.str()};
    args.insert(args.end(), extra.begin(), extra.end());
    args.push_back("-o");
    args.push_back(exe.string());
    const FincRun c = runFinc(args, {{"FIN_LIBS", ""}});
    std::error_code ec;
    fs::remove(exe, ec);
    return stripAnsi(c.err);
}

std::string buildErrWithModule(const std::string& code, const std::string& includeDir) {
    return buildErr(code, {"-I", includeDir});
}

// A throwaway directory of .fin modules, for the tests here that need a second file
// the bundle does not provide. test_module_loader.cpp has the same class against
// the loader's C++ API; this copy drives the real binary and the two share nothing
// else.
class TempModuleDir {
public:
    TempModuleDir() {
        dir_ = uniqueTempPath("fin_stdlib_mods");
        fs::create_directories(dir_);
    }
    ~TempModuleDir() { std::error_code ec; fs::remove_all(dir_, ec); }

    void write(const std::string& name, const std::string& contents) {
        const fs::path p = dir_ / name;
        std::ofstream f(p, std::ios::binary);
        f.write(contents.data(), (std::streamsize)contents.size());
    }
    std::string path() const { return dir_.string(); }

private:
    fs::path dir_;
};

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

// The whole-module import forms the corpus uses, which resolve a module without naming
// anything inside it.
//
// `import * from somelib;` was a fourth row here and is gone, because the module is:
// `4d79ae7` deleted `lib/std/somelib/index.fin` with the message "somelib isnt a real
// stdlib module", and a row asserting that a deleted module resolves is a row asserting
// the deletion was a mistake. It was not -- the directory existed only to exercise
// `<base>/<name>/index.fin` resolution, and that candidate is covered against temp trees
// by test_cli.cpp's library-path tests and by test_module_loader.cpp, neither of which
// needs a directory inside the shipped library to do it. `import * from` itself is held by
// Soundness_Imports.ImportStarBindsEveryValueTheModuleDeclares and its two siblings in
// test_cli.cpp, which build their own module rather than borrowing one from the bundle.
//
// What the deletion does cost is two diagnostics in `tests/samples/importing.fin`, which
// writes `import "somelib";` and `import * from somelib;` and now reports `module not
// found` for both. Its expectation is `unimplemented`, so the corpus stays green; the
// sample is the specification (ADR 0008) and it asks for a module the library no longer
// ships, which is an owner question and not this file's to answer.
TEST(Soundness_BundledStdlib, TheWholeModuleImportFormsResolve) {
    for (const char* code : {
             "import networking;\nfun main() <int> { return 0; }\n",
             "import networking as net;\nfun main() <int> { return 0; }\n",
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

// ---------------------------------------------------------------------------
// Calling through a module qualifier.
//
// complex.fin:14 writes `stdio.printf("Big")` with its own comment saying "uses stdio
// printf", and it reported `Type 'module<stdio>' does not have methods`. Reading a
// module member already worked -- visit(MemberAccess&) has a NamespaceType branch and
// `stdio.nosuch` reports `Namespace 'stdio' has no exported member 'nosuch'` -- but
// visit(MethodCall&) went straight to getStructType, which knows nothing about
// namespaces, so a member could be named and not called.
//
// These live here rather than in test_soundness.cpp because they need a module, and a
// module means either the bundle or a two-file fixture. The bundle is the honest choice:
// `stdio` exports `printf` because lib/std/stdio.fin declares it, and if that ever stops
// being true these tests should say so.

TEST(Soundness_Modules, AModuleFunctionIsCallableThroughADot) {
    const std::string err = stripAnsi(compileBundled(
        "import stdio;\n"
        "fun main() <int> { stdio.printf(\"hi\\n\"); return 0; }\n").err);
    EXPECT_EQ(errorCount(err), 0u) << err;
}

TEST(Soundness_Modules, AModuleCallIsCheckedAgainstTheSignatureItResolvedTo) {
    // The failure mode a namespace branch invites: resolve the member, then call it
    // without looking at what it is. `println` takes one argument (lib/std/stdio.fin),
    // so passing none must still be an arity error -- otherwise the branch is a hole in
    // the call checking rather than a route into it.
    //
    // Not `printf`, which is the member complex.fin actually calls: it is variadic, and
    // a variadic signature is exempt from the arity check entirely
    // (checkCallArguments's `!sig.is_vararg` guard, and mutating that guard away kills
    // four corpus samples). A vararg callee would have made this test green against a
    // branch that skipped checking altogether.
    const std::string err = stripAnsi(compileBundled(
        "import stdio;\n"
        "fun main() <int> { stdio.println(); return 0; }\n").err);
    EXPECT_EQ(errorCount(err), 1u) << err;
    EXPECT_NE(err.find("expects 1 arguments, got 0"), std::string::npos)
        << "a call through a module must be checked like any other:\n" << err;
}

TEST(Soundness_Modules, AnUnknownModuleMemberIsReportedWhetherReadOrCalled) {
    // Read and called must agree, and both must name the namespace. Before this the two
    // spellings gave different diagnostics for the same mistake: the read said which
    // module had no such export, the call said the module had no methods.
    for (const char* code : {"import stdio;\nfun main() <int> { let x <int> = stdio.nosuch; return 0; }\n",
                             "import stdio;\nfun main() <int> { stdio.nosuch(); return 0; }\n"}) {
        const std::string err = stripAnsi(compileBundled(code).err);
        EXPECT_NE(err.find("has no exported member 'nosuch'"), std::string::npos) << code << err;
    }
}

TEST(KnownDefect_Modules, AModuleStructIsNotConstructibleThroughADot) {
    // `stdio.IOError("x")` resolves the name -- lib/std/stdio.fin exports the struct --
    // and then finds no FunctionType behind it, because a struct's constructors live on
    // its StructType and the namespace branch only looks in the value table.
    //
    // Left alone because the dot is not the only half that is missing: `let e <stdio.IOError>;`
    // is a *syntax* error (`unexpected DOT, expecting GT`), so a module's types cannot be
    // named in a type position at all, and making them constructible while they stay
    // unwritable would be a strange place to stop. The corpus writes neither -- complex.fin
    // needs only the function call -- so both wait on whoever rules on namespace
    // semantics. Inverts into Soundness_Modules.AModuleStructIsConstructibleThroughADot
    // together with the type-position grammar.
    const std::string err = stripAnsi(compileBundled(
        "import stdio;\n"
        "fun main() <int> { stdio.IOError(); return 0; }\n").err);
    EXPECT_NE(errorCount(err), 0u)
        << "FIXED: a module's struct is now constructible through a dot. Check that "
           "`let e <stdio.IOError>;` parses too, then invert this.\n"
        << err;
}

// ---------------------------------------------------------------------------
// Lowering a call written through a module qualifier (HANDOFF section 6, item 5).
//
// The block above is the front end's half: `stdio.printf("Big")` resolves, and is
// checked. complex.fin still did not *build* -- codegen's `visit(MethodCall&)` asks
// `baseAddress` for the receiver's address, a module has none, and the refusal was `the
// receiver of a call to the method 'printf' on a value with no address is not lowered
// yet`. The backend holds no reference to `NamespaceType` and that is the design, so the
// qualifier is spent in the front end instead: `SemanticAnalyzer::lowerModuleCall`
// resolves the call to a plain `printf("Big")` and leaves it on the node
// (`MethodCall::resolved_call`), and the backend's first statement lowers that.
//
// What these have to say beyond "it compiles", because a rewrite that dropped the
// arguments or reached some other symbol of the same shape compiles just as cleanly:
//
//   * The value arrives, in more than one expression position.
//   * Only a name the root program will declare *for the same declaration* is rewritten.
//     One mechanism puts a module's declaration into the root program -- `#[global]` and
//     its prototype splice (ADR 0021) -- so the gate is `Symbol::is_ambient`, set where
//     the prototype was retained. A wider gate would trade a refusal for a link failure,
//     which is the worse failure because it arrives after a successful compile.
//   * The module's signature still answers for the call, not the file's own. That is the
//     two-`printf` hazard complex.fin is built out of, and the ordering is the whole
//     answer: `checkCallArguments` runs before `lowerModuleCall`, never after.

namespace {

// A module publishing one fixed-arity ambient extern bound to C's `abs`, so a rewritten
// call has a value to be wrong about -- `printf` cannot say this, because a vararg
// signature is exempt from the arity check and its result is `<noret>`. `#[global]` is
// legal only inside `namespace std` (ADR 0021), and the export is what makes the file
// worth importing.
const char* const kAbsModule =
    "namespace std {\n"
    "#[llvm_name=\"abs\"]\n"
    "#[global]\n"
    "@define c_abs(v: int) <int>;\n"
    "}\n"
    "pub fun anchor() <noret> {}\n";

// Two modules publishing one ambient name, same signature, different symbols. Both are
// accepted -- `publishIfGlobal` refuses a second `#[global]` of a name only when the two
// *types* differ -- and one prototype is spliced, so one of the two qualifiers is
// rewritable and it is not the one the writer happens to spell.
const char* const kTwinAsAbs =
    "namespace std {\n"
    "#[llvm_name=\"abs\"]\n"
    "#[global]\n"
    "@define twin(v: int) <int>;\n"
    "}\n"
    "pub fun a_anchor() <noret> {}\n";

// The loser names a symbol nothing defines, deliberately: an assertion that the losing
// qualifier is refused fails loudly if the gate ever widens, and so does the linker. A
// second real symbol would give a plausible answer instead -- the first probe here paired
// `abs` with `labs`, and the mis-called version printed -7 rather than failing, because a
// 32-bit argument in a `long` parameter is an ABI accident and not a diagnosis.
const char* const kTwinAsAbsentSymbol =
    "namespace std {\n"
    "#[llvm_name=\"fin_no_such_symbol\"]\n"
    "#[global]\n"
    "@define twin(v: int) <int>;\n"
    "}\n"
    "pub fun b_anchor() <noret> {}\n";

} // namespace

TEST(Soundness_Modules, AModuleCallIsCheckedAgainstTheModulesSignatureAndNotTheFilesOwn) {
    // complex.fin in miniature: its :5 declares `@define printf(fmt: string, ...) <int>;`
    // and its :3 imports a `stdio` whose `printf` is `<noret>` (lib/std/stdio.fin:111), so
    // one file holds two `printf`s of different return type and the qualified spelling
    // must be the module's.
    //
    // A rewrite performed *before* the check would hand a bare `printf` to
    // `checkCallArguments`, which would find the file's own `<int>` declaration and pass
    // -- so this program compiling is exactly the "silently picks one" the queue entry
    // warns about, and it is why the namespace branch checks first and rewrites second.
    const std::string qualified = bundledErr(
        "import stdio;\n"
        "@define printf(fmt: string, ...) <int>;\n"
        "fun main() <noret> { let a <int> = stdio.printf(\"hi\\n\"); }\n");
    EXPECT_NE(qualified.find("expected 'int', got 'void'"), std::string::npos)
        << "a module-qualified call must be typed by the module's signature. If this\n"
           "compiled, the rewrite ran before the check and the file's own `<int>` printf\n"
           "answered for the module's `<noret>` one.\n"
        << qualified;

    // The other half, and what makes the first evidence rather than a tautology: the
    // *plain* name in the same file is the file's own declaration, and reading it as an
    // `int` is right. A branch that refused both would satisfy the assertion above.
    const std::string plain = bundledErr(
        "import stdio;\n"
        "@define printf(fmt: string, ...) <int>;\n"
        "fun main() <noret> { let a <int> = printf(\"hi\\n\"); }\n");
    EXPECT_EQ(errorCount(plain), 0u)
        << "the plain name is the file's own `<int>` printf and must still type as one:\n"
        << plain;
}

TEST(Soundness_Modules, AnAmbientMemberCalledThroughAQualifierIsStillCheckedForArity) {
    // The rewrite *moves* the argument list into the call it builds, so the check has to
    // have already happened to it. It has: this fails in the front end, before the
    // qualifier is spent and before anything is lowered.
    TempModuleDir d;
    d.write("absmod.fin", kAbsModule);
    const std::string err = buildErrWithModule(
        "import absmod as am;\n"
        "fun main() <noret> { am.c_abs(); }\n", d.path());
    EXPECT_NE(err.find("Function 'am.c_abs' expects 1 arguments, got 0"), std::string::npos)
        << "a call is checked before it is rewritten, and the diagnostic still names the\n"
           "qualifier the program wrote:\n"
        << err;
}

TEST(KnownDefect_Modules, AnImportedExternThatIsNotAmbientIsNotLoweredThroughADot) {
    // `io_fflush` is one of lib/std/stdio.fin's own externs and is deliberately not
    // `#[global]` (:113: "the marked set is two names and minting a third would be a hole
    // in the module system rather than a convenience"). No prototype for it is spliced
    // into the root program, so there is nothing for a plain `io_fflush(...)` to be a call
    // *to*, and it keeps the refusal it had.
    const std::string plain = buildErr(
        "import stdio;\n"
        "fun main() <noret> { let r <int> = stdio.io_fflush(null); }\n");
    EXPECT_NE(plain.find("the receiver of a call to the method 'io_fflush' on a value "
                         "with no address"), std::string::npos)
        << "GOOD NEWS: a non-ambient imported extern lowers through a qualifier. Check\n"
           "first that it reached the *module's* declaration and that the symbol it asks\n"
           "the linker for is the one lib/std/stdio.fin names, then invert this into\n"
           "Soundness_Modules.AnImportedExternIsLoweredThroughADot.\n"
        << plain;

    // The half that makes this discriminating rather than decorative: the file declares
    // `io_fflush` itself, so the root program *does* bind that name -- and the call must
    // still refuse. A gate written as "the root program binds this name" instead of
    // "binds this name to this declaration" would rewrite here, and the rewritten call
    // would go to the file's own declaration under the symbol `io_fflush`, which no C
    // library defines: a clean compile followed by `undefined reference`. Refusing before
    // the object file exists is the rule the corpus is held to -- a construct the backend
    // cannot lower is refused, never skipped.
    const std::string declared = buildErr(
        "import stdio;\n"
        "@define io_fflush(handle: &void) <int>;\n"
        "fun main() <noret> { let r <int> = stdio.io_fflush(null); }\n");
    EXPECT_NE(declared.find("the receiver of a call to the method 'io_fflush' on a value "
                            "with no address"), std::string::npos)
        << "a file declaring the name itself must not make the module's member\n"
           "lowerable: the two are different declarations, and only the ambient one is\n"
           "put into the root program by anything other than this file.\n"
        << declared;
}

TEST(Soundness_Modules, AnImportedGenericStructTemplateIsFoundAndChecked) {
    // Was KnownDefect_Modules.AnImportedGenericStructsConstructorIsNotLowered:
    // `HashMap::<string, Data>()` (deeptest4.fin:11) refused with
    // "a call to 'HashMap'" because the backend had never heard the name. The
    // imported-declaration decision (ADR 0032) landed since, and `#[export]`
    // is import-visibility only (ADR 0033), so the template is found, checked
    // and instantiated -- and the first refusal moved into the instantiation
    // itself, the `hasher` field's `fn(any) -> int` type. What remains is
    // instantiation: `any`/`fn` field types, transitive callees, and bodies.
    const std::string err = buildErr(
        "import { HashMap } from hashmap::std;\n"
        "fun main() <noret> { let a <auto> = HashMap::<string, int>(); }\n");
    EXPECT_NE(err.find("a field of 'HashMap<string, int>' of type 'fn(any) -> int'"),
              std::string::npos)
        << "the imported template must be found, checked and instantiated,\n"
           "not missed\n"
        << err;
    // And specifically not the old misses, either of which would mean the
    // registry lookup regressed rather than the check moved on.
    EXPECT_EQ(err.find("a call to 'HashMap'"), std::string::npos) << err;
    EXPECT_EQ(err.find("the attribute 'export' on struct 'HashMap'"),
              std::string::npos)
        << err;
    // And specifically not the turbofish refusal, which is what this said before the
    // generic-constructor unit and which would now name the wrong gap.
    EXPECT_EQ(err.find("explicit generic arguments"), std::string::npos) << err;
}

TEST(KnownDefect_Modules, AnImportedFinFunctionIsNotLoweredThroughADot) {
    // The other half of the same gap, and the larger one: a Fin function with a body in a
    // loaded module is not lowered under any spelling. Its definition stays in the
    // loader's `astStorage`, where the backend never looks, so the root object has no copy
    // of it -- this waits on separate compilation, not on the qualifier, and rewriting it
    // would only move the refusal onto a different name.
    const std::string err = buildErr(
        "import stdio;\n"
        "fun main() <noret> { stdio.println(\"x\"); }\n");
    EXPECT_NE(err.find("the receiver of a call to the method 'println' on a value with no "
                       "address"), std::string::npos)
        << "GOOD NEWS: an imported Fin function lowers. That is separate compilation\n"
           "rather than a namespace fix -- check the module's object is actually linked\n"
           "in before inverting this.\n"
        << err;
}

#ifdef FIN_TESTS_HAVE_BACKEND

TEST(Soundness_Modules, TheModulesPrintfLowersAndRunsThroughAQualifier) {
    // complex.fin's construct with a value asserted instead of a compile. The sample
    // prints a bare "Big" and so says nothing about its argument; a `%d` does, because
    // nothing but the C library's printf formats one.
    EXPECT_EQ(buildBundledAndRun(
                  "import stdio::std as stdio;\n"
                  "fun main() <noret> { stdio.printf(\"%d\\n\", 41 + 1); }\n"),
              "42\n");
}

TEST(Soundness_Modules, AQualifiedCallLowersWhereverAnExpressionGoes) {
    // A rewritten call is an expression, not a statement form: one here initialises a
    // local, one is an argument to another call. Both, because the rewrite is recorded on
    // the node rather than replacing the node in its parent's slot -- if that made the
    // lowering depend on where the call was written, this is where it would show.
    //
    // `abs` is the symbol, so the answers are 5 and 9 rather than -5 and -9: a rewrite
    // that dropped its argument, or reached a different symbol, compiles and prints
    // something else.
    TempModuleDir d;
    d.write("absmod.fin", kAbsModule);
    EXPECT_EQ(buildWithModuleAndRun("import absmod as am;\n"
                                    "fun main() <noret> {\n"
                                    "    let v <int> = am.c_abs(0 - 5);\n"
                                    "    printf(\"%d %d\\n\", v, am.c_abs(0 - 9));\n"
                                    "}\n", d.path()),
              "5 9\n");
}

TEST(Soundness_Modules, AQualifiedCallReachesTheSymbolTheRetainedPrototypeNames) {
    // Two modules publish `twin` under different symbols and exactly one prototype is
    // retained -- the first, in import order (`ModuleLoader::retainAmbientPrototype`). So
    // `amod` is what the root program declares `twin` as, `a.twin(-7)` is 7, and
    // `b.twin(-7)` must refuse rather than reach `abs`: rewriting it would answer a call
    // on `bmod`'s declaration with `amod`'s symbol, and no diagnostic anywhere would
    // mention that the two modules had disagreed.
    //
    // This is the test `ModuleLoader::symbolOf`'s comment names as the one that goes red
    // if the loader's reading of `#[llvm_name]` and `CodeGen_LLVM::symbolNameOf`'s ever
    // drift apart -- the two agreeing is what makes a retained prototype's symbol
    // knowable from the loader at all.
    TempModuleDir d;
    d.write("amod.fin", kTwinAsAbs);
    d.write("bmod.fin", kTwinAsAbsentSymbol);
    EXPECT_EQ(buildWithModuleAndRun("import amod as a;\n"
                                    "import bmod as b;\n"
                                    "fun main() <noret> { printf(\"%d\\n\", a.twin(0 - 7)); }\n",
                                    d.path()),
              "7\n");

    const std::string err = buildErrWithModule(
        "import amod as a;\n"
        "import bmod as b;\n"
        "fun main() <noret> { printf(\"%d\\n\", b.twin(0 - 7)); }\n", d.path());
    EXPECT_NE(err.find("the receiver of a call to the method 'twin' on a value with no "
                       "address"), std::string::npos)
        << "the losing module's qualifier must not be rewritten: the name the root\n"
           "program declares is the other module's declaration, under the other module's\n"
           "symbol. Swapping the two imports swaps which module wins, which is what says\n"
           "the retention is first-wins rather than alphabetical.\n"
        << err;
}

TEST(KnownDefect_Modules, RenamingAnAmbientNameInTheRootFileBreaksBothSpellingsAlike) {
    // The residual hazard, booked rather than repaired, and bookable because the two
    // spellings now agree. A file that redeclares an ambient name wins in
    // `declareFunction` -- first declaration wins, and the splice is appended -- so the
    // plain call goes to *its* symbol, and after the rewrite the qualified call goes there
    // too. Here that symbol is `twin`, which no C library defines, and both fail at the
    // link.
    //
    // Not repaired here because it is not a namespace fault: the plain call in this file
    // was already broken before any rewrite existed, and the second assertion is what
    // says so. Making the qualified spelling link while the plain one does not, in one
    // file, would be worse than both failing. It is `#[overwrite]`'s question -- which of
    // two declarations of a name wins, and under whose symbol -- and it is due whoever
    // rules on that. `printf` never shows it because the corpus samples that redeclare it
    // write no `#[llvm_name]`, so their symbol and the bundle's are the same string.
    TempModuleDir d;
    d.write("amod.fin", kTwinAsAbs);
    const std::string plain = buildErrWithModule(
        "import amod as a;\n"
        "@define twin(v: int) <int>;\n"
        "fun main() <noret> { printf(\"%d\\n\", twin(0 - 7)); }\n", d.path());
    EXPECT_NE(plain.find("link failed"), std::string::npos)
        << "the plain call is the baseline this defect is measured against, and it is\n"
           "expected to fail the link: the file's own `twin` wins and names a symbol\n"
           "nothing defines.\n"
        << plain;

    const std::string qualified = buildErrWithModule(
        "import amod as a;\n"
        "@define twin(v: int) <int>;\n"
        "fun main() <noret> { printf(\"%d\\n\", a.twin(0 - 7)); }\n", d.path());
    EXPECT_NE(qualified.find("link failed"), std::string::npos)
        << "GOOD NEWS, maybe: the qualified spelling no longer fails the way the plain\n"
           "one does. Check which symbol it reached. If it reached the module's, then two\n"
           "spellings of one call in one file now disagree, which is a worse state than\n"
           "this defect -- read the note above before inverting anything.\n"
        << qualified;
}

#endif  // FIN_TESTS_HAVE_BACKEND

// ---------------------------------------------------------------------------
// `#[global]` against the real bundle (ADR 0021).
//
// These belong here rather than in test_soundness.cpp for the reason that file's own
// `#[global]` block gives: the interesting cases need a *second* file, and a harness
// that compiles one string has none. The bundle is the second file, and it is the
// honest one -- `printf` is ambient because `lib/std/stdio.fin:109` marks it, and if
// that line is ever deleted these must go red rather than a corpus sample quietly
// gaining a diagnostic.
//
// `compileBundled` clears `FIN_LIBS`, so the only way any of this resolves is the
// bundled path the driver adds for itself. That matters here more than elsewhere:
// the ambient declaration is reached by the driver *preloading* the stdio module
// before it analyses the root file, and a preload against a search path the test
// supplied would say nothing about what a user gets.

TEST(Soundness_GlobalAttribute, TheBundledPrintfResolvesWithNoImport) {
    // The two corpus sites this exists for are `const.fin:68` and `interfaces.fin:18`,
    // which call `printf` bare and declare nothing. ADR 0021 promises both resolve with
    // no edit to either sample; this is that promise as one line, so a failure names
    // the mechanism instead of arriving as two sample expectations flipping.
    const FincRun r = compileBundled("fun main() <noret> { printf(\"hi\\n\"); }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

TEST(Soundness_GlobalAttribute, ANonGlobalStdNameStillNeedsItsImport) {
    // The ambience half, answered no. `getkeyid` is `pub` inside `#[export]` inside
    // `namespace std` at lib/std/enums.fin:24 -- every property `printf` has except the
    // attribute -- so it is exactly the "everything else" the owner's ruling keeps
    // import-only, and it is the test that says the shared scope carries the marked set
    // and not a module.
    //
    // A stronger claim than it looks: `enums` is not preloaded at all, so this would
    // pass against a broken implementation that published every `pub` name of every
    // *loaded* module. The next test closes that.
    const std::string err = stripAnsi(
        compileBundled("fun main() <noret> { let x <int> = getkeyid(1); }\n").err);
    EXPECT_NE(err.find("Undefined function or type 'getkeyid'"), std::string::npos)
        << "a `pub` name in `namespace std` that is not marked #[global] must still be\n"
           "imported (ADR 0021, the ambience half). If this went green because the\n"
           "shared scope now takes a module's whole `symbols` map, that answers the\n"
           "half the owner answered no.\n"
        << err;
}

TEST(Soundness_GlobalAttribute, TheStdioModulesOwnUnmarkedExportsStayImportOnly) {
    // The case above with the loading question removed. `stdio` *is* preloaded, so
    // `Printable`, `print`, `println`, `IOError` and `Stream` are all analysed and all
    // sitting in a scope the root file's lookup can reach the parent of -- and every one
    // of them must still be invisible, because only the stamped declaration is
    // published. This is what holds `publishIfGlobal` to the stamp rather than to the
    // module: a publish that copied the loaded scope would leave the test above green
    // and every name here would leak.
    for (const char* name : {"print", "println", "Printable", "IOError", "Stream"}) {
        const std::string code =
            std::string("fun main() <noret> { let x <int> = ") + name + "(1); }\n";
        const std::string err = stripAnsi(compileBundled(code).err);
        EXPECT_NE(errorCount(err), 0u)
            << "`" << name << "` is exported by the preloaded stdio module and is not\n"
               "marked #[global], so it must not resolve without an import.\n"
            << err;
    }
}

TEST(Soundness_GlobalAttribute, AnAmbientNameReachesAnImportedModuleToo) {
    // "Visible to every file in the compiler session" includes the files the session
    // loads on the user's behalf, and this is the case the shared scope was built for
    // rather than a consequence of it: a module is analysed by its *own*
    // SemanticAnalyzer, so an implementation that put the ambient binding in the root
    // analyzer's scope would pass every test above and fail here.
    //
    // `helper` calls `printf` and imports nothing. It resolves only if the loader's
    // scope is the parent of the module analyzer's scope as well as the root's.
    // `-I` rather than `--fin-libs`, because the flag *replaces* the library paths
    // and would take the bundle -- and with it the ambient declaration -- out of the
    // run this test is about. An include path is additive and leaves the bundle
    // exactly where the driver puts it (Driver.cpp's configureLoader, steps 1-3).
    TempModuleDir d;
    d.write("helper.fin", "pub fun shout() <noret> { printf(\"loud\\n\"); }\n");
    Src s("import { shout } from helper;\n"
          "fun main() <noret> { shout(); }\n");
    const FincRun r = runFinc({s.str(), "-I", d.path()}, {{"FIN_LIBS", ""}});
    EXPECT_EQ(r.exitCode, 0)
        << "an ambient declaration must reach a module the session loaded, not just\n"
           "the root file.\n"
        << stripAnsi(r.err);
}

TEST(Soundness_GlobalAttribute, AnUnmarkedExternInALoadedModuleIsNotPublished) {
    // The stamp is what publishes, not the declaration form -- and this is the only
    // test in either file that says so, which is why it is written against a module
    // rather than folded into one of the cases above.
    //
    // Measured, not assumed. A mutant that dropped the attribute check from
    // `publishIfGlobal` and published every `@define` passed all seventeen cases in
    // test_soundness.cpp's `#[global]` block and every other case here: the unmarked
    // declarations they look at are a `fun`, an `interface` and two structs, and the
    // one `@define` in `lib/std/stdio.fin` is the marked one. So the leak this rules
    // out had no witness anywhere until this file got a module with a second `@define`
    // in it.
    //
    // `absent` is `@define`d and unmarked, `anchor` is what the import is for -- an
    // import that binds nothing would fail for its own reasons and say nothing about
    // the extern.
    TempModuleDir d;
    d.write("externs.fin",
            "@define absent(fmt: string) <noret>;\n"
            "pub fun anchor() <noret> {}\n");
    Src s("import { anchor } from externs;\n"
          "fun main() <noret> { anchor(); absent(\"x\"); }\n");
    const std::string err = stripAnsi(runFinc({s.str(), "-I", d.path()},
                                              {{"FIN_LIBS", ""}}).err);
    EXPECT_NE(err.find("Undefined function or type 'absent'"), std::string::npos)
        << "an `@define` in a loaded module that carries no #[global] must stay local\n"
           "to that module. #[global] is opt-in per declaration (ADR 0021), so what\n"
           "publishes is the stamp and never the declaration form.\n"
        << err;
}

TEST(Soundness_GlobalAttribute, AFileMayDeclareTheAmbientPrintfItself) {
    // Fourteen corpus samples write `@define printf(fmt: string, ...) <noret>;`
    // verbatim, so the ambient declaration lands on top of a local one in most of the
    // corpus and must not turn any of them into a duplicate-global error. The identical
    // signature is the point: test_soundness.cpp's
    // TwoGlobalsOfOneNameWithDifferentTypesAreRefused is the other side, and this pins
    // that the check compares types rather than counting declarations.
    const FincRun r = compileBundled(
        "@define printf(fmt: string, ...) <noret>;\n"
        "fun main() <noret> { printf(\"hi\\n\"); }\n");
    EXPECT_EQ(r.exitCode, 0) << stripAnsi(r.err);
}

// ---------------------------------------------------------------------------
// The backend half of the ambient declaration.
//
// Publishing `printf` into the shared scope is what makes a call to it type-check in a
// file that imports nothing. It is not what makes the call *link*: `declareTopLevel`
// walks the root program's own statements, and the declaration that named the C symbol
// is in `lib/std/stdio.fin`, whose AST the loader keeps in `astStorage` where the
// backend never looks. So `finc hello.fin` exited 0 and `finc hello.fin -o hello`
// refused with `codegen: a call to 'printf' is not lowered yet` -- in a file the
// compiler had just told needed no import.
//
// The driver closes it by splicing a prototype for each ambiently-published extern into
// the root program between the front end and the backend
// (ModuleLoader::appendAmbientPrototypes). These tests are the ones that would go red if
// that splice went away, and they are written as build-and-run because the failure they
// replaced was a *link* failure: a compile that exits 0 proves nothing about it.
//
// Guarded, because a build with FIN_WITH_LLVM=OFF has no backend to link with and ADR
// 0010 keeps that configuration supported. The front-end tests above run either way.
#ifdef FIN_TESTS_HAVE_BACKEND

TEST(Soundness_GlobalAttribute, TheAmbientPrintfLinksAndRuns) {
    // The exit criterion for the ambient name, and the shortest program that states it:
    // no import, no declaration, one call. `const.fin:68` and `interfaces.fin:18` are
    // the corpus sites, and until this held neither could be built.
    EXPECT_EQ(buildBundledAndRun("fun main() <noret> { printf(\"hi\\n\"); }\n"),
              "hi\n");
}

TEST(Soundness_GlobalAttribute, TheAmbientPrintfCallsTheCLibrarysPrintfAndNotItsFinName) {
    // The prototype carries `#[llvm_name="printf"]` and it has to survive being copied,
    // which is not automatic: `CloneVisitor::visit(DefineDeclaration&)` did not clone the
    // `attributes` vector at all -- nine other declaration visits did -- so a clone
    // produced a prototype under the Fin name with no rename on it. Here the two names
    // are the same string and the bug would be invisible, so this asserts on the *format
    // string* instead: a call that reached the C library formats `%d`, and one that
    // reached anything else does not.
    EXPECT_EQ(buildBundledAndRun(
                  "fun main() <noret> { printf(\"%d-%s\\n\", 7, \"seven\"); }\n"),
              "7-seven\n");
}

TEST(Soundness_GlobalAttribute, AFileThatDeclaresPrintfItselfStillGetsOneSymbol) {
    // Fourteen corpus samples write the `@define` themselves while the bundle publishes
    // it, so the splice lands on top of a local declaration in most of the corpus. One
    // declaration has to win: `declareFunction` keeps the first (`if
    // (functions_.count(name)) return;`), and two `llvm::Function::Create` calls for one
    // name would otherwise give the second a `printf.1` that nothing defines.
    EXPECT_EQ(buildBundledAndRun("@define printf(fmt: string, ...) <noret>;\n"
                                 "fun main() <noret> { printf(\"both\\n\"); }\n"),
              "both\n");
}

TEST(Soundness_GlobalAttribute, AnAmbientExternIsSplicedForItsLlvmNameAndNotItsFinName) {
    // The rename, with the two names actually different, which the bundle cannot show:
    // its `printf` is renamed to `printf`. A module publishes `shout` bound to C's
    // `puts`, and the program calls `shout`. If the splice dropped `#[llvm_name]` the
    // object would ask for a symbol called `shout`, and the link would fail -- so this
    // running at all is the assertion.
    //
    // `-I` rather than `--fin-libs`: the flag replaces the library paths and would take
    // the bundle out of the run, and `main` is not the point here.
    TempModuleDir d;
    d.write("shouter.fin",
            "namespace std {\n"
            "#[llvm_name=\"puts\"]\n"
            "#[global]\n"
            "@define shout(msg: string) <noret>;\n"
            "}\n"
            "pub fun anchor() <noret> {}\n");
    Src s("import { anchor } from shouter;\n"
          "fun main() <noret> { shout(\"loud\"); }\n");
    const fs::path exe = uniqueTempPath("fin_stdlib_shout");
    const FincRun c = runFinc({s.str(), "-I", d.path(), "-o", exe.string()},
                              {{"FIN_LIBS", ""}});
    ASSERT_EQ(c.exitCode, 0)
        << "an ambiently-published extern must reach the backend with its #[llvm_name]\n"
           "intact, or the object asks for a symbol nothing defines.\n"
        << stripAnsi(c.err);
    ASSERT_TRUE(fs::exists(exe));
    std::error_code ec;
    fs::remove(exe, ec);
}

TEST(Soundness_GlobalAttribute, AnUnmarkedExternInALoadedModuleIsNotSpliced) {
    // The splice follows the stamp, like the publish does. An implementation that handed
    // the backend every `@define` of every loaded module would pass every test above --
    // the names would all link -- and would quietly put `lib/std`'s externs into the
    // symbol table of a program that imports nothing. `absent` is unmarked, so a call to
    // it must still fail, and in the front end rather than the linker.
    TempModuleDir d;
    d.write("externs.fin",
            "@define absent(msg: string) <noret>;\n"
            "pub fun anchor() <noret> {}\n");
    Src s("import { anchor } from externs;\n"
          "fun main() <noret> { absent(\"x\"); }\n");
    const std::string err = stripAnsi(runFinc({s.str(), "-I", d.path(), "-o",
                                               uniqueTempPath("fin_stdlib_absent")},
                                              {{"FIN_LIBS", ""}}).err);
    EXPECT_NE(err.find("Undefined function or type 'absent'"), std::string::npos) << err;
}

#endif  // FIN_TESTS_HAVE_BACKEND

// ---------------------------------------------------------------------------
// The rewritten library's surface.
//
// These belong here rather than in test_soundness.cpp for the reason the file's header
// gives: what is under test is the *bundle*, and reaching it needs the real binary with
// no library flags. `compileBundled` is that.
//
// What they do NOT assert is any algorithm. `HashMap`'s probe sequence and
// `Collection`'s insert arithmetic were verified by transliterating each module into C
// and running it against a reference, because a call to an imported function is not
// lowered and no Fin program can exercise them. Asserting an algorithm here would mean
// asserting that a type-check succeeded, which says nothing about whether the algorithm
// is right -- so these hold the *shape* to account and the commit messages carry the
// measurements.
//
// Each test pairs a positive case with a negative one. A test that only checks "this
// compiles" passes against a library that dropped the method and gained a
// catch-all, and passes against a module that failed to load in a way that suppressed
// its own diagnostics -- both have happened in this repository. The negative case is
// what makes the positive one evidence.

namespace {

// A program body that imports from the bundle and runs the given statements in `main`.
std::string program(const std::string& imports, const std::string& body) {
    return imports + "fun main() <noret> {\n" + body + "}\n";
}

} // namespace

TEST(Soundness_BundledStdlib, EveryModuleChecksCleanThroughAnImport) {
    // The blanket claim the guide chapter makes, held to account one module at a time so
    // that a failure names the module rather than "the stdlib".
    //
    // Through an *import* and not standalone, deliberately: `finc lib/std/stdio.fin` and
    // `finc lib/std/error.fin` each report a circular dependency when they are the root
    // file, which is a loader limitation rather than a fault in either file, and
    // TwoModulesDoNotCheckStandalone below is where that is recorded. Every module has
    // to be clean by the route a program actually uses.
    for (const char* module : {"error", "collection", "hashmap", "types", "typing",
                               "enums", "operators", "stdptr", "stdio", "strings",
                               "math", "networking"}) {
        const std::string err =
            bundledErr(std::string("import ") + module + ";\nfun main() <noret> {}\n");
        EXPECT_EQ(errorCount(err), 0u)
            << "lib/std/" << module << ".fin does not check clean through an import.\n"
            << err;
    }
}

TEST(Soundness_BundledStdlib, TheHashMapSurfaceResolves) {
    // Every method the rewritten `HashMap` gained, named at a call. `get_index`,
    // `exists`, `len`, `__get`, `__set` and the two operators are the draft's and are
    // covered by the corpus; these are the additions, and without a test the first thing
    // to notice one had been dropped would be a user's program.
    const std::string err = bundledErr(program(
        "import { HashMap, hash_of } from hashmap::std;\n",
        "  let m <auto> = HashMap::<string, int>();\n"
        "  m[\"a\"] = 1;\n"
        "  let v <int> = m[\"a\"];\n"
        "  let g <int> = m.get_or(\"z\", -1);\n"
        "  let r <bool> = m.remove(\"a\");\n"
        "  let e <bool> = m.is_empty();\n"
        "  let c <int> = m.capacity();\n"
        "  let n <int> = m.slot_count();\n"
        "  let live <bool> = m.is_live(0);\n"
        "  let k <string> = m.key_at(0);\n"
        "  let w <int> = m.value_at(0);\n"
        "  m.clear();\n"
        "  let h <int> = hash_of::<string>(\"a\");\n"));
    EXPECT_EQ(errorCount(err), 0u) << err;
}

TEST(Soundness_BundledStdlib, AHashMapCallIsStillCheckedAgainstItsSignature) {
    // The negative half of the test above. Without it, a `HashMap` that had lost
    // `get_or` and gained a variadic catch-all would pass -- and so would one whose
    // module failed to load in a way that suppressed the diagnostics, which is the
    // failure mode `stdio.fin`'s circular dependency produces when it is the root file.
    const std::string err = bundledErr(program(
        "import { HashMap } from hashmap::std;\n",
        "  let m <auto> = HashMap::<string, int>();\n"
        "  let g <int> = m.get_or(\"z\");\n"));
    EXPECT_NE(err.find("expects 2 arguments, got 1"), std::string::npos)
        << "a method on a library type must be arity-checked like any other:\n" << err;
}

TEST(Soundness_BundledStdlib, TheCollectionSurfaceResolves) {
    const std::string err = bundledErr(program(
        "import { Collection } from collection::std;\n",
        "  let c <&Collection<int>> = new Collection::<int>{};\n"
        "  c.push(1);\n"
        "  let n <int> = c.len();\n"
        "  let e <bool> = c.is_empty();\n"
        "  let cap <int> = c.capacity();\n"
        "  c.reserve(16);\n"
        "  let f <int> = c.first();\n"
        "  let l <int> = c.last();\n"
        "  let i <int> = c.index_of(1);\n"
        "  let has <bool> = c.contains(1);\n"
        "  c.insert(0, 2);\n"
        "  let gone <int> = c.remove(0);\n"
        "  c.set(0, 3);\n"
        "  c.reverse();\n"
        "  let d <&Collection<int>> = new Collection::<int>{};\n"
        "  d.extend(c);\n"
        "  c.clear();\n"));
    EXPECT_EQ(errorCount(err), 0u) << err;
}

TEST(Soundness_BundledStdlib, ACollectionCallIsStillCheckedAgainstItsSignature) {
    const std::string err = bundledErr(program(
        "import { Collection } from collection::std;\n",
        "  let c <&Collection<int>> = new Collection::<int>{};\n"
        "  c.insert(0);\n"));
    EXPECT_NE(err.find("expects 2 arguments, got 1"), std::string::npos) << err;
}

TEST(Soundness_BundledStdlib, TheSmartPointerSurfaceResolves) {
    // `rptr`'s counting members and `wptr`, which the module gained together: the counts
    // are only meaningful because the counters are shared, and `wptr` is the handle that
    // reads a count without incrementing it.
    const std::string err = bundledErr(program(
        "import { rptr, wptr, OwnershipError } from stdptr::std;\n",
        "  let p <rptr<int>> = rptr(5);\n"
        "  let refs <int> = p.refs();\n"
        "  let borrows <int> = p.borrows();\n"
        "  let owned <bool> = p.is_owned();\n"
        "  let borrowed <bool> = p.is_borrowed();\n"
        "  let back <bool> = p.is_givenback();\n"
        "  let a <&rptr<int>> = p.alias();\n"
        "  let ro <&rptr<int>> = p.readonly_view();\n"
        "  let w <wptr<int>> = p.weak();\n"
        "  let alive <bool> = w.is_alive();\n"
        "  let seen <&int> = w.get();\n"
        "  let lent <&rptr<int>> = p.borrow();\n"
        "  lent.set(7);\n"
        "  lent.giveback();\n"
        "  let heir <&rptr<int>> = p.own();\n"
        "  let value <&int> = heir.get();\n"
        "  heir.release();\n"));
    EXPECT_EQ(errorCount(err), 0u) << err;
}

TEST(Soundness_BundledStdlib, ASmartPointerCallIsStillTypeChecked) {
    // `refs()` returns an `int`. Assigning it to a `string` has to fail, or the test
    // above is measuring that the names exist rather than that they mean anything.
    const std::string err = bundledErr(program(
        "import { rptr } from stdptr::std;\n",
        "  let p <rptr<int>> = rptr(5);\n"
        "  let n <string> = p.refs();\n"));
    EXPECT_NE(err.find("expected 'string', got 'int'"), std::string::npos) << err;
}

TEST(Soundness_BundledStdlib, TheErrorSurfaceResolves) {
    // Both arities of the constructor, and the three methods. The second parameter is the
    // point: `Error(msg: string, err_code: int = -1)` is the draft's spelling, and it was
    // written as one parameter for a while on the belief that a defaulted parameter was
    // still required at a call site. That stopped being true at `d7a91df`, and seven
    // samples import this struct, so a regression in either arity is a regression in all
    // of them.
    const std::string err = bundledErr(program(
        "import { Error } from error::std;\n",
        "  let plain <Error> = Error(\"boom\");\n"
        "  let coded <Error> = Error(\"boom\", 7);\n"
        "  let msg <string> = plain.format();\n"
        "  let full <string> = coded.describe();\n"
        "  let has <bool> = coded.has_code();\n"
        "  let id <int> = coded.error_id;\n"
        "  let text <string> = coded.message;\n"));
    EXPECT_EQ(errorCount(err), 0u) << err;
}

TEST(Soundness_BundledStdlib, TheErrorConstructorTakesOneArgumentOrTwoAndNoOther) {
    // The negative half of the arity claim, both directions. Without this the test above
    // passes against a variadic constructor and against one that takes `any...`, and the
    // "between 1 and 2" wording is what says the default is what makes the second one
    // optional rather than the parameter being nullable.
    const std::string none = bundledErr(program(
        "import { Error } from error::std;\n",
        "  let e <Error> = Error();\n"));
    EXPECT_NE(none.find("expects between 1 and 2 arguments, got 0"), std::string::npos)
        << none;

    const std::string three = bundledErr(program(
        "import { Error } from error::std;\n",
        "  let e <Error> = Error(\"boom\", 1, 2);\n"));
    EXPECT_NE(three.find("expects between 1 and 2 arguments, got 3"), std::string::npos)
        << three;

    // And the methods are typed. `describe()` returns the formatted string, so assigning
    // it to an `int` has to fail -- otherwise the surface test above measures that the
    // names exist rather than that they mean anything.
    const std::string typed = bundledErr(program(
        "import { Error } from error::std;\n",
        "  let e <Error> = Error(\"boom\");\n"
        "  let n <int> = e.describe();\n"));
    EXPECT_NE(typed.find("expected 'int', got 'string'"), std::string::npos) << typed;
}

TEST(Soundness_BundledStdlib, TheResultSurfaceResolves) {
    // `typing.fin`'s `implements` block, reached the way a caller reaches it. The receiver
    // of a method on an enum is its first parameter (Soundness_EnumMethodReceiver), so
    // `r.unwrap()` passes nothing and `r.unwrap_or(0)` passes one argument.
    const std::string err = bundledErr(program(
        "import { Result } from typing::std;\n"
        "fun consume(v: int) <noret> {}\n",
        "  let r <Result<int, string>> = Result::Ok(1);\n"
        "  let v <int> = r.unwrap();\n"
        "  let w <int> = r.unwrap_or(0);\n"
        "  r.expect();\n"
        "  let ok <bool> = r.is_ok();\n"
        "  let bad <bool> = r.is_err();\n"
        "  r.select(consume);\n"));
    EXPECT_EQ(errorCount(err), 0u)
        << "Result's implements block is what makes these resolve; if `keyidof` regressed\n"
           "the block stops type-checking and every one of these becomes an unknown method.\n"
        << err;
}

TEST(Soundness_BundledStdlib, AnEnumMethodTakesItsReceiverAsItsFirstParameter) {
    // The negative half, and it pins the receiver rule rather than just an arity. If the
    // first parameter ever stopped being the receiver, `r.unwrap()` would need an
    // argument and `r.unwrap_or(0)` would need two -- so passing one to `unwrap` must be
    // an error today and would silently become correct if the rule changed.
    const std::string err = bundledErr(program(
        "import { Result } from typing::std;\n",
        "  let r <Result<int, string>> = Result::Ok(1);\n"
        "  let v <int> = r.unwrap(r);\n"));
    EXPECT_NE(errorCount(err), 0u)
        << "the receiver of an enum method is its first parameter, so `unwrap` takes no\n"
           "argument at the call. If this went green, Soundness_EnumMethodReceiver moved\n"
           "and typing.fin's signatures need re-reading.\n"
        << err;
}

TEST(Soundness_BundledStdlib, TheIOResultAndFileSurfacesResolve) {
    const std::string err = bundledErr(program(
        "import { IOResult, File, Stream, PathLike, eprintln_str, println_str,\n"
        "         print_int, flush } from stdio::std;\n",
        "  let r <IOResult<int>> = IOResult::Ok(1);\n"
        "  let v <int> = r.unwrap();\n"
        "  let w <int> = r.unwrap_or(0);\n"
        "  let ok <bool> = r.is_ok();\n"
        "  let path <PathLike> = \"/tmp/fin_stdlib_surface\";\n"
        "  let there <bool> = File::exists(path);\n"
        "  let size <int> = File::size(path);\n"
        "  let wrote <bool> = File::write_text(path, \"x\", 1);\n"
        "  let more <bool> = File::append_text(path, \"y\", 1);\n"
        "  let bytes <[char]> = File::read_all(path);\n"
        "  let s <Stream> = File::open(path);\n"
        "  let t <int> = s.tell();\n"
        "  let left <int> = s.remaining();\n"
        "  s.rewind();\n"
        "  let read <[char]> = s.read_all();\n"
        "  let n <int> = s.write(bytes, 1);\n"
        "  let gone <bool> = File::remove(path);\n"
        "  println_str(\"a\"); eprintln_str(\"b\"); print_int(1); flush();\n"));
    EXPECT_EQ(errorCount(err), 0u) << err;
}

TEST(Soundness_BundledStdlib, TheStringsAndMathSurfacesResolve) {
    // The two modules with no draft behind them, so nothing else in the tree would
    // notice if either went missing -- which is exactly why they are held here.
    const std::string err = bundledErr(program(
        "import { Collection } from collection::std;\n"
        "import { len, equals, find, substr, trim, to_upper, split, join,\n"
        "         concat, free_str, to_chars, from_chars } from strings::std;\n"
        "import { min, max, clamp, abs, signum, gcd, lcm, ipow, isqrt,\n"
        "         floor_div, floor_mod, PI, math_sqrt } from math::std;\n",
        "  let n <int> = len(\"hi\");\n"
        "  let eq <bool> = equals(\"a\", \"a\");\n"
        "  let at <int> = find(\"hello\", \"ell\");\n"
        "  let sub <string> = substr(\"hello\", 1, 3);\n"
        "  let t <string> = trim(\"  x  \");\n"
        "  let up <string> = to_upper(\"x\");\n"
        "  let cat <string> = concat(\"a\", \"b\");\n"
        "  let parts <&Collection<string>> = split(\"a,b\", ',');\n"
        "  let joined <string> = join(parts, \",\");\n"
        "  let bytes <[char]> = to_chars(\"hi\");\n"
        "  let back <string> = from_chars(bytes, 0, 2);\n"
        "  free_str(cat);\n"
        "  let lo <int> = min::<int>(1, 2);\n"
        "  let hi <int> = max::<int>(1, 2);\n"
        "  let cl <int> = clamp::<int>(5, 0, 3);\n"
        "  let a <int> = abs::<int>(-1);\n"
        "  let s <int> = signum::<int>(-1);\n"
        "  let g <int> = gcd(12, 18);\n"
        "  let l <int> = lcm(4, 6);\n"
        "  let p <int> = ipow(2, 8);\n"
        "  let q <int> = isqrt(15);\n"
        "  let fd <int> = floor_div(-7, 2);\n"
        "  let fm <int> = floor_mod(-7, 2);\n"
        "  let pi <double> = PI;\n"
        "  let r <double> = math_sqrt(cast<double>(2.0));\n"));
    EXPECT_EQ(errorCount(err), 0u) << err;
}

TEST(Soundness_BundledStdlib, AModuleNamedStringCannotExist) {
    // Why the module is `strings`. This is a lexer fact and not a naming preference: the
    // token is TYPE_STRING before any module resolution happens, so `from string::std`
    // cannot parse whatever a file called `string.fin` contains. Asserted so that a
    // future reader does not "fix" the name.
    const std::string err = bundledErr(
        "import { len } from string::std;\n"
        "fun main() <noret> {}\n");
    EXPECT_NE(err.find("unexpected TYPE_STRING"), std::string::npos)
        << "`string` is a keyword, so a module can never carry that name. If this went\n"
           "green the lexer changed and lib/std/strings.fin's header needs re-reading.\n"
        << err;
}

TEST(KnownDefect_BundledStdlib, TwoModulesDoNotCheckStandalone) {
    // `finc lib/std/stdio.fin` and `finc lib/std/error.fin` each report a circular
    // dependency when they are the root file. Neither is a cycle in the source: stdio
    // imports error and error imports nothing, so the graph is a single edge. What
    // produces it is the driver preloading stdio before analysing the root file (ADR
    // 0021's eager-loading consequence) -- so compiling either of those two *as* the root
    // means the file is already on the loader's in-progress stack when its own import is
    // reached.
    //
    // Every other module checks clean standalone, which is what makes this two files and
    // not a general limitation. Recorded rather than fixed because the fix is in the
    // loader's cycle detection and that is not the stdlib's lane.
    //
    // Inverts into Soundness_BundledStdlib.EveryModuleChecksCleanStandalone: delete this
    // and add "stdio" and "error" to a standalone loop.
    for (const char* module : {"stdio", "error"}) {
        const std::string path = testsDir() + "/../lib/std/" + module + ".fin";
        const std::string err = stripAnsi(runFinc({path}, {{"FIN_LIBS", ""}}).err);
        EXPECT_NE(err.find("circular dependency"), std::string::npos)
            << "GOOD NEWS: lib/std/" << module << ".fin checks standalone now. Invert\n"
               "this test into EveryModuleChecksCleanStandalone and drop the caveat from\n"
               "docs/guide/12-standard-library-tour.md.\n"
            << err;
    }

    // The control, and it is what makes the above about those two files rather than about
    // compiling anything in lib/std directly: a module that imports nothing loads fine as
    // a root file.
    const std::string path = testsDir() + "/../lib/std/collection.fin";
    const std::string ok = stripAnsi(runFinc({path}, {{"FIN_LIBS", ""}}).err);
    EXPECT_EQ(errorCount(ok), 0u)
        << "collection.fin must still check standalone, or the test above is measuring\n"
           "something wider than the two files it names.\n"
        << ok;
}
