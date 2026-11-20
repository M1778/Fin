// SPDX-License-Identifier: GPL-3.0-or-later
// Stage coverage for the samples the agreement harness cannot reach.
//
// `tests/test_stage_agreement.cpp` compares the reference compiler against
// Stage2/Stage3 by building each normative-ok sample into an executable and
// running it. Samples with no `fun main` are refused by *both* compilers
// (`error: this program has no 'main'`), so that harness can only skip them --
// which is why fourteen corpus files never reached a stage at all.
//
// That skip is a harness limitation (bucket c), not a language gap: the files
// are libraries and function collections, and every one of them is legal input
// to `-c`. This suite closes the hole the only way that proves anything -- it
// appends a `main` to the sample's own text, builds it with all three
// compilers, runs it, and requires the reference's fate, stdout and stderr to
// be byte-identical on both stages (the project's stage-agreement rule).
//
// Two of the fourteen are not harness limitations. `stdlib/memory.fin` and
// `stdlib/prototypes.fin` build with the reference and are REFUSED by the stage
// backend, so they get a refusal pin instead: the test asserts the refusal is
// reported and NAMES the construct, so a regression to a silent drop fails here.
// `deeptest4.fin` is the held ruling (ADR 0036) and is pinned the same way, on
// both sides, because the two compilers refuse it for different named reasons.
//
// One genuine stage miscompile was found while doing this -- see
// `KnownDefect_StageCoverage.AConstructorReturningNewIsDroppedOnTheStage` and
// `docs/STAGE-COVERAGE.md`. It is pinned as today's wrong behavior, not skipped:
// a failure there means the defect was fixed and the pin should be inverted.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#endif

#include "Corpus.hpp"
#include "utils/Process.hpp"

namespace fs = std::filesystem;
using namespace fin::testing;

namespace {

std::string compilerFor(const std::string& stage) {
    if (stage == "Reference") return fincBinary();
    std::string compiler = stage == "Stage2" ? FINC_STAGE2_BINARY : FINC_STAGE3_BINARY;
#ifdef _WIN32
    if (!fs::exists(compiler) && fs::exists(compiler + ".exe")) compiler += ".exe";
#endif
    return compiler;
}

// The stage links every `-o` build through the fixed path
// /tmp/finc_stage_tmp.o (finc/driver.fin), so two concurrent stage builds
// cross-link. The agreement suite already serializes on this exact lock file;
// this suite takes the same lock so the two serialize against each other too.
// flock releases on process death, so a killed worker cannot wedge the run.
class StageBuildLock {
public:
    StageBuildLock() {
#ifndef _WIN32
        lockPath_ = (fs::temp_directory_path() / "finc_stage_build.lock").string();
        fd_ = ::open(lockPath_.c_str(), O_CREAT | O_RDWR, 0600);
        if (fd_ >= 0) ::flock(fd_, LOCK_EX);
#endif
    }
    ~StageBuildLock() {
#ifndef _WIN32
        if (fd_ >= 0) {
            ::flock(fd_, LOCK_UN);
            ::close(fd_);
        }
#endif
    }
    StageBuildLock(const StageBuildLock&) = delete;
    StageBuildLock& operator=(const StageBuildLock&) = delete;

private:
#ifndef _WIN32
    std::string lockPath_;
    int fd_ = -1;
#endif
};

struct Capture {
    int exit = -1;  // -1 is "did not launch", never a fate
    std::string out;
    std::string err;
};

// Runs `cmd` with stdout and stderr captured to files, from the current
// directory. FIN_LIBS is cleared so an inherited value cannot change what an
// import resolves to between compilers.
Capture runCaptured(const std::vector<std::string>& cmd, const std::string& tag) {
    const fs::path out = uniqueTempPath(tag + "_out");
    const fs::path err = uniqueTempPath(tag + "_err");
    Capture c;
    c.exit = fin::runProcess(cmd, out.string(), err.string(), {{"FIN_LIBS", ""}});
    c.out = readProcessOutput(out.string());
    c.err = readProcessOutput(err.string());
    std::error_code ec;
    fs::remove(out, ec);
    fs::remove(err, ec);
    return c;
}

void removeIfExists(const fs::path& p) {
    std::error_code ec;
    fs::remove(p, ec);
}

struct Side {
    std::string name;
    bool built = false;
    Capture build;  // compiler run: exit code and diagnostics
    Capture run;    // the program's own fate, stdout and stderr
};

// Builds `sample.fin` + `driverMain` with each compiler, runs each executable,
// and requires the reference's fate, stdout and stderr on both stages.
//
// `expected` pins what the reference prints, so a stage that agrees with a
// reference that is itself wrong still fails.
void checkDrivenSample(const std::string& sampleRel, const std::string& driverMain,
                       const std::string& expected) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::vector<std::string> stages{"Reference", "Stage2", "Stage3"};
    for (const auto& stage : stages) {
        const std::string bin = compilerFor(stage);
        if (!fs::exists(bin)) {
            GTEST_SKIP() << stage << " compiler not found at " << bin
                         << "; driven-sample coverage needs build/finc_stage2 and "
                            "build/finc_stage3 next to build/finc";
        }
    }

    std::error_code ec;
    const fs::path previous = fs::current_path(ec);
    // The stage resolves `package::std` imports through the working directory,
    // so every build and run below shares the repo root as its tree.
    fs::current_path(fs::path(testsDir()).parent_path(), ec);
    const fs::path dir = uniqueTempPath("stagecov");
    fs::create_directories(dir);

    struct Cleanup {
        fs::path dir;
        fs::path previous;
        ~Cleanup() {
            std::error_code inner;
            fs::current_path(previous, inner);
            fs::remove_all(dir, inner);
        }
    } cleanup{dir, previous};

    const fs::path src = dir / "sample.fin";
    {
        std::ofstream file(src, std::ios::binary);
        file << readWholeFile((fs::path(samplesDir()) / sampleRel).string()) << "\n" << driverMain;
        ASSERT_TRUE(file.good()) << "cannot write " << src;
    }

    std::vector<Side> sides;
    for (const auto& stage : stages) {
        Side side;
        side.name = stage;
        const fs::path exe = uniqueTempPath("stagecov_" + stage);
        {
            StageBuildLock lock;
            side.build = runCaptured({compilerFor(stage), src.string(), "-O0", "-o", exe.string()},
                                     "stagecov_build_" + stage);
        }
        side.built = side.build.exit == 0 && fs::exists(exe);
        if (side.built) side.run = runCaptured({exe.string()}, "stagecov_run_" + stage);
        removeIfExists(exe);
        sides.push_back(side);
    }

    const Side& ref = sides.front();
    ASSERT_TRUE(ref.built) << sampleRel << ": reference build failed (exit " << ref.build.exit
                           << "):\n"
                           << stripAnsi(ref.build.err);
    EXPECT_EQ(ref.run.exit, 0) << sampleRel << ": reference program exited " << ref.run.exit
                               << "\nstderr:\n"
                               << ref.run.err;
    EXPECT_EQ(ref.run.out, expected) << sampleRel << ": reference stdout is not the pinned text";
    EXPECT_EQ(ref.run.err, std::string()) << sampleRel << ": reference wrote to stderr";

    for (std::size_t i = 1; i < sides.size(); ++i) {
        const Side& st = sides[i];
        EXPECT_TRUE(st.built) << sampleRel << ": " << st.name << " build refused (exit "
                              << st.build.exit << "):\n"
                              << stripAnsi(st.build.err);
        if (!st.built) continue;
        EXPECT_EQ(st.run.exit, ref.run.exit)
            << sampleRel << ": " << st.name << " run fate diverges (reference " << ref.run.exit
            << ", " << st.name << " " << st.run.exit << ")\nstderr:\n"
            << st.run.err;
        EXPECT_EQ(st.run.out, ref.run.out)
            << sampleRel << ": " << st.name << " stdout diverges from the reference\nreference:\n"
            << ref.run.out << st.name << ":\n"
            << st.run.out;
        EXPECT_EQ(st.run.err, ref.run.err)
            << sampleRel << ": " << st.name << " stderr diverges from the reference\nreference:\n"
            << ref.run.err << st.name << ":\n"
            << st.run.err;
    }
}

// Compile-only, both sides, from the repo root. Returns each compiler's capture.
struct CompilePair {
    Capture reference;
    Capture stage;
    Capture stage3;
};

CompilePair compileOnly(const std::string& sampleRel) {
    const fs::path previous = fs::current_path();
    std::error_code ec;
    fs::current_path(fs::path(testsDir()).parent_path(), ec);
    CompilePair pair;
    const fs::path object = uniqueTempPath("stagecov_obj");
    pair.reference = runCaptured(
        {compilerFor("Reference"), (fs::path(samplesDir()) / sampleRel).string(), "-c", "-o",
         object.string()},
        "stagecov_refc");
    pair.stage = runCaptured({compilerFor("Stage2"), (fs::path(samplesDir()) / sampleRel).string(),
                              "-c", "-o", object.string()},
                             "stagecov_st2c");
    pair.stage3 = runCaptured({compilerFor("Stage3"), (fs::path(samplesDir()) / sampleRel).string(),
                               "-c", "-o", object.string()},
                              "stagecov_st3c");
    removeIfExists(object);
    fs::current_path(previous, ec);
    return pair;
}

bool stagesAvailable() {
    return fs::exists(compilerFor("Stage2")) && fs::exists(compilerFor("Stage3"));
}

}  // namespace

// ---------------------------------------------------------------------------
// Driven samples: a `main` appended to the sample's own text, built and run by
// all three compilers, stdout and stderr byte-identical. These were skips in
// the agreement suite purely because the files declare no entry point.
// ---------------------------------------------------------------------------

TEST(StageCoverageDriven, Deeptest3RunsEveryEntryPoint) {
    // The sample's seven `test_*_main` entry points, in declaration order, cover
    // swappable pointers, pointer member access, `&array` indexing, nullable
    // pointers, a linked list through `delete`, and double dereference.
    checkDrivenSample("deeptest3.fin",
                      "fun main() <noret> {\n"
                      "    test_swap_main();\n"
                      "    test_Player_main();\n"
                      "    test_array_main();\n"
                      "    test_null_main();\n"
                      "    test_linkedlist_main();\n"
                      "    test_ptr2arr_main();\n"
                      "    test_ptr2ptr_main();\n"
                      "}\n",
                      "Before: 10, 20\n"
                      "After:  20, 10\n"
                      "Player HP: 100, Score: 50\n"
                      "Value at index 1: 20\n"
                      "New value in array: 99\n"
                      "Value is: 500\n"
                      "No value provided.\n"
                      "Head: 1\n"
                      "Next: 2\n"
                      "First element: 10\n"
                      "Original: 10\n"
                      "Modified via double pointer: 500\n");
}

TEST(StageCoverageDriven, StructMethodsRunOnBothStageCompilers) {
    // struct_methods.fin declares `Point<T>` with a member method, a generic
    // method and two statics. `Point<int>{...}` is refused by BOTH compilers (a
    // reference-side parser gap, not a stage one -- see docs/STAGE-COVERAGE.md),
    // so the driver instantiates through the default generic, which is the
    // spelling tests/test_codegen.cpp uses for this same declaration.
    checkDrivenSample("struct_methods.fin",
                      "fun main() <noret> {\n"
                      "    let p <Point<int>> = Point{ x: 4 };\n"
                      "    p.print_point();\n"
                      "    printf(\"\\n\");\n"
                      "    p.set_x(9);\n"
                      "    p.print_point();\n"
                      "    printf(\"\\n\");\n"
                      "    let q <&Point<int>> = Point::default_point();\n"
                      "    q.print_point();\n"
                      "    printf(\"\\n\");\n"
                      "}\n",
                      "x: 4, y:0\nx: 9, y:0\nx: 0, y:0\n");
}

TEST(StageCoverageDriven, MacrosExpandsItsQuoteBody) {
    // macros.fin's `magic_add(a, b)` is ADR 0023's settled form: one `quote`
    // returning one expression. importing.fin:23 calls it across a module.
    checkDrivenSample("macros.fin",
                      "fun main() <noret> { printf(\"%d\\n\", magic_add!(10, 20)); }\n", "30\n");
}

TEST(StageCoverageDriven, Macros2ParsesToAnEmptyUnit) {
    // macros2.fin is entirely inside a `/* */` block: the file's live content is
    // nothing. Driving it still proves the block comment lexes the same on all
    // three compilers and leaves no trailing declaration behind.
    checkDrivenSample("macros2.fin",
                      "fun main() <noret> { printf(\"macros2 has no live declarations\\n\"); }\n",
                      "macros2 has no live declarations\n");
}

TEST(StageCoverageDriven, MacroDefinitionsParsesToAnEmptyUnit) {
    // Same shape as macros2: the Rust-like arms sketch sits in a block comment
    // because ADR 0023 refused that spelling, so the file declares nothing.
    checkDrivenSample(
        "macro_definitions.fin",
        "fun main() <noret> { printf(\"macro_definitions has no live declarations\\n\"); }\n",
        "macro_definitions has no live declarations\n");
}

TEST(StageCoverageDriven, ExternAsResolvesAliasesAndEnumMembers) {
    // extern_as.fin: a namespace function reached through its own name after
    // `extern * from a_namespace`, an enum member reached bare after
    // `extern * from MyEnum`, and a type alias from `extern int as Integer`.
    // Its `extern myglobv as myglobv_diffname` line is NOT driven here: the
    // stage does not resolve the alias and reports a bare `use of undeclared
    // identifier`, pinned separately below (defect D4 in docs/STAGE-COVERAGE.md).
    checkDrivenSample("extern_as.fin",
                      "fun main() <noret> {\n"
                      "    myfunc();\n"
                      "    a();\n"
                      "    b();\n"
                      "    let e <MyEnum> = MyEnum::B;\n"
                      "    printf(\"enum=%d\\n\", cast<int>(e));\n"
                      "    let i <Integer> = 7;\n"
                      "    printf(\"int=%d\\n\", i);\n"
                      "    printf(\"global=%d\\n\", myglobv);\n"
                      "}\n",
                      "enum=1\nint=7\nglobal=10\n");
}

TEST(StageCoverageDriven, LiteralInterfaceAnswersAnImplementsQuery) {
    // literal_interface.fin's first half: `compatible()` takes `$interface` and
    // `$struct` and folds `@implements`, fed `interface {...}` and `struct {...}`
    // literals. The sample's second half (`pick_interface` returning
    // `$interface`) is refused by both compilers with `Type '$interface' is not
    // a struct`, so it is a reference-side gap and is not claimed here.
    checkDrivenSample("literal_interface.fin",
                      "fun main() <noret> {\n"
                      "    let s <$struct> = struct {\n"
                      "        pub picked_first <bool>,\n"
                      "        pub fun say_hello() <noret> { printf(\"hi\\n\"); }\n"
                      "      };\n"
                      "    let i <$interface> = interface {\n"
                      "        pub picked_first <bool>,\n"
                      "        pub fun say_hello() <noret>;\n"
                      "      };\n"
                      "    printf(\"compat=%d\\n\", cast<int>(compatible(i, s)));\n"
                      "}\n",
                      "compat=1\n");
}

TEST(StageCoverageDriven, StdlibErrorCarriesItsFields) {
    // error::std is a `#[class]` whose constructor body is `return new Error{}`.
    // That shape is the first stage miscompile pinned below, so this test builds
    // the value with the struct literal instead -- the field layout and the
    // defaulted member are what it proves, and all three compilers agree on them.
    // `format()` is NOT driven here: its body is `format!("Error: {}", self.message)`
    // and a string ARGUMENT to `format!` is the second stage miscompile, pinned
    // below. Driving it here would only re-report that defect as a skip.
    checkDrivenSample("stdlib/error.fin",
                      "fun main() <noret> {\n"
                      "    let e <Error> = Error{message: \"boom\", error_id: 42};\n"
                      "    printf(\"%d %s\\n\", e.error_id, e.message);\n"
                      "}\n",
                      "42 boom\n");
}

TEST(StageCoverageDriven, StdlibHashMapParsesAsAModule) {
    // hashmap::std cannot be DRIVEN at all today, by either compiler: its
    // `__set` stores through `self.values[self.get_index(key)]` and the
    // reference refuses that with `an assignment to this target is not lowered
    // yet` (pinned by StageCoverageRefusal.HashMapSetIsRefusedByTheReferenceToo).
    // What can be proved is everything short of instantiation: the module parses,
    // its generic struct and both operators resolve, and a driver that never
    // instantiates `HashMap` builds and runs on all three compilers.
    checkDrivenSample("stdlib/hashmap.fin",
                      "fun main() <noret> { printf(\"hashmap module parses\\n\"); }\n",
                      "hashmap module parses\n");
}

TEST(StageCoverageDriven, StdlibOutcomeBuildsAndReadsBothArms) {
    // outcome::std constructs through free functions returning struct literals
    // (no `new`), so both stages lower the value the same way the reference does.
    checkDrivenSample("stdlib/outcome.fin",
                      "fun main() <noret> {\n"
                      "    let a <auto> = Ok::<int>(11);\n"
                      "    let b <Outcome<int>> = Err::<int>(\"nope\");\n"
                      "    printf(\"%d %d %d %d\\n\", cast<int>(a.is_ok()), a.unwrap_or(0),\n"
                      "           cast<int>(b.is_err()), b.unwrap_or(5));\n"
                      "}\n",
                      "1 11 1 5\n");
}

TEST(StageCoverageDriven, StdlibTypingConstructsItsResultBothWays) {
    // typing::std declares `Result<T, U>` (a payload enum) and the `IResult`
    // interface. The sample sits in `namespace std`, so its names are imported
    // unqualified -- `Result`, not `std::Result`, which the parser refuses.
    // Building both arms is all the module offers a caller today; casting a
    // payload enum to int is refused by the reference (pinned below).
    checkDrivenSample("stdlib/typing.fin",
                      "fun main() <noret> {\n"
                      "    let r <Result<int, string>> = Result::<int, string>::Ok(7);\n"
                      "    let e <Result<int, string>> = Result::<int, string>::Err(\"bad\");\n"
                      "    printf(\"built both arms\\n\");\n"
                      "}\n",
                      "built both arms\n");
}

// ---------------------------------------------------------------------------
// Refusal pins. The stage cannot lower these constructs, and the backend's
// invariant is that a refusal NAMES the construct. Each test asserts the
// refusal is still reported, at the same place, with the same name -- so a
// regression to a silent drop or a guessed lowering fails here.
// ---------------------------------------------------------------------------

TEST(StageCoverageRefusal, MemoryCompileTimeCallIsRefusedByName) {
    // stdlib/memory.fin line 15: `@GET_MEMORY_LIMIT()`, a `@special` compile-time
    // call inside a runtime function body. The reference folds it; the stage
    // has no compile-time interpreter, so it must refuse and name the call.
    if (!stagesAvailable()) GTEST_SKIP() << "stage compilers not found; refusal pins need both";
    const CompilePair p = compileOnly("stdlib/memory.fin");
    EXPECT_EQ(p.reference.exit, 0) << "the reference builds it; this is a stage-only refusal:\n"
                                   << stripAnsi(p.reference.err);
    for (const auto& stage : {std::make_pair("Stage2", &p.stage), std::make_pair("Stage3", &p.stage3)}) {
        EXPECT_NE(stage.second->exit, 0)
            << stage.first << " no longer refuses stdlib/memory.fin; if the compile-time call is\n"
               "now lowered, delete this pin and drive the module with a main instead";
        EXPECT_NE(stage.second->err.find("codegen: the compile-time call '@GET_MEMORY_LIMIT' is not lowered yet"),
                  std::string::npos)
            << stage.first << " refused stdlib/memory.fin without naming the construct.\n"
               "The backend invariant is that a refusal names what it cannot lower:\n"
            << stripAnsi(stage.second->err);
    }
}

TEST(StageCoverageRefusal, PrototypesKeyEscapeIsRefusedByName) {
    // stdlib/prototypes.fin returns `prtp.0` (a prototype's key array) out of a
    // function. A key buffer cannot escape through a call (ADR 0028), which is a
    // declared refusal and not a gap: the reference lowers the same source
    // because it duplicates the buffer instead.
    if (!stagesAvailable()) GTEST_SKIP() << "stage compilers not found; refusal pins need both";
    const CompilePair p = compileOnly("stdlib/prototypes.fin");
    EXPECT_EQ(p.reference.exit, 0) << "the reference builds it; this is a stage-only refusal:\n"
                                   << stripAnsi(p.reference.err);
    for (const auto& stage : {std::make_pair("Stage2", &p.stage), std::make_pair("Stage3", &p.stage3)}) {
        EXPECT_NE(stage.second->exit, 0)
            << stage.first << " no longer refuses stdlib/prototypes.fin; if the escape rule moved,\n"
               "delete this pin and document the new rule";
        EXPECT_NE(stage.second->err.find(
                      "prototype key buffers cannot escape through calls (ADR 0028)"),
                  std::string::npos)
            << stage.first << " refused stdlib/prototypes.fin without naming the rule:\n"
            << stripAnsi(stage.second->err);
    }
}

TEST(StageCoverageRefusal, Deeptest4StructEqualityIsRefusedByName) {
    // ADR 0036: struct `==` is declared-only, never synthesized. The reference
    // refuses at the `==` and the stage refuses earlier, at the generic
    // `HashMap` constructor it cannot key. Both refusals are NAMED, which is
    // what is pinned here -- neither compiler may quietly compile it.
    if (!stagesAvailable()) GTEST_SKIP() << "stage compilers not found; refusal pins need both";
    const CompilePair p = compileOnly("deeptest4.fin");
    EXPECT_NE(p.reference.exit, 0) << "the held ruling says struct '==' is not synthesized:\n"
                                   << stripAnsi(p.reference.err);
    EXPECT_NE(p.reference.err.find(
                  "an undeclared operator '==' on struct 'Data' is not lowered yet"),
              std::string::npos)
        << "the reference must name the undeclared operator:\n"
        << stripAnsi(p.reference.err);
    for (const auto& stage : {std::make_pair("Stage2", &p.stage), std::make_pair("Stage3", &p.stage3)}) {
        EXPECT_NE(stage.second->exit, 0) << stage.first << " compiled deeptest4.fin cleanly:\n"
                                         << stripAnsi(stage.second->err);
        EXPECT_NE(stage.second->err.find("finc codegen refusal:"), std::string::npos)
            << stage.first << " refused deeptest4.fin without the named-refusal prefix:\n"
            << stripAnsi(stage.second->err);
    }
}

TEST(StageCoverageRefusal, ExternConstAliasIsRefusedByName) {
    // extern_as.fin:9 `extern myglobv as myglobv_diffname;` -- the reference
    // resolves the alias, the stage reports a bare `use of undeclared identifier`
    // with no name in it. Pinned as today's behavior: the fix gives the diagnostic
    // the identifier, and this test then asserts the alias RESOLVES instead.
    if (!stagesAvailable()) GTEST_SKIP() << "stage compilers not found; refusal pins need both";
    const fs::path dir = uniqueTempPath("stagecov_alias");
    fs::create_directories(dir);
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    } cleanup{dir};
    const fs::path src = dir / "alias.fin";
    {
        std::ofstream file(src, std::ios::binary);
        file << "@define printf(fmt: string, ...) <noret>;\n"
                "const myglobv <int> = 10;\n"
                "extern myglobv as myglobv_diffname;\n"
                "fun main() <noret> { printf(\"%d %d\\n\", myglobv, myglobv_diffname); }\n";
        ASSERT_TRUE(file.good());
    }
    const Capture ref = runCaptured({compilerFor("Reference"), src.string(), "-c"}, "stagecov_alias_ref");
    EXPECT_EQ(ref.exit, 0) << "the reference resolves `extern as` over a const:\n"
                           << stripAnsi(ref.err);
    for (const auto& stage : {std::make_pair("Stage2", "Stage2"), std::make_pair("Stage3", "Stage3")}) {
        const Capture c = runCaptured({compilerFor(stage.second), src.string(), "-c"}, "stagecov_alias_st");
        EXPECT_NE(c.exit, 0)
            << stage.first << " now resolves `extern <const> as <name>`; that is the fix landing.\n"
               "Delete this pin and drive extern_as.fin's alias line in\n"
               "StageCoverageDriven.ExternAsResolvesAliasesAndEnumMembers";
        EXPECT_NE(c.err.find("error:"), std::string::npos)
            << stage.first << " refused the alias with no diagnostic:\n"
            << stripAnsi(c.err);
    }
}

// ---------------------------------------------------------------------------
// The one genuine stage miscompile found by this sweep.
//
// A constructor whose body is `return new S{...}` -- the dominant corpus shape,
// and the shape `lib/std/error.fin` uses -- has its result thrown away by the
// stage. The call site allocates a zeroed receiver box, passes its address as
// the implicit `self`, and returns the box; the constructor body evaluates the
// returned expression and then returns it out of a function whose result the
// call site ignores. So every field the returned literal set reads back zero.
//
// The reference implements the other half of that one calling convention:
// CodeGen_LLVM.cpp's `emitConstructedValue` stores the returned object through
// the receiver. The stage has the receiver parameter and the box but not the
// store.
//
// Pinned as today's WRONG behavior, in the project's KnownDefect convention
// (tests/test_soundness.cpp:32-40): **a failure here is good news** -- it means
// the store was added. Invert the expectations and move the test to
// StageCoverageDriven.
// ---------------------------------------------------------------------------

TEST(KnownDefect_StageCoverage, AConstructorReturningNewIsDroppedOnTheStage) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    if (!fs::exists(compilerFor("Stage2"))) {
        GTEST_SKIP() << "stage compiler not found at " << compilerFor("Stage2");
    }
    const fs::path dir = uniqueTempPath("stagecov_ctor");
    fs::create_directories(dir);
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    } cleanup{dir};
    const fs::path src = dir / "ctor.fin";
    {
        std::ofstream file(src, std::ios::binary);
        // The minimal repro: two fields, one constructor, `return new E{...}`.
        file << "@define printf(fmt: string, ...) <noret>;\n"
                "struct E {\n"
                "    pub message <string>,\n"
                "      pub code <int>,\n"
                "      E(msg: string, c: int) { return new E{message: msg, code: c}; }\n"
                "}\n"
                "fun main() <noret> {\n"
                "    let e <E> = E(\"boom\", 42);\n"
                "    printf(\"[%s] [%d]\\n\", e.message, e.code);\n"
                "}\n";
        ASSERT_TRUE(file.good());
    }
    const Capture ref =
        runCaptured({compilerFor("Reference"), src.string(), "-O0", "-o", (dir / "ref").string()},
                    "stagecov_ctor_ref");
    ASSERT_EQ(ref.exit, 0) << "the reference rejects the minimal repro:\n" << stripAnsi(ref.err);
    const Capture refRun = runCaptured({(dir / "ref").string()}, "stagecov_ctor_refrun");
    EXPECT_EQ(refRun.out, "[boom] [42]\n") << "the reference no longer builds the constructor";

    const std::string bin = compilerFor("Stage2");
    const fs::path exe = dir / "stage";
    Capture build;
    {
        StageBuildLock lock;
        build = runCaptured({bin, src.string(), "-O0", "-o", exe.string()}, "stagecov_ctor_st");
    }
    ASSERT_EQ(build.exit, 0) << "the stage now REFUSES the repro instead of miscompiling it; that\n"
                                    "is a behavior change -- re-read the pin before editing:\n"
                             << stripAnsi(build.err);
    const Capture run = runCaptured({exe.string()}, "stagecov_ctor_strun");
    // The correct result is `[boom] [42]`. Today the stage prints the zeroed box.
    EXPECT_EQ(run.out, "[(null)] [0]\n")
        << "The stage now agrees with the reference. That means the constructor-return\n"
           "miscompile was fixed (docs/STAGE-COVERAGE.md, 'genuine defects'): move this\n"
           "test into StageCoverageDriven and delete the defect entry.\nGot:\n"
        << run.out;
}

// ---------------------------------------------------------------------------
// Second genuine stage miscompile: a `format!` argument that is a runtime
// string value.
//
// `format!` is the one macro the compiler implements (ADR 0023). The C++ backend
// lowers it directly (CodeGen_LLVM.cpp:14923 onwards) and picks `%s` for a string
// value. The stage instead expands `format!` in the driver into a generated
// helper that calls `snprintf` through the declared conversions of
// `finc/driver.fin:mx_fmt_conv`, which reads the argument's SOURCE TEXT and
// defaults to `%d` for anything that is not a literal, `&x`, `null`, a bool or a
// numeric literal. A named `string` local is none of those, so it is printed as
// its pointer -- visible as a decimal number like `4202500`.
//
// The reference compiles the same source correctly, so this is a silent
// miscompile, not a refusal; the backend invariant ("never silently drop
// runtime code") is what makes it a defect rather than a limitation.
// ---------------------------------------------------------------------------

TEST(KnownDefect_StageCoverage, FormatBangPrintsAStringValueAsItsPointer) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    if (!fs::exists(compilerFor("Stage2"))) {
        GTEST_SKIP() << "stage compiler not found at " << compilerFor("Stage2");
    }
    const fs::path dir = uniqueTempPath("stagecov_fmt");
    fs::create_directories(dir);
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    } cleanup{dir};
    const fs::path src = dir / "fmt.fin";
    {
        std::ofstream file(src, std::ios::binary);
        file << "@define printf(fmt: string, ...) <noret>;\n"
                "fun main() <noret> {\n"
                "    let s <string> = \"boom\";\n"
                "    printf(\"[%s]\\n\", format!(\"Error: {}\", s));\n"
                "}\n";
        ASSERT_TRUE(file.good());
    }
    const Capture ref =
        runCaptured({compilerFor("Reference"), src.string(), "-O0", "-o", (dir / "ref").string()},
                    "stagecov_fmt_ref");
    ASSERT_EQ(ref.exit, 0) << "the reference rejects the minimal repro:\n" << stripAnsi(ref.err);
    const Capture refRun = runCaptured({(dir / "ref").string()}, "stagecov_fmt_refrun");
    EXPECT_EQ(refRun.out, "[Error: boom]\n") << "the reference's format! changed";

    const fs::path exe = dir / "stage";
    Capture stageBuild;
    {
        StageBuildLock lock;
        stageBuild =
            runCaptured({compilerFor("Stage2"), src.string(), "-O0", "-o", exe.string()},
                        "stagecov_fmt_st");
    }
    ASSERT_EQ(stageBuild.exit, 0) << "the stage now refuses the repro:\n"
                                  << stripAnsi(stageBuild.err);
    const Capture run = runCaptured({exe.string()}, "stagecov_fmt_strun");
    // Correct: `[Error: boom]`. Today: the pointer as a decimal, e.g. `[Error: 4202500]`.
    EXPECT_EQ(run.out.rfind("[Error: 4", 0), 0u)
        << "The stage's format! no longer prints the string argument as a pointer-shaped\n"
           "number. That means mx_fmt_conv (finc/driver.fin) learned the argument's type --\n"
           "move this test into StageCoverageDriven and delete the defect entry.\nGot:\n"
        << run.out;
    EXPECT_NE(run.out, refRun.out) << "the stage now agrees with the reference; invert this pin";
}

// ---------------------------------------------------------------------------
// Third genuine stage miscompile, and the narrowest of the three: indexing a
// FIXED-ARRAY field from inside a method.
//
// `self.vals[i]` where `vals <[int, 4]>` is accepted by the reference and
// refused by both stages as `Type mismatch: expected 'int', got 'int,4'` -- the
// stage's index lowering reads the array annotation `[int, 4]` and hands the
// whole thing to the element-type position instead of splitting off the extent.
// The same index on a LOCAL fixed array (`a[i]`) and on a field read from
// outside a method (`s.vals[i]`) both lower correctly, which is what makes this
// a stage-only defect rather than a language gap.
//
// The sample that exposed it is stdlib/hashmap.fin: `__set` stores through
// `self.values[...]`, so the whole mutating half of the hash map is unreachable
// on a stage until this is fixed.
// ---------------------------------------------------------------------------

TEST(KnownDefect_StageCoverage, IndexingAFixedArrayFieldInsideAMethodIsRefused) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    if (!fs::exists(compilerFor("Stage2"))) {
        GTEST_SKIP() << "stage compiler not found at " << compilerFor("Stage2");
    }
    const fs::path dir = uniqueTempPath("stagecov_arrfield");
    fs::create_directories(dir);
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    } cleanup{dir};
    const fs::path src = dir / "arrfield.fin";
    {
        std::ofstream file(src, std::ios::binary);
        file << "@define printf(fmt: string, ...) <noret>;\n"
                "struct S {\n"
                "    pub vals <[int, 4]>,\n"
                "      S() { self.vals = [1, 2, 3, 4]; }\n"
                "    pub fun get(self: &Self, i: int) <int> { return self.vals[i]; }\n"
                "}\n"
                "fun main() <noret> {\n"
                "    let s <S> = S();\n"
                "    printf(\"%d\\n\", s.get(2));\n"
                "}\n";
        ASSERT_TRUE(file.good());
    }
    const Capture ref =
        runCaptured({compilerFor("Reference"), src.string(), "-O0", "-o", (dir / "ref").string()},
                    "stagecov_arrfield_ref");
    ASSERT_EQ(ref.exit, 0) << "the reference rejects the minimal repro:\n" << stripAnsi(ref.err);
    const Capture refRun = runCaptured({(dir / "ref").string()}, "stagecov_arrfield_refrun");
    EXPECT_EQ(refRun.out, "3\n") << "the reference's answer changed";

    for (const auto& stage : {std::make_pair("Stage2", "Stage2"), std::make_pair("Stage3", "Stage3")}) {
        const Capture c = runCaptured({compilerFor(stage.second), src.string(), "-c"},
                                      "stagecov_arrfield_st");
        EXPECT_NE(c.exit, 0)
            << stage.first << " now accepts a fixed-array field indexed inside a method. That is\n"
               "the fix landing: delete this pin and drive stdlib/hashmap.fin's __set";
        EXPECT_NE(c.err.find("Type mismatch: expected 'int', got 'int,4'"), std::string::npos)
            << stage.first << " refused the program without the array-extent diagnostic:\n"
            << stripAnsi(c.err);
    }
}

// ---------------------------------------------------------------------------
// A reference-side gap found while driving the same sample, recorded so the
// stage's coverage of hashmap.fin is not overstated: `__set`'s index-assign
// through a method call is refused by the C++ backend as well.
//
// This is NOT a stage defect. It is here because docs/STAGE-COVERAGE.md claims
// what the stage covers, and a claim that skipped the reference's own refusal
// would hide that hashmap.fin's mutating half is unbuildable by either compiler.
// ---------------------------------------------------------------------------

TEST(StageCoverageRefusal, HashMapSetIsRefusedByTheReferenceToo) {
    if (!stagesAvailable()) GTEST_SKIP() << "stage compilers not found; this pin needs both";
    // Uses the corpus sample itself, so the refusal is the one a caller of
    // hashmap::std actually meets rather than a hand-rolled imitation of it.
    const fs::path dir = uniqueTempPath("stagecov_hmset");
    fs::create_directories(dir);
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    } cleanup{dir};
    const fs::path src = dir / "hmset.fin";
    {
        std::ofstream file(src, std::ios::binary);
        file << readWholeFile((fs::path(samplesDir()) / "stdlib/hashmap.fin").string()) << "\n"
             << "fun main() <noret> {\n"
                "    let m <HashMap<int, int>> = HashMap::<int, int>();\n"
                "    m.__set(1, 10);\n"
                "    printf(\"stored\\n\");\n"
                "}\n";
        ASSERT_TRUE(file.good());
    }
    std::error_code ec;
    const fs::path previous = fs::current_path(ec);
    fs::current_path(fs::path(testsDir()).parent_path(), ec);
    const Capture ref = runCaptured({compilerFor("Reference"), src.string(), "-c"}, "stagecov_hmset_ref");
    fs::current_path(previous, ec);
    EXPECT_NE(ref.exit, 0) << "the reference now lowers this index-assign. If so, drive\n"
                              "stdlib/hashmap.fin's __set in StageCoverageDriven and delete this pin";
    EXPECT_NE(ref.err.find("an assignment to this target is not lowered yet"), std::string::npos)
        << "the reference must name the refused assignment:\n"
        << stripAnsi(ref.err);
}

TEST(KnownDefect_StageCoverage, CastingAPayloadEnumToIntIsRefusedOnlyByTheReference) {
    if (!stagesAvailable()) GTEST_SKIP() << "stage compilers not found; this pin needs both";
    const fs::path dir = uniqueTempPath("stagecov_enumcast");
    fs::create_directories(dir);
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    } cleanup{dir};
    const fs::path src = dir / "enumcast.fin";
    {
        std::ofstream file(src, std::ios::binary);
        file << "@define printf(fmt: string, ...) <noret>;\n"
                "enum Payload { Empty, Holds(int) }\n"
                "fun main() <noret> {\n"
                "    let p <Payload> = Payload::Holds(7);\n"
                "    printf(\"%d\\n\", cast<int>(p));\n"
                "}\n";
        ASSERT_TRUE(file.good());
    }
    const Capture ref =
        runCaptured({compilerFor("Reference"), src.string(), "-c"}, "stagecov_enumcast_ref");
    EXPECT_NE(ref.exit, 0) << "the reference now casts a payload enum to int:\n"
                           << "re-read this pin -- it may have become a supported conversion";
    EXPECT_NE(ref.err.find("this conversion (from 'a struct' to 'an integer') is not lowered yet"),
              std::string::npos)
        << "the reference must name the refused conversion:\n"
        << stripAnsi(ref.err);
    for (const auto& stage : {std::make_pair("Stage2", "Stage2"), std::make_pair("Stage3", "Stage3")}) {
        const Capture c =
            runCaptured({compilerFor(stage.second), src.string(), "-c"}, "stagecov_enumcast_st");
        EXPECT_EQ(c.exit, 0)
            << stage.first << " now refuses `cast<int>` on a payload enum, like the reference.\n"
               "That closes the divergence: delete this pin.";
    }
}
