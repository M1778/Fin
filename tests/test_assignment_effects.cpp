#include "Corpus.hpp"
#include "utils/Process.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

std::string runEffects(const std::string& source, const std::vector<std::string>& options = {},
                       const std::string& compiler = fin::testing::fincBinary()) {
    namespace fs = std::filesystem;
    using namespace fin::testing;
    const fs::path dir = uniqueTempPath("fin_assignment_effects");
    fs::create_directories(dir);
    struct Cleanup {
        fs::path path;
        ~Cleanup() { std::error_code ec; fs::remove_all(path, ec); }
    } cleanup{dir};
    const fs::path input = dir / "main.fin";
    const fs::path exe = dir / "program.exe";
    const fs::path output = dir / "output.txt";
    const fs::path error = dir / "error.txt";
    {
        std::ofstream file(input, std::ios::binary);
        file << source;
    }
    std::vector<std::string> command{compiler, input.string(), "-o", exe.string()};
    command.insert(command.end(), options.begin(), options.end());
    const int compiled = fin::runProcess(command, output.string(), error.string());
    if (compiled != 0) return "compile failed: " + stripAnsi(readProcessOutput(error.string()));
    const int status = fin::runProcess({exe.string()}, output.string(), error.string());
    if (status != 0) return "run failed: " + std::to_string(status) + "\n" + readProcessOutput(error.string());
    return readProcessOutput(output.string());
}

const std::string allocationSource = R"FIN(
fun allocate(count: &int) <&int> {
    *count += 1;
    return new int(42);
}
fun main() <noret> {
    let count <int> = 0;
    let values <[&int]> = new [&int, 1]{};
    values[0] = allocate(&count);
    blame count == 1;
    blame *values[0] == 42;
    delete values[0];
    delete values;
    printf("allocated once\n");
}
)FIN";

}

TEST(Soundness_AssignmentEffects, AnArrayAssignmentAllocatesItsValueOnce) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    for (const std::string level : {"-O0", "-O2"}) {
        SCOPED_TRACE(level);
        EXPECT_EQ(runEffects(allocationSource, {level}), "allocated once\n");
    }
}

TEST(Soundness_AssignmentEffects, ArrayRootsIndicesAndValuesEachRunOnce) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string source = R"FIN(
fun root(values: &[int], count: &int) <&[int]> { *count += 1; return values; }
fun index(count: &int) <int> { *count += 1; return 0; }
fun value(count: &int) <int> { *count += 1; return 7; }
fun main() <noret> {
    let roots <int> = 0;
    let indices <int> = 0;
    let values <int> = 0;
    let cells <[int]> = [1];
    (*root(&cells, &roots))[index(&indices)] = value(&values);
    blame roots == 1 && indices == 1 && values == 1;
    blame cells[0] == 7;
    (*root(&cells, &roots))[index(&indices)] += value(&values);
    blame roots == 2 && indices == 2 && values == 2;
    blame cells[0] == 14;
    delete cells;
    printf("effects once\n");
}
)FIN";
    EXPECT_EQ(runEffects(source), "effects once\n");
}

TEST(Soundness_AssignmentEffects, PrototypeAndIndexOperatorStoresKeepSingleEvaluation) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string source = R"FIN(
struct Slot {
    value <int>,
    operator [](key: <int>) <int> { return self.value; }
    operator []=(key: <int>, value: <int>) <noret> { self.value = value; }
}
fun key(count: &int) <int> { *count += 1; return 1; }
fun value(count: &int) <int> { *count += 1; return 9; }
fun main() <noret> {
    let keys <int> = 0;
    let values <int> = 0;
    let table <{int, int}> = {1: 0};
    table[key(&keys)] = value(&values);
    blame keys == 1 && values == 1;
    blame table[1] == 9;
    let slot <Slot> = Slot{value: 0};
    slot[key(&keys)] = value(&values);
    blame keys == 2 && values == 2;
    blame slot.value == 9;
    delete table.0;
    delete table.1;
    printf("stores once\n");
}
)FIN";
    EXPECT_EQ(runEffects(source), "stores once\n");
}

TEST(Soundness_AssignmentEffects, ExistingSelfHostedStagesAllocateTheValueOnce) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    int available = 0;
    for (const std::string compiler : {FINC_STAGE2_BINARY, FINC_STAGE3_BINARY}) {
        if (!std::filesystem::exists(compiler)) continue;
        ++available;
        SCOPED_TRACE(compiler);
        EXPECT_EQ(runEffects(allocationSource, {}, compiler), "allocated once\n");
    }
    if (available == 0) GTEST_SKIP() << "Self-hosted stage binaries are not built";
}

TEST(Soundness_AssignmentEffects, DebugLocationsSurviveGenericBodyEmission) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string source = R"FIN(
fun identity<T>(value: T) <T> { return value; }
fun outer() <int> { return identity::<int>(42); }
fun main() <noret> { blame outer() == 42; printf("debug ok\n"); }
)FIN";
    EXPECT_EQ(runEffects(source, {"-g"}), "debug ok\n");
}
