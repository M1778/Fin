#include "Corpus.hpp"
#include "utils/Process.hpp"
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

class StageValueOwnership : public ::testing::TestWithParam<std::string> {};

std::string valueCompiler(const std::string& stage) {
    if (stage == "Reference") return fin::testing::fincBinary();
    std::string compiler = stage == "Stage2" ? FINC_STAGE2_BINARY : FINC_STAGE3_BINARY;
#ifdef _WIN32
    if (!std::filesystem::exists(compiler) && std::filesystem::exists(compiler + ".exe"))
        compiler += ".exe";
#endif
    return compiler;
}

// Builds the program with `compiler`, runs it, and compares stdout with the
// reference compiler's stdout. Both compilers must agree; the expected text
// pins what they must agree on.
void checkValueProgram(const std::string& compiler, const std::string& source,
                       const std::string& expected, bool checkLeaks = false, bool checkAddress = false) {
    namespace fs = std::filesystem;
    using namespace fin::testing;
    const fs::path dir = uniqueTempPath("fin_stage_value");
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
    const fs::path object = dir / "main.o";
    const fs::path output = dir / "output.txt";
    const fs::path error = dir / "error.txt";
    {
        std::ofstream file(input, std::ios::binary);
        file << source;
        ASSERT_TRUE(file.good());
    }
    for (const std::string level : {"-O0", "-O2"}) {
        SCOPED_TRACE(level);
        auto command = std::vector<std::string>{compiler, input.string(), level, "-o", executable.string()};
        if (checkLeaks) command = {compiler, input.string(), level, "-c", "-o", object.string()};
        const int compiled = fin::runProcess(
            command,
            output.string(), error.string(), {{"FIN_LIBS", ""}});
        ASSERT_EQ(compiled, 0) << stripAnsi(readProcessOutput(error.string()));
        if (checkLeaks) {
            auto link = fin::linkCommand({object.string()}, executable.string());
            link.push_back(checkAddress ? "-fsanitize=address" : "-fsanitize=leak");
            link.push_back("-no-pie");
            ASSERT_EQ(fin::runProcess(link, output.string(), error.string()), 0)
                << readProcessOutput(error.string());
        }
        const int status = fin::runProcess({executable.string()}, output.string(), error.string(),
            checkLeaks ? std::vector<std::pair<std::string, std::string>>{
                             {"LSAN_OPTIONS", "detect_leaks=1:exitcode=23"},
                             {"ASAN_OPTIONS", "detect_leaks=1:exitcode=23"}}
                       : std::vector<std::pair<std::string, std::string>>{});
        ASSERT_EQ(status, 0) << readProcessOutput(error.string());
        EXPECT_EQ(readProcessOutput(output.string()), expected);
    }
}

// A struct value is a copy: assigning one binding from another leaves the
// original alone, and mutating the copy does not reach back into it. The
// reference compiler lowers a struct as an aggregate value; the stage lowers
// one as a pointer to a heap box, so a copy there must be an explicit
// snapshot rather than the same pointer.
TEST_P(StageValueOwnership, AssignmentCopiesTheValue) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
struct Point { pub x <int>, pub y <int> }
fun main() <noret> {
    let source <Point> = Point{x: 7, y: 11};
    let copied <Point> = source;
    copied.x = 20;
    blame source.x == 7;
    blame source.y == 11;
    printf("assignment copies\n");
}
)FIN", "assignment copies\n");
}

// A nested struct field is a value too: copying the outer struct copies the
// inner one, so a write through the copy's inner field does not reach the
// original.
TEST_P(StageValueOwnership, NestedValueFieldsCopyWithTheOuterValue) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
struct Inner { pub v <int> }
struct Outer { pub i <Inner>, pub n <int> }
fun main() <noret> {
    let source <Outer> = Outer{i: Inner{v: 1}, n: 2};
    let copied <Outer> = source;
    copied.n = 99;
    copied.i.v = 77;
    blame source.n == 2;
    blame source.i.v == 1;
    printf("nested copies\n");
}
)FIN", "nested copies\n");
}

// A by-value parameter is a copy: the callee writes to it, and the caller's
// binding is unchanged.
TEST_P(StageValueOwnership, ByValueParameterIsACopy) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
struct Point { pub x <int>, pub y <int> }
fun bump(p: Point) <noret> {
    p.x = 100;
    p.y = 200;
}
fun main() <noret> {
    let source <Point> = Point{x: 7, y: 11};
    bump(source);
    blame source.x == 7;
    blame source.y == 11;
    printf("parameter copies\n");
}
)FIN", "parameter copies\n");
}

// A returned struct is a value: the caller owns what it got, and writing to
// it does not reach back into the callee's storage.
TEST_P(StageValueOwnership, ReturnedValueIsIndependentOfTheSource) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
struct Point { pub x <int>, pub y <int> }
fun snapshot(source: &Point) <Point> { return *source; }
fun main() <noret> {
    let source <Point> = Point{x: 7, y: 11};
    let returned <Point> = snapshot(&source);
    source.x = 99;
    blame returned.x == 7;
    blame returned.y == 11;
    returned.y = 42;
    blame source.y == 11;
    printf("returned values are independent\n");
}
)FIN", "returned values are independent\n");
}

// A pointer field shares its pointee: copying the struct must not copy what
// the pointer points at.
TEST_P(StageValueOwnership, PointerFieldsKeepSharingTheirPointee) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
struct Holder { pub p <&int> }
fun main() <noret> {
    let n <int> = 5;
    let source <Holder> = Holder{p: &n};
    let copied <Holder> = source;
    *copied.p = 9;
    blame n == 9;
    printf("pointer fields share\n");
}
)FIN", "pointer fields share\n");
}

TEST_P(StageValueOwnership, CleanupCompositionAndTiedMovesRemainOrdered) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
#[protocol(move_or_copy)]
@special mymove(s: $struct) <quote> {}
struct Track {
    pub v <int>,
    ~Track() { printf("dtor %d\n", self.v); }
}
struct Outer {
    pub child <Track>,
    ~Outer() { printf("outer\n"); }
}
fun pick(early: bool) <int> {
    let outer <Outer> = Outer{child: Track{v: 7}};
    if (early) { return outer.child.v; }
    printf("late\n");
    return outer.child.v;
}
fun main() <noret> {
    blame pick(true) == 7;
    blame pick(false) == 7;
    let z <Track> = Track{v: 0};
    {
        #[slaveof(z)] let deferred <Track> = Track{v: 1};
        let moved <Track> = @move(deferred);
        printf("body\n");
    }
    printf("after\n");
}
)FIN", "outer\ndtor 7\nlate\nouter\ndtor 7\nbody\ndtor 1\nafter\ndtor 0\n");
}

TEST_P(StageValueOwnership, LoopExitDestructorsRunExactlyOnce) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
struct Track {
    pub x <int>,
    ~Track() { printf("dtor %d\n", self.x); }
}
fun main() <noret> {
    for (let i <int> = 0; i < 4; i++) {
        let value <Track> = Track{x: i};
        if (i == 0) { continue; }
        if (i == 2) { break; }
        printf("body\n");
    }
    printf("done\n");
}
)FIN", "dtor 0\nbody\ndtor 1\ndtor 2\ndone\n");
}

TEST_P(StageValueOwnership, ReusedStorageIsZeroInitializedAgain) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
struct Value { pub x <int>, pub y <int> }
fun main() <noret> {
    for (let i <int> = 0; i < 4; i++) {
        let zero <Value>;
        blame zero.x == 0;
        zero.x = 99;
        let omitted <Value> = Value{x: i};
        blame omitted.y == 0;
        omitted.y = 123;
    }
    printf("reused zero values\n");
}
)FIN", "reused zero values\n");
}

TEST_P(StageValueOwnership, DereferencedStructAssignmentCopiesIntoItsHome) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
struct Value { pub x <int>, pub y <int> }
fun replace(p: &Value) <noret> {
    let next <Value> = Value{x: 12, y: 15};
    *p = next;
    next.x = 99;
}
fun main() <noret> {
    let value <Value> = Value{x: 3, y: 4};
    replace(&value);
    blame value.x == 12 && value.y == 15;
    printf("dereference value store ok\n");
}
)FIN", "dereference value store ok\n");
}

TEST_P(StageValueOwnership, GlobalStructAssignmentsKeepStaticStorage) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
struct Value { pub x <int>, pub y <int> }
let global <Value> = Value{x: 1, y: 2};
fun set() <noret> { global = Value{x: 12, y: 15}; }
fun main() <noret> {
    let home <&Value> = &global;
    set();
    blame global.x == 12 && home.y == 15;
    printf("global value home ok\n");
}
)FIN", "global value home ok\n");
}

#if defined(__linux__)
TEST_P(StageValueOwnership, RepeatedAggregateStorageIsLeakClean) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
struct Inner { pub x <int> }
struct Value {
    pub inner <Inner>, pub y <int>,
    fun address(self: &Self) <&int> { return &self.y; }
}
fun make(n: int) <Value> { return Value{inner: Inner{x: n}, y: n + 1}; }
fun snapshot(p: &Value) <Value> { return *p; }
fun read(v: Value) <int> { return v.inner.x + v.y; }
fun main() <noret> {
    let escaped <&int> = make(40).address();
    let value <Value> = make(1);
    let home <&Value> = &value;
    for (let i <int> = 0; i < 64; i++) {
        let local <Value> = make(i);
        let copied <Value> = local;
        copied.inner.x = 99;
        blame local.inner.x == i;
        value = snapshot(&local);
        blame home.y == i + 1;
        blame read(local) == 2 * i + 1;
        make(i);
        let heap <&Value> = new Value{inner: Inner{x: i}, y: i};
        blame heap.inner.x == i;
        delete heap;
    }
    blame *escaped == 41;
    printf("aggregate storage clean\n");
}
)FIN", "aggregate storage clean\n", true);
}

TEST_P(StageValueOwnership, ArrayElementsOwnEscapingValues) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
struct Entry { pub number <int>, pub text <string> }
struct Table { pub entries <[Entry]> }
fun fill(table: &Table, index: int) <noret> {
    table.entries[index] = Entry{number: index, text: "alive"};
}
fun main() <noret> {
    let table <Table> = Table{entries: new [Entry, 4]{}};
    for (let i <int> = 0; i < 4; i++) { fill(&table, i); }
    for (let i <int> = 0; i < 4; i++) {
        blame table.entries[i].number == i;
        blame table.entries[i].text == "alive";
        fill(&table, i);
    }
    delete table.entries;
    printf("array values alive\n");
}
)FIN", "array values alive\n", true);
}

TEST_P(StageValueOwnership, LiteralArrayElementsSurviveTheirFactory) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
struct Entry { pub x <int> }
fun make() <[Entry]> { return [Entry{x: 5}, Entry{x: 9}]; }
fun main() <noret> {
    let entries <[Entry]> = make();
    blame entries[0].x == 5 && entries[1].x == 9;
    entries[0] = Entry{x: 12};
    blame entries[0].x == 12 && entries[1].x == 9;
    delete entries;
    printf("literal array values alive\n");
}
)FIN", "literal array values alive\n", true);
}

TEST_P(StageValueOwnership, ConstructorsReleaseOnlyRepresentationStorage) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
struct Value {
    pub x <int>,
    Value(n: int) { self.x = n; }
}
fun main() <noret> {
    for (let i <int> = 0; i < 32; i++) {
        let value <Value> = Value(i);
        blame value.x == i;
        Value(i);
    }
    printf("constructor storage clean\n");
}
)FIN", "constructor storage clean\n", true);
}

TEST_P(StageValueOwnership, EarlyReturnsAndLoopExitsReleaseStorage) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
struct Value { pub x <int> }
fun choose(n: int) <Value> {
    if (n == 0) { return Value{x: 7}; }
    let value <Value> = Value{x: n};
    for (let i <int> = 0; i < 8; i++) {
        let local <Value> = Value{x: i};
        if (i < 3) { continue; }
        value = local;
        if (i == 4) { break; }
    }
    return value;
}
fun main() <noret> {
    for (let n <int> = 0; n < 32; n++) {
        let value <Value> = choose(n);
        if (n == 0) { blame value.x == 7; }
        else { blame value.x == 4; }
    }
    printf("control flow storage clean\n");
}
)FIN", "control flow storage clean\n", true);
}

TEST_P(StageValueOwnership, AbsentAggregateReturnsStayAbsent) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
struct Value { pub x <int> }
fun? make(present: bool) <Value> {
    if (present) { return Value{x: 7}; }
    return null;
}
fun main() <noret> {
    for (let i <int> = 0; i < 32; i++) {
        let absent? <Value> = make(false);
        blame absent == null;
        let value <Value> = make(true)?;
        blame value.x == 7;
    }
    printf("aggregate absence preserved\n");
}
)FIN", "aggregate absence preserved\n", true);
}

TEST_P(StageValueOwnership, FixedArrayFieldsCopyAndReleaseWithTheirValue) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
struct Box<T> { pub values <[T, 2]> }
struct Plain { pub values <[int, 2]> }
fun make() <Box<int>> { return Box::<int>{values: [3, 7]}; }
fun main() <noret> {
    for (let i <int> = 0; i < 32; i++) {
        let source <Box<int>> = make();
        let copied <Box<int>> = source;
        copied.values[0] = 99;
        blame source.values[0] == 3 && source.values[1] == 7;
        let bytes <Box<char>> = Box::<char>{values: ['x', 'y']};
        let byte_copy <Box<char>> = bytes;
        byte_copy.values[1] = 'z';
        blame bytes.values[1] == 'y';
        let plain <Plain> = Plain{values: [5, 8]};
        let plain_copy <Plain> = plain;
        plain_copy.values[0] = 11;
        blame plain.values[0] == 5 && plain.values[1] == 8;
        let heap <&Plain> = new Plain{};
        blame heap.values[0] == 0 && heap.values[1] == 0;
        delete heap;
    }
    printf("fixed value fields clean\n");
}
)FIN", "fixed value fields clean\n", true);
}

TEST_P(StageValueOwnership, PinnedValueStorageSurvivesItsLexicalScope) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
struct Inner { pub x <int> }
struct Value { pub inner <Inner> }
fun main() <noret> {
    let saved <&Value>;
    {
        #[slaveof($Fin)] let value <Value> = Value{inner: Inner{x: 42}};
        saved = &value;
    }
    let churn <[&Value]> = new [&Value, 64]{};
    for (let i <int> = 0; i < 64; i++) { churn[i] = new Value{inner: Inner{x: i}}; }
    blame saved.inner.x == 42;
    for (let i <int> = 0; i < 64; i++) { delete churn[i]; }
    delete churn;
    printf("pinned value alive\n");
}
)FIN", "pinned value alive\n");
}

TEST_P(StageValueOwnership, DefaultedNestedFieldsOwnTheirStorage) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
struct Inner { pub x <int> }
struct Value { pub inner <Inner> = Inner{x: 7}, pub numbers <[int, 2]> = [3, 8] }
fun make() <Value> { return Value{}; }
fun main() <noret> {
    for (let i <int> = 0; i < 32; i++) {
        let value <Value> = make();
        blame value.inner.x == 7 && value.numbers[1] == 8;
        let copied <Value> = value;
        copied.numbers[0] = 99;
        blame value.numbers[0] == 3;
        let heap <&Value> = new Value{};
        blame heap.inner.x == 7 && heap.numbers[1] == 8;
        heap.numbers = value.numbers;
        value.numbers[1] = 22;
        blame heap.numbers[1] == 8;
        delete heap;
    }
    printf("default field storage clean\n");
}
)FIN", "default field storage clean\n", true, true);
}

TEST_P(StageValueOwnership, FixedArrayStructElementsCopyRecursively) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
struct Inner { pub x <int> }
struct Value { pub entries <[Inner, 2]> }
fun make() <Value> { return Value{entries: [Inner{x: 3}, Inner{x: 7}]}; }
fun main() <noret> {
    for (let i <int> = 0; i < 16; i++) {
        let value <Value> = make();
        let copied <Value> = value;
        copied.entries[0].x = 99;
        blame value.entries[0].x == 3 && value.entries[1].x == 7;
        let empty <&Value> = new Value{};
        blame empty.entries[0].x == 0 && empty.entries[1].x == 0;
        delete empty;
    }
    printf("fixed struct fields copy\n");
}
)FIN", "fixed struct fields copy\n", true, true);
}

TEST_P(StageValueOwnership, DefaultEmptyDynamicArraysAreNullHandles) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
struct Holder { pub values <[int]> }
fun length(values: [int]) <int> { return values.length; }
fun main() <noret> {
    for (let i <int> = 0; i < 32; i++) {
        let empty <[int]>;
        blame empty == null && empty.length == 0;
        blame length(empty) == 0;
        let holder <Holder> = Holder{values: empty};
        blame holder.values == null && holder.values.length == 0;
        delete empty;
        delete holder.values;
        let allocated <[int]> = new [int, 0]{};
        blame allocated.length == 0;
        delete allocated;
    }
    printf("empty handles clean\n");
}
)FIN", "empty handles clean\n", true, true);
}

TEST_P(StageValueOwnership, CollectionLifecycleIsLeakClean) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
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
)FIN", "collection cleanup ok\n", true, true);
}

TEST_P(StageValueOwnership, HashMapLifecycleIsLeakClean) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
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
)FIN", "hashmap cleanup ok\n", true, true);
}

TEST_P(StageValueOwnership, SharedChannelAggregateStorageIsLeakClean) {
#ifndef FIN_TESTS_HAVE_BACKEND
    GTEST_SKIP() << "Executable checks need the LLVM backend";
#endif
    const std::string compiler = valueCompiler(GetParam());
    if (!std::filesystem::exists(compiler)) GTEST_SKIP() << "Compiler missing: " << compiler;
    checkValueProgram(compiler, R"FIN(
import { Channel, Thread } from sync::std;
let shared <&Channel<int>>;
fun produce() <noret> {
    for (let i <int> = 0; i < 257; i++) {
        while (!shared.send(i)) {}
    }
}
fun main() <noret> {
    for (let round <int> = 0; round < 4; round++) {
        let channel <&Channel<int>> = Channel::<int>::shared(4);
        shared = channel;
        let producer <Thread> = Thread::spawn(() <noret> => { produce(); });
        for (let i <int> = 0; i < 257; i++) {
            blame channel.recv().unwrap_or(-1) == i;
        }
        producer.join();
        channel.free_channel();
        delete channel;
    }
    printf("shared cleanup ok\n");
}
)FIN", "shared cleanup ok\n", true);
}
#endif

INSTANTIATE_TEST_SUITE_P(Compilers, StageValueOwnership,
                        ::testing::Values("Reference", "Stage2", "Stage3"));

}
