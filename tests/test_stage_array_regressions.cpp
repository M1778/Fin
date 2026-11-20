// =============================================================================
// Fin Programming Language Compiler
//
// Made with ❤️ for someone special.
//
// This project is genuinely built on love and dedication, a way to express your love can be done through Fin.
// Fin is a semi-object-orientated programming language that specialized in control, giving so much control to the user while asking for the bare minimum.
// Fin is the name of the Angel from BlackNightmare-Stories not yet published.
// “Love is the reason this project exists”
// -----------------------------------------------------------------------------
// Author: M1778
// Repository: https://github.com/M1778/Fin
// Profile: https://github.com/M1778M/
// -----------------------------------------------------------------------------
// Copyright (C) 2027 M1778
//
// This file is part of the Fin Programming Language Compiler.
//
// Fin is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Fin is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Fin.  If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================

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
                       const std::string& expected, const std::string& diagnostic = "",
                       const std::string& runtimeDiagnostic = "") {
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
            const std::string message = stripAnsi(readProcessOutput(error.string()));
            EXPECT_NE(message.find(diagnostic), std::string::npos) << message;
            EXPECT_EQ(message.find("Operand is null"), std::string::npos) << message;
            EXPECT_FALSE(fs::exists(executable));
            continue;
        }
        ASSERT_EQ(compiled, 0) << stripAnsi(readProcessOutput(error.string()));
        const int status = fin::runProcess({executable.string()}, output.string(), error.string());
        if (!runtimeDiagnostic.empty()) {
            EXPECT_NE(status, 0);
            const std::string message = stripAnsi(readProcessOutput(output.string()) +
                                                  readProcessOutput(error.string()));
            EXPECT_NE(message.find(runtimeDiagnostic), std::string::npos) << message;
            continue;
        }
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

TEST_P(StageArrayRegressions, IntegerCastsToLongPreserveSourceSignedness) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun minimum() <int> { return -2147483648; }
fun main() <noret> {
    let negative <int> = -1;
    let smallest <int> = minimum();
    let positive <uint> = cast<uint>(-1);
    let byte <char> = cast<char>(-1);
    let narrow <short> = cast<short>(-1);
    blame cast<long>(negative) == -1;
    blame cast<long>(smallest) == -2147483648;
    blame cast<long>(minimum()) == -2147483648;
    blame cast<long>(positive) == 4294967295;
    blame cast<long>(byte) == -1;
    blame cast<long>(narrow) == -1;
    blame cast<long>(true) == 1;
    printf("integer cast signedness ok\n");
}
)FIN", "integer cast signedness ok\n");
}

TEST_P(StageArrayRegressions, LongArrayElementsKeepTheirArithmeticWidth) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun main() <noret> {
    let values <[long]> = [5000000000, 7];
    let unsigned_values <[ulong]> = [5000000000, 7];
    blame values[0] * 2 + values[1] == 10000000007;
    blame unsigned_values[0] * 2 + unsigned_values[1] == 10000000007;
    values[1] = values[0] * 2;
    blame values[1] == 10000000000;
    delete values;
    delete unsigned_values;
    printf("long array arithmetic ok\n");
}
)FIN", "long array arithmetic ok\n");
}

TEST_P(StageArrayRegressions, LongParametersPreserveNegativeIntegerArguments) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun identity(n: long) <long> { return n; }
fun main() <noret> {
    let negative <int> = -1;
    let unsigned_value <uint> = cast<uint>(-1);
    blame identity(-1) == -1;
    blame identity(negative) == -1;
    blame identity(unsigned_value) == 4294967295;
    printf("long arguments preserve values\n");
}
)FIN", "long arguments preserve values\n");
}

TEST_P(StageArrayRegressions, NativePathsPreserveRootsAndOwnedResults) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
import { is_absolute, is_relative, join, basename, dirname, extname, stem, normalize, free_path } from path::std;
import { equals } from strings::std;
fun expect(actual: string, expected: string) <noret> {
    blame equals(actual, expected);
    free_path(actual);
}
fun main() <noret> {
    expect(join("a", "b"), "a/b");
    expect(basename("/a/b/"), "b");
    expect(dirname("/a/b/"), "/a");
    expect(extname("/a/archive.tar.gz"), ".gz");
    expect(stem("/a/archive.tar.gz"), "archive.tar");
    expect(extname(".gitignore"), "");
    expect(normalize("a/../../b"), "../b");
    expect(normalize("/a/../../b"), "/b");
    #c_ifdef FIN_TARGET_WINDOWS
    blame is_absolute("C:\\work\\file.txt") && is_absolute("c:/work/file.txt");
    blame is_absolute("\\\\server\\share\\file") && is_absolute("//server/share");
    blame is_relative("C:file.txt") && is_relative("\\file.txt") && is_relative("/file.txt");
    expect(basename("C:\\work\\file.txt"), "file.txt");
    expect(dirname("C:\\work\\file.txt"), "C:\\work");
    expect(basename("C:\\"), "C:\\");
    expect(dirname("C:\\file.txt"), "C:\\");
    expect(basename("C:"), "C:");
    expect(dirname("C:child"), "C:");
    expect(basename("\\\\server\\share\\"), "\\\\server\\share");
    expect(dirname("\\\\server\\share\\file"), "\\\\server\\share");
    expect(join("D:\\base", "C:\\work\\file.txt"), "C:\\work\\file.txt");
    expect(join("C:\\base", "\\child"), "C:\\child");
    expect(join("\\\\server\\share\\base", "/child"), "\\\\server\\share/child");
    expect(join("C:\\base", "c:child"), "c:\\base/child");
    expect(join("C:\\base", "D:child"), "D:child");
    expect(join("C:", "child"), "C:child");
    expect(normalize("C:\\a\\.\\b\\..\\c"), "C:/a/c");
    expect(normalize("C:a\\..\\..\\b"), "C:../b");
    expect(normalize("C:"), "C:");
    expect(normalize("\\\\server\\share\\a\\..\\..\\b"), "//server/share/b");
    expect(normalize("\\\\server\\share\\"), "//server/share");
    expect(extname("C:\\a.b\\file"), "");
    expect(stem("C:\\a.b\\file.txt"), "file");
    #c_else
    blame !is_absolute("C:\\work\\file.txt") && is_absolute("/file.txt");
    expect(basename("C:\\work\\file.txt"), "C:\\work\\file.txt");
    expect(dirname("C:\\work\\file.txt"), ".");
    expect(join("D:\\base", "C:\\work\\file.txt"), "D:\\base/C:\\work\\file.txt");
    expect(normalize("C:\\a\\..\\b"), "C:\\a\\..\\b");
    #c_endif
    printf("native paths ok\n");
}
)FIN", "native paths ok\n");
}

TEST_P(StageArrayRegressions, NegatedBlameUsesKeywordTokensNotMacroNames) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun negate(value: bool) <bool> { return !(value); }
fun main() <noret> {
    let value <bool> = false;
    blame !(value);
    blame ! (value);
    blame !value;
    blame !((value));
    blame !(1 == 2);
    blame !(value || !negate(value));
    blame ! /* not a macro */ (value);
    // blame !(true); ghost!(1);
    let text <string> = "blame !(value); ghost!(1);";
    blame negate(value);
    printf("negated keywords ok\n");
}
)FIN", "negated keywords ok\n");
}

TEST_P(StageArrayRegressions, GenuineIdentifierMacroCallsStillExpand) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
@macro blameful(value) { return quote { $value + 1; }; }
@macro identity(value) { return quote { $value; }; }
fun main() <noret> {
    blame blameful!(6) == 7;
    blame blameful ! (8) == 9;
    blame identity!(true);
    blame 1 != 2;
    printf("identifier macros ok\n");
}
)FIN", "identifier macros ok\n");
}

TEST_P(StageArrayRegressions, UnknownIdentifierMacrosKeepTheirDiagnostic) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun main() <noret> { blameghost!(1); }
)FIN", "", "Undefined macro 'blameghost!'");
}

TEST_P(StageArrayRegressions, MacroInvocationSkipsCommentsAroundTheBang) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
@macro identity(value) { return quote { $value; }; }
fun main() <noret> {
    blame identity /* before */ !(42) == 42;
    blame identity! /* after */ (43) == 43;
    blame identity /* both */ ! /* sides */ (44) == 44;
    blame identity /* outer /* nested */ end */ !(45) == 45;
    blame identity! /* outer /* nested */ end */ (47) == 47;
    blame identity // before bang
        ! // after bang
        (46) == 46;
    let list <{int, int}> = identity /* shaped */ ! /* bracket */ [7, 9];
    blame list[0] == 7 && list[1] == 9;
    delete list.0;
    delete list.1;
    let pairs <{int, int}> = identity /* shaped */ ! /* brace */ {10 => 70, 20 => 90};
    blame pairs[10] == 70 && pairs[20] == 90;
    delete pairs.0;
    delete pairs.1;
    // ghost /* before */ ! /* after */ (1);
    /* outer /* nested */ ghost /* before */ ! (1); */
    let text <string> = "ghost /* before */ ! /* after */ (1);";
    let value <bool> = false;
    blame !(value);
    printf("macro trivia ok\n");
}
)FIN", "macro trivia ok\n");
}

TEST_P(StageArrayRegressions, DottedMacroInvocationSkipsCommentsAroundTheBang) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
import "tests/samples/macros.fin";
fun main() <noret> {
    blame macros.magic_add /* before */ ! /* after */ (20, 22) == 42;
    printf("dotted macro trivia ok\n");
}
)FIN", "dotted macro trivia ok\n");
}

TEST_P(StageArrayRegressions, CommentSeparatedUnknownMacroKeepsItsDiagnostic) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun main() <noret> { absent /* before */ ! /* after */ (1); }
)FIN", "", "Undefined macro 'absent!'");
}

TEST_P(StageArrayRegressions, MacroTriviaPreservesFollowingDiagnosticLine) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(@macro identity(value) { return quote { $value; }; }
fun main() <noret> {
    blame identity /* before
        */ ! /* after
        */ (42) == 42;

    absent!(1);
}
)FIN", "", ":7:");
}

TEST_P(StageArrayRegressions, UnterminatedMacroTriviaKeepsScannerDiagnostic) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    for (const std::string boundary : {"identity /* open", "identity! /* open"}) {
        SCOPED_TRACE(boundary);
        checkArrayProgram(compiler,
            "@macro identity(value) { return quote { $value; }; }\n"
            "fun main() <noret> {\n    " + boundary + "\n",
            "", "unterminated block comment");
    }
}

TEST_P(StageArrayRegressions, StringPrototypeLookupComparesKeyContents) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
import { new_buffer, free_buffer, free_string } from buffer::std;
fun main() <noret> {
    let buffer <auto> = new_buffer(16);
    buffer.write_string("alpha");
    let key <string> = buffer.to_string();
    let equal <string> = buffer.to_string();
    free_buffer(buffer);
    let items <{string, int}> = {key: 10, "b": 20};
    blame items["alpha"] == 10 && items[equal] == 10;
    blame items["b"] == 20;
    delete items.0;
    delete items.1;
    free_string(key);
    free_string(equal);
    printf("prototype string lookup ok\n");
}
)FIN", "prototype string lookup ok\n");
}

TEST_P(StageArrayRegressions, StringPrototypeStoreUpdatesEqualContentKeys) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
import { new_buffer, free_buffer, free_string } from buffer::std;
fun main() <noret> {
    let buffer <auto> = new_buffer(16);
    buffer.write_string("alpha");
    let key <string> = buffer.to_string();
    let equal <string> = buffer.to_string();
    free_buffer(buffer);
    let items <{string, int}> = {key: 10, "b": 20};
    items[equal] = 30;
    blame items.0.length == 2 && items.1[0] == 30;
    items["c"] = 40;
    blame items.0.length == 3 && items.1.length == 3;
    blame items["alpha"] == 30 && items["c"] == 40;
    delete items.0;
    delete items.1;
    free_string(key);
    free_string(equal);
    printf("prototype string store ok\n");
}
)FIN", "prototype string store ok\n");
}

TEST_P(StageArrayRegressions, StringPrototypeErasureRemovesOnlyTheMatchingEntry) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
import { new_buffer, free_buffer, free_string } from buffer::std;
fun main() <noret> {
    let buffer <auto> = new_buffer(16);
    buffer.write_string("alpha");
    let key <string> = buffer.to_string();
    let equal <string> = buffer.to_string();
    free_buffer(buffer);
    let items <{string, int}> = {"b": 20, key: 10, "c": 40};
    delete &items[equal];
    blame items.0.length == 2 && items.1.length == 2;
    blame items["b"] == 20 && items["c"] == 40;
    delete &items["missing"];
    blame items.0.length == 2 && items.1.length == 2;
    delete items.0;
    delete items.1;
    free_string(key);
    free_string(equal);
    printf("prototype string erase ok\n");
}
)FIN", "prototype string erase ok\n");
}

TEST_P(StageArrayRegressions, PrototypeIntegerKeysKeepLookupStoreAndErase) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun main() <noret> {
    let items <{int, int}> = {10: 1, 20: 2};
    items[10] = 7;
    items[30] = 3;
    blame items[10] == 7 && items[30] == 3;
    delete &items[20];
    blame items.0.length == 2 && items.1.length == 2;
    blame items[10] == 7 && items[30] == 3;
    delete &items[99];
    blame items.0.length == 2;
    delete items.0;
    delete items.1;
    printf("prototype integer controls ok\n");
}
)FIN", "prototype integer controls ok\n");
}

TEST_P(StageArrayRegressions, StringPrototypeKeysKeepPrefixesEmptyAndProjectionWritesDistinct) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun main() <noret> {
    let items <{string, int}> = {"alphabet": 1, "alpha": 2, "": 3};
    blame items["alpha"] == 2 && items[""] == 3;
    items.0[0] = "replacement";
    blame items["replacement"] == 1;
    delete &items["alph"];
    blame items.0.length == 3;
    delete &items["alpha"];
    blame items.0.length == 2 && items[""] == 3;
    delete items.0;
    delete items.1;
    let zero <{string, int}>;
    zero["first"] = 9;
    blame zero["first"] == 9;
    delete zero.0;
    delete zero.1;
    printf("prototype string boundaries ok\n");
}
)FIN", "prototype string boundaries ok\n");
}

TEST_P(StageArrayRegressions, StringPrototypeMissingKeyBlames) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun main() <noret> {
    let items <{string, int}> = {"alphabet": 1};
    printf("%d\n", items["alpha"]);
}
)FIN", "", "", "key is not in the prototype");
}

TEST_P(StageArrayRegressions, PrototypeStringProbeDoesNotDereferenceIntegerStorage) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun main() <noret> {
    let items <{any, int}> = {1: 10, 2: 20};
    delete &items["missing"];
    blame items.0.length == 2 && items.1.length == 2;
    blame items[1] == 10 && items[2] == 20;
    delete items.0;
    delete items.1;
    printf("prototype key kind guard ok\n");
}
)FIN", "prototype key kind guard ok\n");
}

TEST_P(StageArrayRegressions, PrototypeMixedKeyStorageComparesOnlyMatchingKinds) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun main() <noret> {
    let items <{any, int}> = {1: 10, "alpha": 20, 2: 30, "b": 40};
    blame items["alpha"] == 20 && items[1] == 10;
    items["alpha"] = 25;
    blame items.0.length == 4 && items["alpha"] == 25;
    delete &items[1];
    blame items["alpha"] == 25 && items[2] == 30;
    delete &items["alpha"];
    blame items[2] == 30 && items["b"] == 40;
    delete items.0;
    delete items.1;
    printf("prototype mixed keys ok\n");
}
)FIN", "prototype mixed keys ok\n");
}

TEST_P(StageArrayRegressions, PrototypeErasedKeyProjectionWritesRefuseBeforeDereferencing) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    const std::string diagnostic = GetParam() == "Reference" ? "" :
        "prototype key projection writes require a concrete key type";
    checkArrayProgram(compiler, R"FIN(
fun main() <noret> {
    let items <{any, int}> = {"alpha": 10};
    items.0[0] = 1;
    delete &items["alpha"];
    blame items.0.length == 1;
    delete items.0;
    delete items.1;
    printf("prototype erased projection control ok\n");
}
)FIN", "prototype erased projection control ok\n", diagnostic);
}

TEST_P(StageArrayRegressions, StringPrototypeRmUsesContentAndKeepsBothLengths) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
import { new_buffer, free_buffer, free_string } from buffer::std;
fun main() <noret> {
    let buffer <auto> = new_buffer(8);
    buffer.write_string("alpha");
    let key <string> = buffer.to_string();
    free_buffer(buffer);
    let items <{string, int}> = {"b": 20, "alpha": 10, "c": 30};
    blame items.rm(key);
    blame items.0.length == 2 && items.1.length == 2;
    blame items["b"] == 20 && items["c"] == 30;
    blame !items.rm(key);
    blame items.0.length == 2 && items.1.length == 2;
    delete items.0;
    delete items.1;
    free_string(key);
    printf("prototype rm ok\n");
}
)FIN", "prototype rm ok\n");
}

TEST_P(StageArrayRegressions, StringPrototypeNullKeysAvoidByteReads) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun main() <noret> {
    let absent <string> = null;
    let items <{string, int}> = {absent: 10, "": 20};
    blame items[absent] == 10 && items[""] == 20;
    items[absent] = 30;
    blame items.0.length == 2 && items[absent] == 30;
    delete &items[absent];
    blame items.0.length == 1 && items[""] == 20;
    delete items.0;
    delete items.1;
    printf("prototype null keys ok\n");
}
)FIN", "prototype null keys ok\n");
}

TEST_P(StageArrayRegressions, PrototypeKeyProjectionRejectsIncompatibleWords) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    if (GetParam() == "Reference") GTEST_SKIP() << "Reference rejects incompatible projected element types";
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun main() <noret> {
    let items <{string, int}> = {"alpha": 10};
    items.0[0] = 1;
    printf("%d\n", items["alpha"]);
}
)FIN", "", "prototype key projection writes require a matching key kind");
}

TEST_P(StageArrayRegressions, PrototypeErasedStoresPreserveExistingKeyKinds) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun main() <noret> {
    let items <{any, int}> = {1: 10};
    items["alpha"] = 20;
    blame items[1] == 10 && items["alpha"] == 20;
    delete items.0;
    delete items.1;
    printf("prototype erased store reference ok\n");
}
)FIN", "prototype erased store reference ok\n");
}

TEST_P(StageArrayRegressions, PrototypeKeyAnnotationsStayWithTheirBinding) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun main() <noret> {
    let items <{string, int}> = {"alpha": 10};
    {
        let items <int> = 7;
        blame items == 7;
    }
    items.0[0] = "replacement";
    blame items["replacement"] == 10;
    {
        let items <{int, int}> = {1: 20};
        items.0[0] = 2;
        blame items[2] == 20;
        delete items.0;
        delete items.1;
    }
    items.0[0] = "last";
    blame items["last"] == 10;
    delete items.0;
    delete items.1;
    printf("prototype scoped annotations ok\n");
}
)FIN", "prototype scoped annotations ok\n");
}

TEST_P(StageArrayRegressions, PrototypeKeyHalfAliasesKeepContentWrites) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun main() <noret> {
    let items <{string, int}> = {"alpha": 10, "b": 20};
    let keys <[string]> = items.0;
    let alias <[string]> = keys;
    alias[0] = "replacement";
    blame items["replacement"] == 10 && items["b"] == 20;
    delete alias;
    delete items.1;
    printf("prototype key aliases ok\n");
}
)FIN", "prototype key aliases ok\n");
}

TEST_P(StageArrayRegressions, PrototypeKeyHalfAliasesRejectIncompatibleWords) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    if (GetParam() == "Reference") GTEST_SKIP() << "Reference rejects incompatible projected element types";
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun main() <noret> {
    let items <{string, int}> = {"alpha": 10};
    let keys <[string]> = items.0;
    let alias <[string]> = keys;
    alias[0] = 1;
    printf("%d\n", items["alpha"]);
}
)FIN", "", "prototype key projection writes require a matching key kind");
}

TEST_P(StageArrayRegressions, PrototypeErasedKeyBuffersCannotEscapeThroughCalls) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    const std::string diagnostic = GetParam() == "Reference" ? "" :
        "prototype key buffers cannot escape through calls";
    checkArrayProgram(compiler, R"FIN(
fun replace(keys: [any]) <noret> { keys[0] = 1; }
fun main() <noret> {
    let items <{any, int}> = {"alpha": 10};
    replace(items.0);
    delete &items["alpha"];
    blame items.0.length == 1;
    delete items.0;
    delete items.1;
    printf("prototype escaped keys control ok\n");
}
)FIN", "prototype escaped keys control ok\n", diagnostic);
}

TEST_P(StageArrayRegressions, PrototypeKeyBuffersCannotEscapeThroughCasts) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    const std::string diagnostic = GetParam() == "Reference" ? "Invalid cast" :
        "prototype key buffers cannot escape through casts";
    checkArrayProgram(compiler, R"FIN(
fun main() <noret> {
    let items <{string, int}> = {"alpha": 10};
    let keys <[string]> = items.0;
    let raw <&string> = cast<&string>(keys);
    *raw = "replacement";
    blame items["replacement"] == 10;
    delete items.0;
    delete items.1;
    printf("prototype cast keys control ok\n");
}
)FIN", "prototype cast keys control ok\n", diagnostic);
}

TEST_P(StageArrayRegressions, PrototypeKeyBuffersCannotEscapeThroughReturns) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    const std::string diagnostic = GetParam() == "Reference" ? "" :
        "prototype key buffers cannot escape through returns";
    checkArrayProgram(compiler, R"FIN(
fun keys() <[string]> {
    let items <{string, int}> = {"alpha": 10};
    delete items.1;
    return items.0;
}
fun main() <noret> {
    let projected <[string]> = keys();
    blame projected[0] == "alpha";
    delete projected;
    printf("prototype returned keys control ok\n");
}
)FIN", "prototype returned keys control ok\n", diagnostic);
}

TEST_P(StageArrayRegressions, PrototypeLiteralKeyCleanupEvaluatesOnce) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    if (GetParam() == "Reference") GTEST_SKIP() << "Reference refuses an untyped prototype literal at delete";
    const std::string compiler = arrayCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkArrayProgram(compiler, R"FIN(
fun key(count: &int) <int> { *count += 1; return 7; }
fun main() <noret> {
    let count <int> = 0;
    delete {key(&count): 10}.0;
    blame count == 1;
    printf("prototype cleanup effects ok\n");
}
)FIN", "prototype cleanup effects ok\n");
}

INSTANTIATE_TEST_SUITE_P(Compilers, StageArrayRegressions,
                        ::testing::Values("Reference", "Stage2", "Stage3"),
                        [](const auto& info) { return info.param; });

}
