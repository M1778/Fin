#include "Corpus.hpp"
#include "utils/Process.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::string runContainerProgram(const std::string& source, bool checkLeaks = false) {
    namespace fs = std::filesystem;
    using namespace fin::testing;
    const fs::path dir = uniqueTempPath("fin_container_lifetime");
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

const char* kCollectionLifecycle = R"FIN(
import { Collection } from collection::std;
fun main() <noret> {
    for (let round <int> = 0; round < 32; round++) {
        let values <Collection<int>> = Collection::<int>{};
        values.free_collection();
        values.free_collection();
        for (let i <int> = 0; i < 128; i++) { values.push(i); }
        values.reserve(512);
        blame values.len() == 128;
        blame values.get(127) == 127;
        values.clear();
        blame values.is_empty();
        values.push(round);
        blame values.get(0) == round;
        values.free_collection();
        blame values.len() == 0;
        values.free_collection();
        values.push(77);
        blame values.get(0) == 77;
        values.free_collection();
        let heap <&Collection<int>> = new Collection::<int>{};
        heap.push(round);
        heap.free_collection();
        delete heap;
    }
    printf("collection cleanup ok\n");
}
)FIN";

const char* kHashMapLifecycle = R"FIN(
import { HashMap } from hashmap::std;
fun main() <noret> {
    for (let round <int> = 0; round < 32; round++) {
        let values <HashMap<int, int>> = HashMap::<int, int>();
        values.free_hashmap();
        values.free_hashmap();
        for (let i <int> = 0; i < 128; i++) { values.__set(i, i * 3); }
        for (let i <int> = 0; i < 128; i += 2) { blame values.remove(i); }
        for (let i <int> = 128; i < 256; i++) { values.__set(i, i * 3); }
        blame values.len() == 192;
        blame !values.exists(0);
        blame values.__get(127) == 381;
        blame values.__get(255) == 765;
        values.clear();
        blame values.is_empty();
        values.__set(round, 99);
        blame values.__get(round) == 99;
        values.free_hashmap();
        values.free_hashmap();
        blame values.is_empty();
        blame values.slot_count() == 0;
        values.__set(7, 11);
        blame values.__get(7) == 11;
        values.free_hashmap();
        let heap <&HashMap<int, int>> = new HashMap::<int, int>{};
        heap.__set(1, round);
        heap.free_hashmap();
        delete heap;
    }
    printf("hashmap cleanup ok\n");
}
)FIN";

}

TEST(Soundness_ContainerLifetimes, CollectionCleanupAllowsEmptyRepeatedCleanupAndReuse) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable container checks need the LLVM backend";
#endif
    EXPECT_EQ(runContainerProgram(kCollectionLifecycle), "collection cleanup ok\n");
}

TEST(Soundness_ContainerLifetimes, HashMapCleanupReclaimsGrowthAndAllowsReuse) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable container checks need the LLVM backend";
#endif
    EXPECT_EQ(runContainerProgram(kHashMapLifecycle), "hashmap cleanup ok\n");
}

TEST(Soundness_ContainerLifetimes, NegativeCollectionReservationRefuses) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable container checks need the LLVM backend";
#endif
    const std::string source = R"FIN(
import { Collection } from collection::std;
fun main() <noret> {
    let values <Collection<int>> = Collection::<int>{};
    values.reserve(-1);
    values.free_collection();
}
)FIN";
    const auto result = runContainerProgram(source);
    EXPECT_EQ(result.find("run failed:"), 0u) << result;
    EXPECT_NE(result.find("collection capacity cannot be negative"), std::string::npos) << result;
}

TEST(Soundness_ContainerLifetimes, CleanupLeavesStoredAllocationsWithTheirCaller) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable container checks need the LLVM backend";
#endif
    const std::string source = R"FIN(
import { Collection } from collection::std;
import { HashMap } from hashmap::std;
import { clone, equals, free_str } from strings::std;
fun main() <noret> {
    let text <string> = clone("caller owned");
    let value <&int> = new int(73);
    let texts <Collection<string>> = Collection::<string>{};
    texts.push(text);
    texts.free_collection();
    let values <HashMap<string, &int>> = HashMap::<string, &int>{};
    values.__set(text, value);
    values.free_hashmap();
    blame equals(text, "caller owned");
    blame *value == 73;
    free_str(text);
    delete value;
    printf("caller ownership ok\n");
}
)FIN";
    EXPECT_EQ(runContainerProgram(source), "caller ownership ok\n");
}

#if defined(FIN_TESTS_SANITIZE) && defined(__linux__)
TEST(Soundness_ContainerLifetimes, PrimitiveLifecycleIsLeakSanitizerClean) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable container checks need the LLVM backend";
#endif
    EXPECT_EQ(runContainerProgram(kCollectionLifecycle, true), "collection cleanup ok\n");
    EXPECT_EQ(runContainerProgram(kHashMapLifecycle, true), "hashmap cleanup ok\n");
}
#endif
