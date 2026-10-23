#include "Corpus.hpp"
#include "utils/Process.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

class StageArrayRegressions : public ::testing::TestWithParam<std::string> {};

std::string arrayCompiler(const std::string& stage) {
    if (stage == "Reference") return fin::testing::fincBinary();
    std::string compiler = stage == "Stage2" ? FINC_STAGE2_BINARY : FINC_STAGE3_BINARY;
#ifdef _WIN32
    if (!std::filesystem::exists(compiler) && std::filesystem::exists(compiler + ".exe"))
        compiler += ".exe";
#endif
    return compiler;
}

void checkArrayProgram(const std::string& compiler, const std::string& source,
                       const std::string& expected, const std::string& diagnostic = "") {
    namespace fs = std::filesystem;
    using namespace fin::testing;
    const fs::path dir = uniqueTempPath("fin_stage_array");
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
        if (!diagnostic.empty()) {
            ASSERT_EQ(compiled, 1) << stripAnsi(readProcessOutput(error.string()));
            EXPECT_NE(stripAnsi(readProcessOutput(error.string())).find(diagnostic), std::string::npos);
            EXPECT_FALSE(fs::exists(executable));
            continue;
        }
        ASSERT_EQ(compiled, 0) << stripAnsi(readProcessOutput(error.string()));
        const int status = fin::runProcess({executable.string()}, output.string(), error.string());
        ASSERT_EQ(status, 0) << readProcessOutput(error.string());
        EXPECT_EQ(readProcessOutput(output.string()), expected);
    }
}

TEST_P(StageArrayRegressions, CharacterLiteralsKeepTheirArrayElementWidth) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun check(values: [char]) <noret> {
    blame values.length == 4;
    blame values[0] == 'x' && values[1] == 'y';
    blame values[2] == '\0' && values[3] == '\n';
}
fun main() <noret> {
    let explicit <[char]> = ['x', 'y', '\0', '\n'];
    let fixed <[char, 4]> = ['x', 'y', '\0', '\n'];
    let integers <[int]> = ['x', 'y'];
    check(explicit);
    blame explicit[1] == 'y' && fixed[1] == 'y';
    blame fixed[2] == '\0' && fixed[3] == '\n';
    blame integers[0] == 120 && integers[1] == 121;
    delete explicit;
    delete integers;
    printf("character arrays ok\n");
}
)FIN", "character arrays ok\n");
}

TEST_P(StageArrayRegressions, CharacterLiteralsWidenInScalarValueContexts) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun character_code() <int> { return 'x'; }
fun short_code() <short> { return 'y'; }
fun main() <noret> {
    let integer <int> = 'x';
    let narrow <short> = 'y';
    let wide <long> = 'z';
    blame character_code() == 120 && short_code() == 121;
    blame integer == 120 && narrow == 121 && wide == 122;
    printf("character scalars ok\n");
}
)FIN", "character scalars ok\n");
}

TEST_P(StageArrayRegressions, ArrayLiteralsUseEachDeclaredValueContext) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
struct Box { values <[int]> }
fun consume(values: [int]) <noret> {
    blame values.length == 2;
    blame values[0] == 120 && values[1] == 121;
    delete values;
}
fun make() <[int]> { return ['x', 'y']; }
fun main() <noret> {
    consume(['x', 'y']);
    let returned <[int]> = make();
    consume(returned);
    let box <Box> = Box{values: ['x', 'y']};
    consume(box.values);
    let values <[int]> = [9, 10];
    {
        let values <[int]> = ['x', 'y'];
        consume(values);
    }
    blame values[0] == 9 && values[1] == 10;
    delete values;
    printf("array contexts ok\n");
}
)FIN", "array contexts ok\n");
}

TEST_P(StageArrayRegressions, ArrayParametersReleaseTheirAllocation) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun consume(values: [int]) <noret> {
    blame values.length == 2 && values[0] == 4 && values[1] == 7;
    delete values;
}
fun consume_bytes(values: [char]) <noret> {
    blame values.length == 2 && values[0] == 'x' && values[1] == 'y';
    delete values;
}
fun free_scalar(value: &int) <noret> { blame *value == 42; delete value; }
fun main() <noret> {
    let values <[int]> = [4, 7];
    let bytes <[char]> = ['x', 'y'];
    consume(values);
    consume_bytes(bytes);
    free_scalar(new int(42));
    printf("array parameters ok\n");
}
)FIN", "array parameters ok\n");
}

TEST_P(StageArrayRegressions, ArrayArgumentsDoNotDecayToRawPointers) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun free_raw(value: &int) <noret> { delete value; }
fun main() <noret> {
    let values <[int]> = [42];
    free_raw(values);
}
)FIN", "", "got '[int]'");
}

TEST_P(StageArrayRegressions, GenericFixedArrayFieldsKeepValuesAcrossReadsAndWrites) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
struct Box<T> { values <[T, 2]> }
fun main() <noret> {
    let floats <Box<float>> = Box::<float>{values: [1.5, 2.5]};
    blame floats.values[0] == 1.5 && floats.values[1] == 2.5;
    floats.values[1] = 3.5;
    floats.values[0] += 1.0;
    blame floats.values[0] == 2.5 && floats.values[1] == 3.5;
    let integers <Box<int>> = Box::<int>{values: [3, 7]};
    blame integers.values[0] == 3 && integers.values[1] == 7;
    integers.values[1] += 5;
    blame integers.values[0] == 3 && integers.values[1] == 12;
    let bytes <Box<char>> = Box::<char>{values: ['x', 'y']};
    blame bytes.values[0] == 'x' && bytes.values[1] == 'y';
    bytes.values[1] = 'z';
    blame bytes.values[0] == 'x' && bytes.values[1] == 'z';
    printf("generic arrays ok\n");
}
)FIN", "generic arrays ok\n");
}

TEST_P(StageArrayRegressions, ArrayLiteralElementsEvaluateOnce) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
let counter <int> = 0;
fun next() <int> { counter += 1; return counter; }
fun main() <noret> {
    let values <[int]> = [next(), next()];
    blame counter == 2;
    blame values[0] == 1 && values[1] == 2;
    delete values;
    printf("array elements once\n");
}
)FIN", "array elements once\n");
}

TEST_P(StageArrayRegressions, StreamReusesReleasedStorageWithCharacterLiterals) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
import { Stream } from stdio::std;
fun main() <noret> {
    let original <[char]> = ['a', '\0', 'b'];
    let stream <Stream> = Stream(original, 3);
    let head <[char]> = stream.read(2);
    blame head.length == 2 && head[0] == 'a' && head[1] == '\0';
    delete head;
    stream.free_stream();
    stream.free_stream();
    blame stream.capacity() == 0 && stream.remaining() == 0 && stream.tell() == 0;
    let text <[char]> = ['x', 'y'];
    blame stream.write(text, 2) == 2;
    delete text;
    stream.rewind();
    let reread <[char]> = stream.read_all();
    blame reread.length == 2 && reread[0] == 'x' && reread[1] == 'y';
    delete reread;
    stream.free_stream();
    printf("stream reuse ok\n");
}
)FIN", "stream reuse ok\n");
}

TEST_P(StageArrayRegressions, DereferencedArrayRootsEvaluateOnceForReadsAndStores) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun root(cells: &[int], count: &int) <&[int]> { *count += 1; return cells; }
fun index(count: &int) <int> { *count += 1; return 1; }
fun value(count: &int) <int> { *count += 1; return 7; }
fun main() <noret> {
    let roots <int> = 0;
    let indices <int> = 0;
    let values <int> = 0;
    let cells <[int]> = [2, 1];
    (*root(&cells, &roots))[index(&indices)] = value(&values);
    blame roots == 1 && indices == 1 && values == 1;
    blame cells[0] == 2 && cells[1] == 7;
    (*root(&cells, &roots))[index(&indices)] += value(&values);
    blame roots == 2 && indices == 2 && values == 2;
    blame cells[0] == 2 && cells[1] == 14;
    blame (*root(&cells, &roots))[index(&indices)] == 14;
    blame roots == 3 && indices == 3 && values == 2;
    delete cells;
    printf("array root effects ok\n");
}
)FIN", "array root effects ok\n");
}

INSTANTIATE_TEST_SUITE_P(Compilers, StageArrayRegressions,
                        ::testing::Values("Reference", "Stage2", "Stage3"),
                        [](const auto& info) { return info.param; });

}
