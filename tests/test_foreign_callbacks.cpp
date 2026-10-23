#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "Corpus.hpp"
#include "utils/Process.hpp"

namespace {

struct CallbackRun {
    int compiled = -1;
    int exited = -1;
    std::string diagnostics;
    std::string output;
};

CallbackRun runCallbackProgram(const std::string& source) {
    namespace fs = std::filesystem;
    using namespace fin::testing;
    const fs::path input = uniqueTempPath("foreign_callback", ".fin");
    const fs::path executable = uniqueTempPath("foreign_callback_exe");
    const fs::path output = uniqueTempPath("foreign_callback_output");
    {
        std::ofstream file(input);
        file << source;
    }
    const auto compiled = runFinc({input.string(), "-o", executable.string()});
    CallbackRun result;
    result.compiled = compiled.exitCode;
    result.diagnostics = stripAnsi(compiled.err);
    if (result.compiled == 0 && fs::exists(executable)) {
        result.exited = fin::runProcess({executable.string()}, output.string(), output.string());
        result.output = readProcessOutput(output.string());
    }
    std::error_code error;
    fs::remove(input, error);
    fs::remove(executable, error);
    fs::remove(output, error);
    return result;
}

const char* kQsort = R"(@define qsort(base: &void, count: ulong, size: ulong, compare: fn(&void, &void) => int) <noret>;
)";

}

#ifndef FIN_TESTS_HAVE_BACKEND
#define CALLBACK_TEST(name) \
    TEST(Soundness_ForeignCallbacks, name) { GTEST_SKIP() << "built with FIN_WITH_LLVM=OFF"; } \
    [[maybe_unused]] static void callback_body_##name()
#else
#define CALLBACK_TEST(name) TEST(Soundness_ForeignCallbacks, name)
#endif

CALLBACK_TEST(NoncapturingLambdaSortsThroughTheCAbi) {
    const auto result = runCallbackProgram(std::string(kQsort) + R"(fun main() <noret> {
    let values <[int]> = [9, -1, 2, -7, 0];
    qsort(&values[0], cast<ulong>(5), cast<ulong>(4), (a: &void, b: &void) <int> => {
        return *cast<&int>(a) - *cast<&int>(b);
    });
    printf("%d %d %d %d %d\n", values[0], values[1], values[2], values[3], values[4]);
    delete values;
}
)");
    ASSERT_EQ(result.compiled, 0) << result.diagnostics;
    EXPECT_EQ(result.exited, 0) << result.output;
    EXPECT_EQ(result.output, "-7 -1 0 2 9\n");
}

CALLBACK_TEST(NamedFunctionSortsThroughTheCAbi) {
    const auto result = runCallbackProgram(std::string(kQsort) + R"(fun descending(a: &void, b: &void) <int> {
    return *cast<&int>(b) - *cast<&int>(a);
}
fun main() <noret> {
    let values <[int]> = [-7, 9, 2];
    qsort(&values[0], cast<ulong>(3), cast<ulong>(4), descending);
    printf("%d %d %d\n", values[0], values[1], values[2]);
    delete values;
}
)");
    ASSERT_EQ(result.compiled, 0) << result.diagnostics;
    EXPECT_EQ(result.exited, 0) << result.output;
    EXPECT_EQ(result.output, "9 2 -7\n");
}

CALLBACK_TEST(ThreadEntryReceivesContextAndReturnsItsResult) {
    const auto result = runCallbackProgram(R"(#c_ifdef FIN_TARGET_WINDOWS
@define _beginthreadex(attr: &void, stack: uint, body: fn(&void) => uint, arg: &void, flags: uint, id: &uint) <ulong>;
@define memcpy(dst: &void, src: &void, count: ulong) <&void>;
@define WaitForSingleObject(handle: &void, timeout: uint) <uint>;
@define GetExitCodeThread(handle: &void, result: &uint) <int>;
@define CloseHandle(handle: &void) <int>;
#c_else
@define pthread_create(out: &void, attr: &void, body: fn(&void) => &void, arg: &void) <int>;
@define pthread_join(thread: &void, result: &void) <int>;
#c_endif
fun main() <noret> {
    let value <&int> = new int(42);
#c_ifdef FIN_TARGET_WINDOWS
    let handle <ulong> = _beginthreadex(null, cast<uint>(0), (arg: &void) <uint> => {
        return cast<uint>(*cast<&int>(arg));
    }, value, cast<uint>(0), null);
    blame handle != cast<ulong>(0);
    let thread <&void> = null;
    memcpy(&thread, &handle, cast<ulong>(8));
    blame WaitForSingleObject(thread, cast<uint>(-1)) == cast<uint>(0);
    let result <uint> = cast<uint>(0);
    blame GetExitCodeThread(thread, &result) != 0;
    blame CloseHandle(thread) != 0;
    printf("%u\n", result);
#c_else
    let thread <&void> = null;
    blame pthread_create(&thread, null, (arg: &void) <&void> => {
        return arg;
    }, value) == 0;
    let result <&void> = null;
    blame pthread_join(thread, &result) == 0;
    blame result == cast<&void>(value);
    printf("%d\n", *cast<&int>(result));
#c_endif
    delete value;
}
)");
    ASSERT_EQ(result.compiled, 0) << result.diagnostics;
    EXPECT_EQ(result.exited, 0) << result.output;
    EXPECT_EQ(result.output, "42\n");
}

CALLBACK_TEST(CapturedStateCannotBeDroppedAtTheCAbi) {
    const auto result = runCallbackProgram(std::string(kQsort) + R"(fun main() <noret> {
    let direction <int> = -1;
    let values <[int]> = [2, 1];
    qsort(&values[0], cast<ulong>(2), cast<ulong>(4), (a: &void, b: &void) <int> => {
        return direction * (*cast<&int>(a) - *cast<&int>(b));
    });
}
)");
    EXPECT_NE(result.compiled, 0);
    EXPECT_NE(result.diagnostics.find("capturing function value as a C callback"), std::string::npos)
        << result.diagnostics;
}

CALLBACK_TEST(RuntimeCallbackValuesAreRefusedWithoutAContext) {
    const auto result = runCallbackProgram(std::string(kQsort) + R"(fun sort(compare: fn(&void, &void) => int) <noret> {
    let values <[int]> = [2, 1];
    qsort(&values[0], cast<ulong>(2), cast<ulong>(4), compare);
}
fun main() <noret> {}
)");
    EXPECT_NE(result.compiled, 0);
    EXPECT_NE(result.diagnostics.find("runtime function value as a C callback"), std::string::npos)
        << result.diagnostics;
}

CALLBACK_TEST(AggregateCallbackParametersAreRefused) {
    const auto result = runCallbackProgram(R"(struct Point { x <int>, y <int> }
@define accept_point(callback: fn(Point) => int) <noret>;
fun main() <noret> {}
)");
    EXPECT_NE(result.compiled, 0);
    EXPECT_NE(result.diagnostics.find("a C callback with an aggregate parameter"), std::string::npos)
        << result.diagnostics;
}
