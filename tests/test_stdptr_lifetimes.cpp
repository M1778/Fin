#include "Corpus.hpp"
#include "utils/Process.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::string runPointerProgram(const std::string& source, bool checkLeaks = false) {
    namespace fs = std::filesystem;
    using namespace fin::testing;
    const fs::path dir = uniqueTempPath("fin_pointer_lifetime");
    fs::create_directories(dir);
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{dir};
    const fs::path input = dir / "main.fin";
    const fs::path object = dir / "main.o";
    const fs::path executable = dir / "program.exe";
    const fs::path output = dir / "output.txt";
    {
        std::ofstream file(input, std::ios::binary);
        file << source;
    }
    auto args = std::vector<std::string>{input.string(), "-o", executable.string()};
    if (checkLeaks) args = {input.string(), "-c", "-o", object.string()};
    const auto compiled = runFinc(args, {{"FIN_LIBS", ""}});
    if (compiled.exitCode != 0) return "compile failed: " + stripAnsi(compiled.err);
    if (checkLeaks) {
        auto command = fin::linkCommand({object.string()}, executable.string());
        command.push_back("-fsanitize=leak");
        const int linked = fin::runProcess(command, output.string(), output.string());
        if (linked != 0) return "link failed: " + readProcessOutput(output.string());
    }
    const int status = fin::runProcess({executable.string()}, output.string(), output.string(),
        checkLeaks ? std::vector<std::pair<std::string, std::string>>{
                         {"LSAN_OPTIONS", "detect_leaks=1:exitcode=23"}}
                   : std::vector<std::pair<std::string, std::string>>{});
    const auto text = readProcessOutput(output.string());
    return status == 0 ? text : "run failed: " + std::to_string(status) + "\n" + text;
}

const char* kPointerLifecycle = R"FIN(
import { rptr, wptr } from stdptr::std;
fun main() <noret> {
    for (let round <int> = 0; round < 32; round++) {
        let p <rptr<int>> = rptr(round);
        let snapshot <&rptr<int>> = p.restrict;
        blame p.refs() == 2;
        blame !snapshot.is_owned();
        let original <&int> = p.value;
        let a <&rptr<int>> = p.alias();
        blame p.refs() == 3;
        a.release();
        delete a;
        let borrowed <&rptr<int>> = p.borrow();
        borrowed.set(73);
        borrowed.giveback();
        borrowed.release();
        delete borrowed;
        let heir <&rptr<int>> = p.own();
        heir.set(91);
        heir.release();
        delete heir;
        blame p.refs() == 2;
        let first <wptr<int>> = p.weak();
        let last <wptr<int>> = p.weak();
        blame first.is_alive();
        blame *first.get() == 91;
        p.release();
        blame p.value == original;
        blame *snapshot.get() == 91;
        snapshot.release();
        delete snapshot;
        blame !first.is_alive();
        first.release();
        blame !first.is_alive();
        blame !last.is_alive();
        last.release();
        blame !last.is_alive();
    }
    printf("pointer cleanup ok\n");
}
)FIN";

std::string pointerProgram(const std::string& body) {
    return "import { rptr, wptr } from stdptr::std;\nfun main() <noret> {\n"
           "    let p <rptr<int>> = rptr(5);\n"
           "    let snapshot <&rptr<int>> = p.restrict;\n" + body + "}\n";
}

}

TEST(Soundness_StdptrLifetimes, WeakCountersOutliveTheFinalStrongRelease) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable pointer checks need the LLVM backend";
#endif
    EXPECT_EQ(runPointerProgram(kPointerLifecycle), "pointer cleanup ok\n");
}

TEST(Soundness_StdptrLifetimes, WeakReleaseBeforeStrongReleaseDoesNotKeepTheValueAlive) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable pointer checks need the LLVM backend";
#endif
    const auto source = pointerProgram(R"FIN(
    let weak <wptr<int>> = p.weak();
    weak.release();
    blame !weak.is_alive();
    blame p.refs() == 2;
    blame *p.get() == 5;
    snapshot.release();
    delete snapshot;
    p.release();
    blame !weak.is_alive();
    printf("weak cleanup first ok\n");
)FIN");
    EXPECT_EQ(runPointerProgram(source), "weak cleanup first ok\n");
}

TEST(Soundness_StdptrLifetimes, StrongReleaseCannotDropTheSameReferenceTwice) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable pointer checks need the LLVM backend";
#endif
    for (const char* prepare : {"", "snapshot.release(); delete snapshot;\n"}) {
        const auto source = pointerProgram(std::string(prepare) + "p.release(); p.release();\n");
        const auto result = runPointerProgram(source);
        EXPECT_EQ(result.find("run failed:"), 0u) << result;
        EXPECT_NE(result.find("this pointer was already released"), std::string::npos) << result;
    }
}

TEST(Soundness_StdptrLifetimes, WeakReadAfterTheFinalStrongReleaseRefuses) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable pointer checks need the LLVM backend";
#endif
    const auto source = pointerProgram(R"FIN(
    let weak <wptr<int>> = p.weak();
    snapshot.release();
    delete snapshot;
    p.release();
    weak.get();
)FIN");
    const auto result = runPointerProgram(source);
    EXPECT_EQ(result.find("run failed:"), 0u) << result;
    EXPECT_NE(result.find("the value this weak pointer refers to has been released"),
              std::string::npos) << result;
}

TEST(Soundness_StdptrLifetimes, ReleasedWeakHandlesRejectAnotherReleaseAndRead) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable pointer checks need the LLVM backend";
#endif
    for (const char* operation : {"weak.release();", "weak.get();"}) {
        const auto source = pointerProgram(std::string(R"FIN(
    let weak <wptr<int>> = p.weak();
    snapshot.release();
    delete snapshot;
    p.release();
    weak.release();
)FIN") + operation + "\n");
        const auto result = runPointerProgram(source);
        EXPECT_EQ(result.find("run failed:"), 0u) << result;
        EXPECT_NE(result.find("this weak pointer was already released"), std::string::npos) << result;
    }
}

TEST(Soundness_StdptrLifetimes, ReleasedStrongHandlesRejectCounterAndValueOperations) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable pointer checks need the LLVM backend";
#endif
    for (const char* operation : {"p.get();", "p.set(9);", "p.refs();", "p.borrows();",
                                  "p.is_givenback();", "p.alias();", "p.weak();",
                                  "p.own();", "p.borrow();", "p.giveback();"}) {
        const auto source = pointerProgram(std::string(R"FIN(
    snapshot.release();
    delete snapshot;
    p.release();
)FIN") + operation + "\n");
        const auto result = runPointerProgram(source);
        EXPECT_EQ(result.find("run failed:"), 0u) << operation << "\n" << result;
        EXPECT_NE(result.find("this pointer was already released"), std::string::npos)
            << operation << "\n" << result;
    }
}

#if defined(FIN_TESTS_SANITIZE) && defined(__linux__)
TEST(Soundness_StdptrLifetimes, ExplicitHandleLifecycleIsLeakSanitizerClean) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable pointer checks need the LLVM backend";
#endif
    EXPECT_EQ(runPointerProgram(kPointerLifecycle, true), "pointer cleanup ok\n");
}
#endif
