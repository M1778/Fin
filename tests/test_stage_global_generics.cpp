#include "Corpus.hpp"
#include "utils/Process.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

class StageGlobalGenerics : public ::testing::TestWithParam<std::string> {};

std::string globalGenericCompiler(const std::string& stage) {
    if (stage == "Reference") return fin::testing::fincBinary();
    std::string compiler = stage == "Stage2" ? FINC_STAGE2_BINARY : FINC_STAGE3_BINARY;
#ifdef _WIN32
    if (!std::filesystem::exists(compiler) && std::filesystem::exists(compiler + ".exe"))
        compiler += ".exe";
#endif
    return compiler;
}

void checkGlobalProgram(const std::string& compiler, const std::string& source,
                        const std::string& expected) {
    namespace fs = std::filesystem;
    using namespace fin::testing;
    const fs::path dir = uniqueTempPath("fin_stage_global_generics");
    fs::create_directories(dir);
    struct Cleanup {
        fs::path path;
        fs::path previous;
        ~Cleanup() {
            std::error_code ec;
            fs::current_path(previous, ec);
            fs::remove_all(path, ec);
        }
    } cleanup{dir, fs::current_path()};
    fs::current_path(fs::path(testsDir()).parent_path());
    const fs::path input = dir / "main.fin";
    const fs::path executable = dir / "program.exe";
    const fs::path output = dir / "output.txt";
    const fs::path error = dir / "error.txt";
    {
        std::ofstream file(input, std::ios::binary);
        file << source;
        ASSERT_TRUE(file.good());
    }
    for (const std::string level : {"-O0", "-O2"}) {
        SCOPED_TRACE(level);
        const int compiled = fin::runProcess(
            {compiler, input.string(), level, "-o", executable.string()},
            output.string(), error.string(), {{"FIN_LIBS", ""}});
        ASSERT_EQ(compiled, 0) << stripAnsi(readProcessOutput(error.string()));
        const int status = fin::runProcess({executable.string()}, output.string(), error.string());
        ASSERT_EQ(status, 0) << readProcessOutput(error.string());
        EXPECT_EQ(readProcessOutput(output.string()), expected);
    }
}

TEST_P(StageGlobalGenerics, GlobalAndLocalReceiversShareConcreteElementLayout) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = globalGenericCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkGlobalProgram(compiler, R"FIN(
import { Channel } from sync::std;
let shared <&Channel<int>>;
fun enqueue() <noret> {
    blame shared.send(0);
    blame shared.send(1);
}
fun main() <noret> {
    let channel <&Channel<int>> = Channel::<int>::shared(4);
    shared = channel;
    enqueue();
    let first <auto> = channel.recv();
    let second <auto> = channel.recv();
    blame first.unwrap_or(-1) == 0 && second.unwrap_or(-1) == 1;
    blame channel.send(2);
    blame channel.send(3);
    let third <auto> = shared.recv();
    let fourth <auto> = shared.recv();
    blame third.unwrap_or(-1) == 2 && fourth.unwrap_or(-1) == 3;
    channel.free_channel();
    delete channel;
    printf("global generic receivers ok\n");
}
)FIN", "global generic receivers ok\n");
}

TEST_P(StageGlobalGenerics, GlobalBindingsRemainVisibleUntilEachShadowDeclaration) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = globalGenericCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkGlobalProgram(compiler, R"FIN(
let value <int>;
fun read_global() <int> { return value; }
fun shadow_at_function_scope() <int> {
    value = 11;
    let value <int> = value + 1;
    return value;
}
fun main() <noret> {
    value = 7;
    blame read_global() == 7;
    {
        blame value == 7;
        let value <int> = value + 1;
        blame value == 8;
        value = 9;
        blame read_global() == 7;
    }
    blame value == 7;
    {
        let value <int> = value + 2;
        blame value == 9;
    }
    blame value == 7;
    blame shadow_at_function_scope() == 12;
    blame value == 11;
    printf("global shadowing ok\n");
}
)FIN", "global shadowing ok\n");
}

TEST_P(StageGlobalGenerics, NestedGenericShadowDoesNotCaptureGlobalReceivers) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = globalGenericCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkGlobalProgram(compiler, R"FIN(
import { Channel } from sync::std;
let shared <&Channel<int>>;
fun enqueue() <noret> {
    blame shared.send(0);
    blame shared.send(1);
}
fun main() <noret> {
    let channel <&Channel<int>> = Channel::<int>::shared(4);
    shared = channel;
    enqueue();
    let first <auto> = channel.recv();
    let second <auto> = shared.recv();
    blame first.unwrap_or(-1) == 0 && second.unwrap_or(-1) == 1;
    {
        let shared <&Channel<char>> = Channel::<char>::shared(2);
        blame shared.send('x');
        blame shared.send('y');
        let x <auto> = shared.recv();
        let y <auto> = shared.recv();
        blame x.unwrap_or('!') == 'x' && y.unwrap_or('!') == 'y';
        shared.free_channel();
        delete shared;
    }
    blame shared.send(2);
    blame shared.send(3);
    let third <auto> = channel.recv();
    let fourth <auto> = shared.recv();
    blame third.unwrap_or(-1) == 2 && fourth.unwrap_or(-1) == 3;
    channel.free_channel();
    delete channel;
    printf("generic shadowing ok\n");
}
)FIN", "generic shadowing ok\n");
}

INSTANTIATE_TEST_SUITE_P(Compilers, StageGlobalGenerics,
                        ::testing::Values("Reference", "Stage2", "Stage3"),
                        [](const auto& info) { return info.param; });

}
