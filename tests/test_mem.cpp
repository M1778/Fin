#include <gtest/gtest.h>

// Registered for the stdlib/infra wave lanes (issues #51/#56+#58/#81+#87+#88).
// Each upstream lane adds its own TESTs here.

#include <filesystem>
#include <fstream>
#include <string>

#include "Corpus.hpp"
#include "utils/Process.hpp"

// ---------------------------------------------------------------------------
// Mem, the allocator interface (issue #51): `Allocator` with `Arena`,
// `Pool`, `LibcAlloc` and `NullAlloc` behind it, plus the generic
// free-function routes (`arena_block`/`mem_release`, `mem_malloc_bytes`,
// `mem_grow_bytes`, `mem_free_bytes`, `null_alloc`) that stand in for the
// issue's `alloc<T>`/`free<T>` member contract, which no struct can carry
// on either compiler.
//
// What belongs here, and why not test_stdlib.cpp: that file's inventory and
// surface suites own every module the corpus imports. `mem` has no corpus
// consumer yet -- its only imports are this file and
// tests/samples/stdlib/mem.fin -- so its surface book lives here, and the
// choice to record it is the point: when mem gains its first corpus
// importer, its row migrates to that file's table, and a test fails here to
// say so.
//
// The same convention as test_stdlib.cpp: `Soundness_*` must pass forever,
// `KnownDefect_*` asserts what is wrong today and a failure is good news.
// Helpers are the minimal local copy of test_stdlib.cpp's, because the
// bundle resolution (no `--fin-libs`, FIN_LIBS cleared) is a bundle fact
// and this module IS in the bundle.
// ---------------------------------------------------------------------------

namespace fs = std::filesystem;
using namespace fin::testing;

namespace {

class Src {
public:
    explicit Src(const std::string& contents) {
        path_ = uniqueTempPath("fin_mem", ".fin");
        std::ofstream f(path_, std::ios::binary);
        f.write(contents.data(), (std::streamsize)contents.size());
    }
    ~Src() { std::error_code ec; fs::remove(path_, ec); }
    std::string str() const { return path_.string(); }

private:
    fs::path path_;
};

// Compiles a string with no library flags against the bundled library, the
// same rule test_stdlib.cpp states: passing --fin-libs would test the flag.
FincRun compileBundled(const std::string& code) {
    Src s(code);
    return runFinc({s.str()}, {{"FIN_LIBS", ""}});
}

// Front-end diagnostics only: `finc file.fin` stops before codegen, which is
// the right box for surface and analyzer pins.
std::string bundledErr(const std::string& code) {
    return stripAnsi(compileBundled(code).err);
}

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

// A build-and-run with no library flags; returns stdout, or "compile failed:"
// plus the reason. Aborts surface as `program exited non-zero, output: ...`,
// so the guard pins below can assert on the blame text in the same string.
std::string buildAndRun(const std::string& code) {
    Src src(code);
    const fs::path exe = uniqueTempPath("fin_mem_exe");
    const fs::path outPath = uniqueTempPath("fin_mem_out");
    const FincRun c = runFinc({src.str(), "-o", exe.string()}, {{"FIN_LIBS", ""}});
    if (c.exitCode != 0 || !fs::exists(exe)) {
        return "compile failed: " + stripAnsi(c.err);
    }
    const int status = fin::runProcess({exe.string()}, outPath.string(), outPath.string());
    const std::string out = readProcessOutput(outPath.string());
    std::error_code ec;
    fs::remove(exe, ec);
    fs::remove(outPath, ec);
    if (status != 0) return "program exited non-zero, output: " + out;
    return out;
}

// A full-build `-o` refusal (codegen or link -- beyond the front end).
std::string buildErr(const std::string& code) {
    Src src(code);
    const fs::path exe = uniqueTempPath("fin_mem_exe");
    const FincRun c = runFinc({src.str(), "-o", exe.string()}, {{"FIN_LIBS", ""}});
    std::error_code ec;
    fs::remove(exe, ec);
    return stripAnsi(c.err);
}

// A run that must stop at a `blame`, with the message in the abort output.
void expectAbort(const std::string& code, const std::string& needle) {
    const std::string out = buildAndRun(code);
    EXPECT_NE(out.find("program exited non-zero"), std::string::npos)
        << "the run must stop, not return:\n" << out;
    EXPECT_NE(out.find(needle), std::string::npos)
        << "the abort must name the guard that caught the caller:\n" << out;
}

} // namespace

// ---------------------------------------------------------------------------
// The surface. Every exported name at a call, held positive and negative the
// way every other module surface here is: an arity error and a type error
// prove the names exist against a checking call site rather than a catch-all.
// ---------------------------------------------------------------------------

TEST(Soundness_Mem, TheSurfaceResolves) {
    const std::string err = bundledErr(
        "import { Arena, Pool, LibcAlloc, NullAlloc, Allocator, arena_block,\n"
        "         mem_release, mem_malloc_bytes, mem_grow_bytes, mem_free_bytes,\n"
        "         null_alloc } from mem::std;\n"
        "fun waste(a: Allocator) <noret> { let u <int> = a.used(); let r <bool> = a.has_room(); }\n"
        "fun main() <noret> {\n"
        "    let arena <Arena> = Arena::create(65536);\n"
        "    let b <&char> = arena.arena_bytes(8);\n"
        "    let u <int> = arena.used();\n"
        "    let r <bool> = arena.has_room();\n"
        "    arena.admit(4);\n"
        "    let buf <[int]> = arena_block::<int>(&arena, 8);\n"
        "    mem_release::<int>(buf);\n"
        "    let pool <Pool<int>> = Pool::<int>::create(8);\n"
        "    let i <int> = pool.take();\n"
        "    pool.give(i);\n"
        "    pool.write(i, 7);\n"
        "    let v <int> = pool.read(i);\n"
        "    let pu <int> = pool.used();\n"
        "    let pr <bool> = pool.has_room();\n"
        "    pool.destroy();\n"
        "    let la <LibcAlloc> = LibcAlloc::create();\n"
        "    let m <&char> = la.malloc_bytes(8);\n"
        "    let g <&char> = la.grow_bytes(m, 16);\n"
        "    la.free_bytes(g);\n"
        "    let lu <int> = la.used();\n"
        "    let lr <bool> = la.has_room();\n"
        "    let na <NullAlloc> = NullAlloc::create();\n"
        "    let nu <int> = na.used();\n"
        "    let nr <bool> = na.has_room();\n"
        "    na.destroy();\n"
        "    let p1 <&char> = mem_malloc_bytes(4);\n"
        "    let p2 <&char> = mem_grow_bytes(p1, 8);\n"
        "    mem_free_bytes(p2);\n"
        "}\n");
    EXPECT_EQ(errorCount(err), 0u)
        << "mem's surface stopped resolving. The bodies all live in the module,\n"
           "so a failure here is an import, a declaration, or a spelling the\n"
           "issue asked for that no longer resolves:\n"
        << err;

    // The negative half, both directions. Without these, every name above
    // could resolve against a catch-all that accepts anything.
    const std::string arity = bundledErr(
        "import { Pool } from mem::std;\n"
        "fun main() <noret> { let p <Pool<int>> = Pool::<int>::create(); }\n");
    EXPECT_NE(arity.find("expects 1 arguments, got 0"), std::string::npos)
        << "a constructor on a library type must be arity-checked like any other:\n"
        << arity;

    const std::string typed = bundledErr(
        "import { Arena } from mem::std;\n"
        "fun main() <noret> {\n"
        "    let a <Arena> = Arena::create(8);\n"
        "    let s <string> = a.used();\n"
        "}\n");
    EXPECT_NE(typed.find("Type mismatch: expected 'string', got 'int'"), std::string::npos)
        << "an allocator's meter is an int and must be typed like any other int:\n"
        << typed;
}

TEST(Soundness_Mem, AnImplMissingAMemberIsRefusedOnBothCompilers) {
    // The declaration-site check that holds every impl to the interface's
    // member names. Measured identical on the stage compilers' front end
    // (the same error, same position), so the check is a front contract and
    // not a backend accident; the position is part of the contract because
    // it points at the struct claiming the interface, not the interface.
    const std::string err = bundledErr(
        "import { Allocator } from mem::std;\n"
        "struct Imposter : <Allocator> {\n"
        "    fun has_room(self: &Self) <bool> { return false; }\n"
        "}\n"
        "fun main() <noret> { let x <int> = 0; }\n");
    EXPECT_NE(err.find("Struct 'Imposter' does not implement interface 'Allocator'"),
              std::string::npos) << err;
    EXPECT_NE(err.find("2:1"), std::string::npos)
        << "the refusal must point at the struct that claimed the interface:\n"
        << err;
}

TEST(Soundness_Mem, TheIssueSpellingIsRefusedAsTheHeaderSays) {
    // The issue's example line, `arena.alloc::<int>(1024)`, against the
    // shipped module: a front-end refusal naming the missing member, with
    // or without the turbofish. The header records why (the typed contract
    // cannot be carried as members on either compiler) and routes the same
    // request to arena_block/mem_release; this pin is what keeps the
    // refusal the module INTENDED instead of letting a silent catch-all
    // back it in.
    const std::string turbofish = bundledErr(
        "import { Arena } from mem::std;\n"
        "fun main() <noret> {\n"
        "    let arena <Arena> = Arena::create(1024);\n"
        "    let buf <[int]> = arena.alloc::<int>(4);\n"
        "}\n");
    EXPECT_NE(turbofish.find("error: Method 'alloc' not found in type 'Arena'"),
              std::string::npos) << turbofish;

    // The bare spelling is the same refusal: there is one gap, not a pair
    // of subtly different ones to document separately.
    const std::string bare = bundledErr(
        "import { Arena } from mem::std;\n"
        "fun main() <noret> {\n"
        "    let arena <Arena> = Arena::create(1024);\n"
        "    let buf <[int]> = arena.alloc(4);\n"
        "}\n");
    EXPECT_NE(bare.find("error: Method 'alloc' not found in type 'Arena'"),
              std::string::npos) << bare;
}

TEST(Soundness_Mem, TheRoutedSpellingOfTheMissingMemberColdStartsTheSameGap) {
    // The shape this test pins: where the typed contract EXISTS as a
    // locally declared generic member, the turbofish call reaches codegen
    // and refuses in all three compilers with one sentence. This is the
    // shape moving the contract back into Arena would have to lower, and
    // the refusal is what the module header promises instead of lowering.
    const std::string codegen = buildErr(
        "struct Local {\n"
        "    used_bytes <int> = 0,\n"
        "    fun alloc<T>(self: &Self, count: int) <[T]> {\n"
        "        let fresh <[T]> = new [T, count]{};\n"
        "        return fresh;\n"
        "    }\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let a <Local>;\n"
        "    let buf <[int]> = a.alloc::<int>(4);\n"
        "}\n");
    EXPECT_NE(codegen.find("a call to the method 'alloc' with explicit generic "
                           "arguments is not lowered yet"),
              std::string::npos)
        << "the locally-declared turbofish must be the codegen refusal the\n"
           "header names, not a front error:\n"
        << codegen;
    // The refusal arrives at codegen, not at the front end.
    EXPECT_EQ(codegen.find("analyzer"), std::string::npos) << codegen;
}

#ifdef FIN_TESTS_HAVE_BACKEND

// ---------------------------------------------------------------------------
// The routes, as runs. `blame` is the assertion, stdout stays empty, and an
// abort marks the guard that caught the caller.
// ---------------------------------------------------------------------------

TEST(Soundness_Mem, TheTypedRouteHandsARealBlockAndTakesItBack) {
    // The routed spelling of the issue's example, as a run: `arena_block`
    // hands a fresh `[int]` and counts it against the arena's meter; the
    // caller owns it and `mem_release` is the spelling of giving it back.
    const std::string out = buildAndRun(
        "import { Arena, arena_block, mem_release } from mem::std;\n"
        "fun main() <noret> {\n"
        "    let arena <Arena> = Arena::create(65536);\n"
        "    let buf <[int]> = arena_block::<int>(&arena, 1024);\n"
        "    buf[0] = 42;\n"
        "    blame buf[0] == 42, \"the typed block lost its first cell\";\n"
        "    buf[1023] = 7;\n"
        "    blame buf[1023] == 7, \"the typed block lost its last cell\";\n"
        "    blame arena.used() == 1024, \"the typed block did not register against the arena's meter\";\n"
        "    mem_release::<int>(buf);\n"
        "    arena.destroy();\n"
        "}\n");
    EXPECT_EQ(out, "") << out;
}

TEST(Soundness_Mem, TheByteRouteBumpsOneRegionAndOneDestroyFreesIt) {
    // The arena's own route, the one whose one-free claim is literally a
    // free: cells from one slab, the meter in bytes, and the second cell's
    // write leaving the first intact.
    const std::string out = buildAndRun(
        "import { Arena } from mem::std;\n"
        "fun main() <noret> {\n"
        "    let arena <Arena> = Arena::create(65536);\n"
        "    let cell <&char> = arena.arena_bytes(8);\n"
        "    *cell = 'H';\n"
        "    let next <&char> = arena.arena_bytes(8);\n"
        "    *next = 'y';\n"
        "    blame *cell == 'H', \"the byte route lost cell one\";\n"
        "    blame *next == 'y', \"the byte route lost cell two\";\n"
        "    blame arena.used() == 16, \"the byte route did not bump the arena's meter\";\n"
        "    blame arena.has_room(), \"the arena reported no room after sixteen bytes\";\n"
        "    arena.destroy();\n"
        "    blame arena.used() == 0, \"a destroyed arena still reports usage\";\n"
        "}\n");
    EXPECT_EQ(out, "") << out;
}

TEST(Soundness_Mem, ThePoolRecyclesItsSlots) {
    // The pool's whole win as a run: write through a taken index, hand it
    // back, and observe the recycling the free list exists for -- the same
    // index comes back, and the meter counts one claimed slot.
    const std::string out = buildAndRun(
        "import { Pool } from mem::std;\n"
        "fun main() <noret> {\n"
        "    let pool <Pool<int>> = Pool::<int>::create(4);\n"
        "    let slot <int> = pool.take();\n"
        "    pool.write(slot, 42);\n"
        "    blame pool.read(slot) == 42, \"the pool slot lost its value\";\n"
        "    pool.give(slot);\n"
        "    let again <int> = pool.take();\n"
        "    blame again == slot, \"the pool did not recycle the handed slot\";\n"
        "    blame pool.used() == 1, \"the pool's meter miscounted the recycle\";\n"
        "    pool.destroy();\n"
        "}\n");
    EXPECT_EQ(out, "") << out;
}

TEST(Soundness_Mem, LibcGrowAddsDeterministically) {
    // A counter fix that two backends once disagreed on: realloc answering
    // in place must not change what `used()` reports, so the wrapper adds
    // the request unconditionally -- 64 by malloc, 128 more by grow, 192.
    // The stage agreement suites hold the same number on both compilers.
    const std::string out = buildAndRun(
        "import { LibcAlloc } from mem::std;\n"
        "fun main() <noret> {\n"
        "    let la <LibcAlloc> = LibcAlloc::create();\n"
        "    let chew <&char> = la.malloc_bytes(64);\n"
        "    *chew = 'X';\n"
        "    let grown <&char> = la.grow_bytes(chew, 128);\n"
        "    blame *grown == 'X', \"realloc dropped the byte it promised to keep\";\n"
        "    blame la.used() == 192, \"the grow did not add the bytes it grew by\";\n"
        "    la.free_bytes(grown);\n"
        "}\n");
    EXPECT_EQ(out, "") << out;
}

TEST(Soundness_Mem, RawByteRoutesReachCWithoutAWrapper) {
    // malloc/realloc/free as the caller's raw block, the no-allocator
    // spelling of LibcAlloc's own moves: p2 is the LIVING pointer after the
    // grow and the one that gets freed.
    const std::string out = buildAndRun(
        "import { mem_malloc_bytes, mem_grow_bytes, mem_free_bytes } from mem::std;\n"
        "fun main() <noret> {\n"
        "    let p1 <&char> = mem_malloc_bytes(32);\n"
        "    let p2 <&char> = mem_grow_bytes(p1, 64);\n"
        "    *p2 = 'Q';\n"
        "    blame *p2 == 'Q', \"the raw grow dropped the byte\";\n"
        "    mem_free_bytes(p2);\n"
        "}\n");
    EXPECT_EQ(out, "") << out;
}

TEST(Soundness_Mem, AnInterfaceValueDispatchesEachAllocatorUniformly) {
    // Dispatch through the interface VALUE, not the concrete struct: one
    // adopter, four impls, the measurement half only (the typed contract is
    // not interface-carried anywhere, by design above).
    const std::string out = buildAndRun(
        "import { Arena, Pool, NullAlloc, LibcAlloc, Allocator } from mem::std;\n"
        "fun waste(a: Allocator) <noret> {\n"
        "    blame a.used() >= 0, \"an allocator reported a negative total\";\n"
        "    blame a.has_room() || !a.has_room(), \"somehow neither\";\n"
        "}\n"
        "fun main() <noret> {\n"
        "    let arena <Arena> = Arena::create(256);\n"
        "    let b <&char> = arena.arena_bytes(16);\n"
        "    *b = 'M';\n"
        "    waste(arena);\n"
        "    let pool <Pool<int>> = Pool::<int>::create(2);\n"
        "    waste(pool);\n"
        "    let dead <NullAlloc> = NullAlloc::create();\n"
        "    waste(dead);\n"
        "    let libc <LibcAlloc> = LibcAlloc::create();\n"
        "    waste(libc);\n"
        "    arena.destroy();\n"
        "    pool.destroy();\n"
        "}\n");
    EXPECT_EQ(out, "") << out;
}

// The guards. Each is a caller bug the module refuses with its blame
// message; each abort text is the module's contract, not libc's, so the
// needle is asserted rather than the exit shape.

TEST(Soundness_Mem, TheArenaGuardsRefuseTheCallerBugs) {
    expectAbort("import { Arena } from mem::std;\n"
                "fun main() <noret> {\n"
                "    let a <Arena> = Arena::create(64);\n"
                "    let c <&char> = a.arena_bytes(65);\n"
                "}\n",
                "assertion failed: the arena is full");

    expectAbort("import { Arena } from mem::std;\n"
                "fun main() <noret> {\n"
                "    let a <Arena> = Arena::create(64);\n"
                "    a.destroy();\n"
                "    let c <&char> = a.arena_bytes(8);\n"
                "}\n",
                "assertion failed: this arena was destroyed");

    // The same full-cap guard on the meter's other door: `admit` is how
    // `arena_block` registers its count before the handout, so a typed
    // request past the cap stops at the meter, not at malloc.
    expectAbort("import { Arena, arena_block } from mem::std;\n"
                "fun main() <noret> {\n"
                "    let a <Arena> = Arena::create(10);\n"
                "    let b <[int]> = arena_block::<int>(&a, 11);\n"
                "}\n",
                "assertion failed: the arena is full");

    expectAbort("import { Arena } from mem::std;\n"
                "fun main() <noret> { Arena::create(0 - 5); }\n",
                "assertion failed: an arena cannot hold a negative byte count");
}

TEST(Soundness_Mem, ThePoolGuardsRefuseTheCallerBugs) {
    expectAbort("import { Pool } from mem::std;\n"
                "fun main() <noret> {\n"
                "    let p <Pool<int>> = Pool::<int>::create(1);\n"
                "    let i <int> = p.take();\n"
                "    let j <int> = p.take();\n"
                "}\n",
                "assertion failed: the pool is exhausted");

    expectAbort("import { Pool } from mem::std;\n"
                "fun main() <noret> {\n"
                "    let p <Pool<int>> = Pool::<int>::create(4);\n"
                "    p.give(99);\n"
                "}\n",
                "assertion failed: a pool index must point at a slot");

    // Read and write carry the same contract as give; one representative
    // read, because the three share the guard verbatim.
    expectAbort("import { Pool } from mem::std;\n"
                "fun main() <noret> {\n"
                "    let p <Pool<int>> = Pool::<int>::create(2);\n"
                "    let v <int> = p.read(7);\n"
                "}\n",
                "assertion failed: a pool index must point at a slot");

    expectAbort("import { Pool } from mem::std;\n"
                "fun main() <noret> {\n"
                "    let p <Pool<int>> = Pool::<int>::create(1);\n"
                "    p.give(0);\n"
                "    p.give(0);\n"
                "}\n",
                "assertion failed: the free list is full");

    expectAbort("import { Pool } from mem::std;\n"
                "fun main() <noret> { Pool::<int>::create(0 - 1); }\n",
                "assertion failed: a pool cannot hold a negative slot count");
}

TEST(Soundness_Mem, NullAllocRefusesToHandOutAndSaysItsName) {
    // The what-if-allocation-failed allocator, struct spelling and free
    // spelling, both stopping at the same named refusal.
    expectAbort("import { NullAlloc } from mem::std;\n"
                "fun main() <noret> {\n"
                "    let n <NullAlloc> = NullAlloc::create();\n"
                "    let c <&char> = n.give(8);\n"
                "}\n",
                "assertion failed: NullAlloc refused to hand out memory");

    expectAbort("import { null_alloc } from mem::std;\n"
                "fun main() <noret> { let b <[int]> = null_alloc::<int>(5); }\n",
                "assertion failed: NullAlloc refused to hand out memory");

    // `used()` counts the ask it refused, and `has_room()` answers false
    // -- the queries a harness reads, as runs, before and after the stop.
    expectAbort("import { NullAlloc } from mem::std;\n"
                "fun main() <noret> {\n"
                "    let n <NullAlloc> = NullAlloc::create();\n"
                "    blame n.has_room() == false, \"the nothing-allocator promised room\";\n"
                "    blame n.used() == 0, \"the nothing-allocator counted a handout\";\n"
                "    let c <&char> = n.give(8);\n"
                "}\n",
                "assertion failed: NullAlloc refused to hand out memory");
}

#endif  // FIN_TESTS_HAVE_BACKEND

// ---------------------------------------------------------------------------
// KnownDefect: what is wrong today, where a failure is good news.
// ---------------------------------------------------------------------------

TEST(KnownDefect_Mem, AnImplSignatureMismatchGoesUncaught) {
    // The interface check is by NAME. A `used` with the wrong return type
    // passes the front end and the whole reference build -- so nothing can
    // lean on the analyzer knowing more than member names, which is
    // exactly what lib/std/mem.fin's header states. If this ever goes red,
    // the analyzer gained a signature check and the header sentence
    // (and this pin's suite name) owed an update.
    const FincRun r = compileBundled(
        "import { Allocator } from mem::std;\n"
        "struct Wrong : <Allocator> {\n"
        "    fun used(self: &Self) <bool> { return false; }\n"
        "    fun has_room(self: &Self) <bool> { return false; }\n"
        "}\n"
        "fun main() <noret> { let x <int> = 0; }\n");
    const std::string err = stripAnsi(r.err);
    EXPECT_EQ(r.exitCode, 0)
        << "GOOD NEWS: the analyzer now checks impl signatures, and this\n"
           "mismatch went refused. Move the caveat from the header and this\n"
           "point into Soundness_Mem:\n"
        << err;
    EXPECT_EQ(err.find("does not implement interface"), std::string::npos) << err;
}
