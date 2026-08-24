#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "Pipeline.hpp"
#include "semantics/SemanticAnalyzer.hpp"
#include "types/ArrayType.hpp"
#include "types/DynamicType.hpp"
#include "types/FunctionType.hpp"
#include "types/GenericType.hpp"
#include "types/Layout.hpp"
#include "types/NullableType.hpp"
#include "types/PointerType.hpp"
#include "types/PrimitiveType.hpp"
#include "types/StructType.hpp"

// Wave 4 step 6: the layout pass.
//
// docs/compiler-api.md calls this "the one thing to build first if only one thing
// gets built", and the reason is ADR 0003: Fin commits to a tracing collector
// written in Fin, a tracing collector needs the byte offsets of the words that
// hold pointers, and before this unit there were *zero* occurrences of `offset`,
// `size` or `align` anywhere in src/types/ or src/semantics/. Not partial --
// absent. Ordered fields (Soundness_FieldOrder) was its only blocker.
//
// What this suite is for, and why it is a suite of its own rather than more of
// test_soundness.cpp: a layout is a number, and a wrong number is the one kind of
// compiler bug that produces a program which runs, produces answers, and is
// wrong. Nothing downstream can catch it -- codegen will believe whatever offset
// it is handed, and so will a collector. So every number here is asserted
// literally, against the x86-64 / AArch64 LP64 rules the emitted object files
// actually use, rather than against a formula this suite re-derives (a suite that
// recomputes `roundUp` agrees with any bug in `roundUp`).
//
// The other half is refusal, and it is the larger half. Most of Fin has no
// decided representation yet: an interface-typed value, an enum's tag, `any`, a
// dynamic array, a closure. For each of those a layout pass has exactly two
// honest options -- refuse and name what is missing, or invent a number. §1.3 of
// docs/compiler-api.md is about what happens when a compiler takes the second
// option: D's `getPointerBitmap` returns an all-zero bitmap for interfaces, which
// is not an error, is not a refusal, and is silently a lie. Every refusal below
// exists so that Fin's answer to the same question is a diagnostic.
//
// Suite convention as documented in test_soundness.cpp: Soundness_* must always
// pass; KnownDefect_* asserts what is wrong today and a failure there is good
// news -- invert and rename it, never relax it.

using namespace fin;

namespace {

// The semantic type a source declaration produces, or null. The analyzer is run
// because a StructType built by hand cannot check that *the front end* fills in
// the fields the layout pass then reads -- which is the half of this that
// Soundness_FieldOrder guards and that layout depends on.
TypePtr typeFromSource(const std::string& source, const std::string& name) {
    static std::vector<std::unique_ptr<fin::Program>> keepAlive;
    static std::vector<std::unique_ptr<fin::SemanticAnalyzer>> analyzers;
    static std::vector<std::unique_ptr<fin::DiagnosticEngine>> engines;
    engines.push_back(std::make_unique<fin::DiagnosticEngine>("", "<test>"));
    auto& diag = *engines.back();
    diag.setColorMode(fin::ColorMode::Never);
    auto parsed = fin::testing::parseSource(source, diag);
    if (!parsed.parsed) return nullptr;
    analyzers.push_back(std::make_unique<fin::SemanticAnalyzer>(diag, false));
    auto& analyzer = *analyzers.back();
    analyzer.visit(*parsed.ast);
    keepAlive.push_back(std::move(parsed.ast));
    return analyzer.getGlobalScope()->resolveType(name);
}

TypePtr prim(const std::string& n) { return std::make_shared<PrimitiveType>(n); }

// A layout that must exist. Fails the test with the refusal rather than
// dereferencing a type that has none.
TypeLayout must(const LayoutResult& r) {
    EXPECT_TRUE(r.ok()) << "refused: " << r.refusal;
    return r.layout;
}

}  // namespace

// --- scalars ---------------------------------------------------------------
//
// These numbers are not a preference. They are the widths src/codegen already
// emits -- `int` is i32 because the corpus writes `printf("%d", n)` -- and the
// table they come from is now shared with the backend rather than duplicated,
// which is the whole point of `scalarByName` being in src/types/ and not in
// CodeGen_LLVM.cpp. Two tables that agree today are two tables that disagree
// after the first edit, and the disagreement is an ABI split.

TEST(Soundness_Layout, EveryScalarHasTheWidthTheBackendEmits) {
    LayoutEngine e;
    struct Case { const char* name; uint64_t size; uint64_t align; };
    const Case cases[] = {
        {"bool", 1, 1},   {"char", 1, 1},    {"short", 2, 2},  {"ushort", 2, 2},
        {"int", 4, 4},    {"uint", 4, 4},    {"long", 8, 8},   {"ulong", 8, 8},
        {"float", 4, 4},  {"double", 8, 8},
        // A Fin `string` is a pointer to NUL-terminated bytes, which is what makes
        // the corpus's `printf("%s", s)` work. A length-carrying string is a
        // library decision (ADR 0003) and a different size.
        {"string", 8, 8},
    };
    for (const auto& c : cases) {
        auto layout = must(e.layoutOf(prim(c.name)));
        EXPECT_EQ(layout.size, c.size) << c.name;
        EXPECT_EQ(layout.align, c.align) << c.name;
        EXPECT_TRUE(layout.fields.empty()) << c.name;
        EXPECT_TRUE(layout.pointers.empty()) << c.name;
    }
}

TEST(Soundness_Layout, APointerIsPointerSizedWhateverItPointsAt) {
    LayoutEngine e;
    // Including a pointee that has no layout of its own. This is not an
    // optimisation: laying out `&T` by laying out `T` is how `struct Node { next
    // <&Node> }` becomes an infinite recursion, so the independence is load-bearing.
    auto opaque = std::make_shared<GenericType>("T");
    for (const TypePtr& pointee : {prim("int"), prim("string"), TypePtr(opaque)}) {
        auto layout = must(e.layoutOf(std::make_shared<PointerType>(pointee)));
        EXPECT_EQ(layout.size, 8u);
        EXPECT_EQ(layout.align, 8u);
    }
}

TEST(Soundness_Layout, APointerToAValueTypeIsOneTracedSlot) {
    LayoutEngine e;
    auto layout = must(e.layoutOf(std::make_shared<PointerType>(prim("int"))));
    ASSERT_EQ(layout.pointers.size(), 1u);
    EXPECT_EQ(layout.pointers[0].offset, 0u);
    ASSERT_TRUE(layout.pointers[0].pointee != nullptr);
    EXPECT_EQ(layout.pointers[0].pointee->toString(), "int");
}

TEST(Soundness_Layout, AStringIsPointerSizedAndIsNotATracedSlot) {
    // The one place where "holds a pointer" and "is in the pointer map" come
    // apart, so it is asserted rather than left to be inferred.
    //
    // A `string` today points at a NUL-terminated blob in .rodata that no
    // allocator owns and no collector may follow: a precise collector that traced
    // it would read an object header that is not there. It is still pointer-sized,
    // because that is what the backend emits for it.
    //
    // Owed ruling: when a string becomes a library value with a heap buffer
    // (ADR 0003), this entry moves into the map and this test inverts.
    LayoutEngine e;
    auto layout = must(e.layoutOf(prim("string")));
    EXPECT_EQ(layout.size, 8u);
    EXPECT_TRUE(layout.pointers.empty());
}

// --- structs ---------------------------------------------------------------

TEST(Soundness_Layout, FieldsAreLaidOutInDeclarationOrderWithPadding) {
    auto t = typeFromSource("struct S { pub a <char>, pub b <int>, pub c <char>, }\n", "S");
    ASSERT_TRUE(t != nullptr);
    LayoutEngine e;
    auto layout = must(e.layoutOf(t));
    ASSERT_EQ(layout.fields.size(), 3u);
    EXPECT_EQ(layout.fields[0].name, "a");
    EXPECT_EQ(layout.fields[0].offset, 0u);
    EXPECT_EQ(layout.fields[1].name, "b");
    EXPECT_EQ(layout.fields[1].offset, 4u) << "b must be padded to its own alignment";
    EXPECT_EQ(layout.fields[2].name, "c");
    EXPECT_EQ(layout.fields[2].offset, 8u);
    EXPECT_EQ(layout.align, 4u) << "a struct's alignment is its widest member's";
    EXPECT_EQ(layout.size, 12u) << "and its size is rounded up to that alignment";
}

TEST(Soundness_Layout, ReorderingTheFieldsChangesTheSize) {
    // The test that says why step 5 had to come first. Same three fields, same
    // types, two declaration orders, two different sizes -- so a layout pass
    // reading an unordered container could not have been right, only lucky.
    LayoutEngine e;
    auto loose = typeFromSource("struct S { pub a <char>, pub b <long>, pub c <char>, }\n", "S");
    auto tight = typeFromSource("struct S { pub a <char>, pub c <char>, pub b <long>, }\n", "S");
    ASSERT_TRUE(loose && tight);
    EXPECT_EQ(must(e.layoutOf(loose)).size, 24u);
    EXPECT_EQ(must(e.layoutOf(tight)).size, 16u);
}

TEST(Soundness_Layout, AnEmptyStructHasNoBytes) {
    // Ruled here, and the reason is recorded rather than borrowed: C++ gives an
    // empty class one byte so that two objects have distinct addresses, and Fin
    // has no rule that requires that of a struct. LLVM's `{}` is zero-sized, the
    // backend is what has to agree with this number, so zero it is -- a pad byte
    // would be a byte this compiler invented.
    auto t = typeFromSource("struct S { }\n", "S");
    ASSERT_TRUE(t != nullptr);
    LayoutEngine e;
    auto layout = must(e.layoutOf(t));
    EXPECT_EQ(layout.size, 0u);
    EXPECT_EQ(layout.align, 1u);
}

TEST(Soundness_Layout, ANestedStructIsEmbeddedByValueAtItsOwnAlignment) {
    auto t = typeFromSource(
        "struct Inner { pub a <int>, pub b <int>, }\n"
        "struct Outer { pub tag <char>, pub inner <Inner>, pub tail <char>, }\n",
        "Outer");
    ASSERT_TRUE(t != nullptr);
    LayoutEngine e;
    auto layout = must(e.layoutOf(t));
    ASSERT_EQ(layout.fields.size(), 3u);
    EXPECT_EQ(layout.fields[0].offset, 0u);
    EXPECT_EQ(layout.fields[1].offset, 4u);
    EXPECT_EQ(layout.fields[1].size, 8u) << "the whole inner struct, not a pointer to it";
    EXPECT_EQ(layout.fields[2].offset, 12u);
    EXPECT_EQ(layout.size, 16u);
    EXPECT_EQ(layout.align, 4u);
}

TEST(Soundness_Layout, AnInheritedFieldComesBeforeTheOnesDeclaredHere) {
    // Base-first, which is single inheritance's whole ABI trick: a pointer to the
    // derived type is already a pointer to the base, so an upcast emits nothing.
    // Any other order would make it emit an addition.
    auto t = typeFromSource(
        "struct Base { pub a <int>, }\n"
        "struct Derived : <Base> { pub b <int>, }\n",
        "Derived");
    ASSERT_TRUE(t != nullptr);
    LayoutEngine e;
    auto layout = must(e.layoutOf(t));
    ASSERT_EQ(layout.fields.size(), 2u);
    EXPECT_EQ(layout.fields[0].name, "a");
    EXPECT_EQ(layout.fields[0].offset, 0u);
    EXPECT_TRUE(layout.fields[0].inherited);
    EXPECT_EQ(layout.fields[1].name, "b");
    EXPECT_EQ(layout.fields[1].offset, 4u);
    EXPECT_FALSE(layout.fields[1].inherited);
    EXPECT_EQ(layout.size, 8u);
}

TEST(Soundness_Layout, AnImplementedInterfaceContributesNoBytes) {
    // `struct S : <I>` puts an interface in `parents` beside any base struct, so
    // the parent walk has to tell them apart. An interface that contributed a slot
    // would move every field after it for no value that exists at runtime.
    auto t = typeFromSource(
        "interface I { pub fun f() <int>; }\n"
        "struct S : <I> { pub a <int>, }\n",
        "S");
    ASSERT_TRUE(t != nullptr);
    LayoutEngine e;
    auto layout = must(e.layoutOf(t));
    ASSERT_EQ(layout.fields.size(), 1u);
    EXPECT_EQ(layout.fields[0].offset, 0u);
    EXPECT_EQ(layout.size, 4u);
}

TEST(Soundness_Layout, AFieldIsFoundByName) {
    auto t = typeFromSource("struct S { pub a <char>, pub b <long>, }\n", "S");
    ASSERT_TRUE(t != nullptr);
    LayoutEngine e;
    auto layout = must(e.layoutOf(t));
    const FieldLayout* b = layout.field("b");
    ASSERT_TRUE(b != nullptr);
    EXPECT_EQ(b->offset, 8u);
    EXPECT_EQ(layout.field("nope"), nullptr);
}

// --- recursion -------------------------------------------------------------

TEST(Soundness_Layout, AStructThatContainsItselfByValueIsRefused) {
    // Built by hand, deliberately. Whether the *front end* accepts
    // `struct S { s <S>, }` is a separate question with its own answer; this test
    // is about the layout pass not recursing forever if it is ever handed one, and
    // a hand-built type is the only way to ask that question and nothing else.
    auto s = std::make_shared<StructType>("S");
    s->defineField("v", prim("int"), true);
    s->defineField("self", s, true);
    LayoutEngine e;
    auto r = e.layoutOf(s);
    EXPECT_FALSE(r.ok());
    EXPECT_NE(r.refusal.find("S"), std::string::npos) << r.refusal;
    EXPECT_NE(r.refusal.find("self"), std::string::npos)
        << "the refusal must name the field that closes the cycle: " << r.refusal;
}

TEST(Soundness_Layout, MutualContainmentByValueIsRefused) {
    auto a = std::make_shared<StructType>("A");
    auto b = std::make_shared<StructType>("B");
    a->defineField("b", b, true);
    b->defineField("a", a, true);
    LayoutEngine e;
    EXPECT_FALSE(e.layoutOf(a).ok());
    EXPECT_FALSE(e.layoutOf(b).ok());
}

TEST(Soundness_Layout, APointerBreaksTheCycleAndIsInTheMap) {
    auto t = typeFromSource("struct Node { pub v <int>, pub next <&Node>, }\n", "Node");
    ASSERT_TRUE(t != nullptr);
    LayoutEngine e;
    auto layout = must(e.layoutOf(t));
    ASSERT_EQ(layout.fields.size(), 2u);
    EXPECT_EQ(layout.fields[1].offset, 8u) << "padded to the pointer's alignment";
    EXPECT_EQ(layout.size, 16u);
    EXPECT_EQ(layout.align, 8u);
    ASSERT_EQ(layout.pointers.size(), 1u);
    EXPECT_EQ(layout.pointers[0].offset, 8u);
    // The surpassing bit, per docs/compiler-api.md §1.9: D's getPointerBitmap says
    // "a pointer lives here" and stops. Knowing it is a pointer *to Node* is the
    // difference between a precise collector and a conservative one.
    ASSERT_TRUE(layout.pointers[0].pointee != nullptr);
    EXPECT_EQ(layout.pointers[0].pointee->toString(), "Node");
}

TEST(Soundness_Layout, ANestedStructsPointersAreFlattenedIntoTheOuterMap) {
    // What a collector needs is one flat list of word offsets for the object it is
    // looking at, not a tree it has to walk at run time. Nesting is resolved here,
    // at compile time, exactly once per type.
    auto t = typeFromSource(
        "struct Node { pub v <int>, }\n"
        "struct Pair { pub l <&Node>, pub r <&Node>, }\n"
        "struct Holder { pub tag <int>, pub pair <Pair>, pub extra <&Node>, }\n",
        "Holder");
    ASSERT_TRUE(t != nullptr);
    LayoutEngine e;
    auto layout = must(e.layoutOf(t));
    EXPECT_EQ(layout.size, 32u);
    ASSERT_EQ(layout.pointers.size(), 3u);
    EXPECT_EQ(layout.pointers[0].offset, 8u);
    EXPECT_EQ(layout.pointers[1].offset, 16u);
    EXPECT_EQ(layout.pointers[2].offset, 24u);
    for (const auto& p : layout.pointers) {
        ASSERT_TRUE(p.pointee != nullptr);
        EXPECT_EQ(p.pointee->toString(), "Node");
    }
}

TEST(Soundness_Layout, APointerFreeTypeAnswersInOneCall) {
    // `pointer_count` returning zero is how a handler decides "this type needs no
    // tracing at all" without walking anything (docs/compiler-api.md §3.6).
    auto t = typeFromSource("struct Flat { pub a <int>, pub b <double>, }\n", "Flat");
    ASSERT_TRUE(t != nullptr);
    LayoutEngine e;
    EXPECT_TRUE(must(e.layoutOf(t)).pointers.empty());
}

// --- generics --------------------------------------------------------------

TEST(Soundness_Layout, AGenericParameterHasNoLayoutUntilItIsSubstituted) {
    LayoutEngine e;
    auto r = e.layoutOf(std::make_shared<GenericType>("T"));
    EXPECT_FALSE(r.ok());
    EXPECT_NE(r.refusal.find("T"), std::string::npos) << r.refusal;
}

TEST(Soundness_Layout, AnUninstantiatedGenericStructIsRefusedAndAnInstantiatedOneIsNot) {
    // The ABI reason this pair is one test: if `Pair<T, U>` had a layout, it would
    // be a *different* layout from `Pair<int, long>`, and two layouts for one name
    // is the ABI split ADR 0002's erasure rule exists to avoid.
    auto t = typeFromSource("struct Pair<T, U> { pub a <T>, pub b <U>, }\n", "Pair");
    ASSERT_TRUE(t != nullptr);
    auto generic = std::dynamic_pointer_cast<StructType>(t);
    ASSERT_TRUE(generic != nullptr);
    LayoutEngine e;
    EXPECT_FALSE(e.layoutOf(generic).ok());

    auto concrete = generic->instantiate({prim("char"), prim("long")});
    ASSERT_TRUE(concrete != nullptr);
    auto layout = must(e.layoutOf(concrete));
    ASSERT_EQ(layout.fields.size(), 2u);
    EXPECT_EQ(layout.fields[0].offset, 0u);
    EXPECT_EQ(layout.fields[1].offset, 8u);
    EXPECT_EQ(layout.size, 16u);
}

TEST(Soundness_Layout, TwoInstantiationsOfOneGenericHaveTheirOwnLayouts) {
    auto t = typeFromSource("struct Box<T> { pub v <T>, }\n", "Box");
    auto generic = std::dynamic_pointer_cast<StructType>(t);
    ASSERT_TRUE(generic != nullptr);
    LayoutEngine e;
    EXPECT_EQ(must(e.layoutOf(generic->instantiate({prim("char")}))).size, 1u);
    EXPECT_EQ(must(e.layoutOf(generic->instantiate({prim("double")}))).size, 8u);
}

// --- refusals --------------------------------------------------------------
//
// One test per representation Fin has not decided. Each asserts a refusal *and*
// that the refusal names the thing, because a refusal that says only "no layout"
// sends the reader to the source of the compiler rather than to the decision that
// is missing.

TEST(Soundness_Layout, VoidHasNoLayout) {
    LayoutEngine e;
    EXPECT_FALSE(e.layoutOf(prim("void")).ok());
    EXPECT_FALSE(e.layoutOf(prim("noret")).ok());
}

TEST(Soundness_Layout, AutoHasNoLayout) {
    LayoutEngine e;
    EXPECT_FALSE(e.layoutOf(prim("auto")).ok());
}

TEST(Soundness_Layout, AnInterfaceHasNoLayout) {
    auto t = typeFromSource("interface I { pub fun f() <int>; }\n", "I");
    ASSERT_TRUE(t != nullptr);
    LayoutEngine e;
    auto r = e.layoutOf(t);
    EXPECT_FALSE(r.ok());
    EXPECT_NE(r.refusal.find("I"), std::string::npos) << r.refusal;
}

TEST(Soundness_Layout, AnEnumHasNoLayout) {
    auto t = typeFromSource("enum Status { OK, FAIL }\n", "Status");
    ASSERT_TRUE(t != nullptr);
    LayoutEngine e;
    EXPECT_FALSE(e.layoutOf(t).ok());
}

TEST(Soundness_Layout, ADynamicTypeHasNoLayout) {
    // `any` is `{i8*, i64}` per docs/plan.md -- and that layout is to be emitted
    // from a declaration in lib/std rather than hardcoded, which is ADR 0003's
    // "library not compiler feature" applied to itself. Hardcoding 16 here would
    // be the hardcoding the plan refuses, one file earlier.
    LayoutEngine e;
    EXPECT_FALSE(e.layoutOf(std::make_shared<DynamicType>("any")).ok());
    EXPECT_FALSE(e.layoutOf(std::make_shared<DynamicType>("object")).ok());
}

TEST(Soundness_Layout, AFunctionTypeHasNoLayout) {
    LayoutEngine e;
    auto fn = std::make_shared<FunctionType>(std::vector<TypePtr>{prim("int")}, prim("int"));
    EXPECT_FALSE(e.layoutOf(fn).ok());
}

TEST(Soundness_Layout, ANullablePointerLaysOutAndANullableValueDoesNot) {
    // `null` is the null pointer, so `(&T)?` needs no discriminant and is the same
    // eight bytes. `int?` needs somewhere to put "absent", and where that goes --
    // a flag byte, a reserved bit pattern, a separate word -- is not decided.
    LayoutEngine e;
    auto nullablePtr = std::make_shared<NullableType>(std::make_shared<PointerType>(prim("int")));
    auto layout = must(e.layoutOf(nullablePtr));
    EXPECT_EQ(layout.size, 8u);
    EXPECT_EQ(layout.align, 8u);
    ASSERT_EQ(layout.pointers.size(), 1u) << "a nullable pointer is still a traced slot";
    EXPECT_EQ(layout.pointers[0].offset, 0u);

    EXPECT_FALSE(e.layoutOf(std::make_shared<NullableType>(prim("int"))).ok());
}

TEST(Soundness_Layout, ARefusalInAFieldNamesTheFieldAndSurvivesToTheOuterType) {
    auto s = std::make_shared<StructType>("Outer");
    s->defineField("fine", prim("int"), true);
    s->defineField("broken", std::make_shared<GenericType>("T"), true);
    LayoutEngine e;
    auto r = e.layoutOf(s);
    EXPECT_FALSE(r.ok());
    EXPECT_NE(r.refusal.find("broken"), std::string::npos) << r.refusal;
    EXPECT_NE(r.refusal.find("Outer"), std::string::npos) << r.refusal;
}

// --- the two phases --------------------------------------------------------
//
// Step 7 of docs/compiler-api.md's wave-4 order, built with step 6 rather than
// after it, for the reason the plan gives: "retrofitting is how you get D's
// all-zero interface bitmap". There are no events yet to fire these, and that is
// fine -- what must exist before the events do is a layout pass in which "the
// offsets are not decided yet" is a *state*, not an absence.

TEST(Soundness_Layout, ALayoutQueryDuringTheDecidePhaseIsRefusedAndNamesThePhase) {
    auto s = std::make_shared<StructType>("S");
    s->defineField("a", prim("int"), true);
    LayoutEngine e;
    e.beginDeciding(s);
    auto r = e.layoutOf(s);
    ASSERT_FALSE(r.ok()) << "size_of during struct_layout_deciding must be a diagnostic, "
                            "never a number that is wrong later";
    EXPECT_NE(r.refusal.find("deciding"), std::string::npos) << r.refusal;
    EXPECT_FALSE(e.isLayoutFinal(s));
}

TEST(Soundness_Layout, IsLayoutFinalBecomesTrueOnlyAfterFinalise) {
    auto s = std::make_shared<StructType>("S");
    s->defineField("a", prim("int"), true);
    LayoutEngine e;
    EXPECT_FALSE(e.isLayoutFinal(s));
    e.beginDeciding(s);
    EXPECT_FALSE(e.isLayoutFinal(s));
    EXPECT_TRUE(e.finalise(s).ok());
    EXPECT_TRUE(e.isLayoutFinal(s));
}

TEST(Soundness_Layout, AHeaderRequestOutsideTheDecidePhaseIsRefused) {
    auto s = std::make_shared<StructType>("S");
    s->defineField("a", prim("int"), true);
    LayoutEngine e;
    EXPECT_FALSE(e.requestHeaderWords(s, 1).empty())
        << "request_header_words is legal only inside struct_layout_deciding";
    e.beginDeciding(s);
    EXPECT_TRUE(e.requestHeaderWords(s, 1).empty());
    EXPECT_TRUE(e.finalise(s).ok());
    EXPECT_FALSE(e.requestHeaderWords(s, 1).empty()) << "and not after it is final";
}

TEST(Soundness_Layout, HeaderWordsAreAdditiveAcrossRequests) {
    // Additive rather than last-wins, so two collectors in one program get two
    // header words instead of a conflict -- docs/compiler-api.md §3.6.
    auto s = std::make_shared<StructType>("S");
    s->defineField("a", prim("int"), true);
    LayoutEngine e;
    e.beginDeciding(s);
    EXPECT_TRUE(e.requestHeaderWords(s, 1).empty());
    EXPECT_TRUE(e.requestHeaderWords(s, 2).empty());
    auto layout = must(e.finalise(s));
    EXPECT_EQ(layout.headerBytes, 24u);
    EXPECT_EQ(layout.allocationSize(), 28u);
    // The header sits *ahead* of the object pointer, so a field's offset is the
    // same whether or not a collector asked for one. Anything else would make
    // every `offset_of` in a program depend on which libraries it links -- and
    // for the same reason `size` and `align` are the type's own numbers still,
    // with the header's demands reported separately by allocationAlign.
    EXPECT_EQ(layout.fields[0].offset, 0u);
    EXPECT_EQ(layout.size, 4u);
    EXPECT_EQ(layout.align, 4u) << "an int's struct is still 4-aligned";
    EXPECT_EQ(layout.allocationAlign(e.target()), 8u)
        << "but the block holding pointer-sized header words must be pointer-aligned";
}

TEST(Soundness_Layout, AHeaderIsPaddedUpToAnOverAlignedTypesAlignment) {
    // The case that makes headerBytes a computed number rather than
    // words * pointerSize: three pointer words is 24 bytes, and if the object
    // needs 16-byte alignment then block + 24 is not 16-aligned. Padding the
    // header is what keeps the object's own offsets correct; the alternative is a
    // misaligned object nobody diagnoses.
    auto s = std::make_shared<StructType>("S");
    s->defineField("a", prim("int"), true);
    LayoutEngine e;
    e.beginDeciding(s);
    EXPECT_TRUE(e.requestMinAlign(s, 16).empty());
    EXPECT_TRUE(e.requestHeaderWords(s, 3).empty());
    auto layout = must(e.finalise(s));
    EXPECT_EQ(layout.align, 16u);
    EXPECT_EQ(layout.size, 16u);
    EXPECT_EQ(layout.headerBytes, 32u) << "24 rounded up to 16";
    EXPECT_EQ(layout.allocationSize(), 48u);
    EXPECT_EQ(layout.allocationAlign(e.target()), 16u);
}

TEST(Soundness_Layout, MinAlignIsTheMaximumOfWhatWasRequested) {
    auto s = std::make_shared<StructType>("S");
    s->defineField("a", prim("char"), true);
    LayoutEngine e;
    e.beginDeciding(s);
    EXPECT_TRUE(e.requestMinAlign(s, 4).empty());
    EXPECT_TRUE(e.requestMinAlign(s, 16).empty());
    EXPECT_TRUE(e.requestMinAlign(s, 2).empty());
    auto layout = must(e.finalise(s));
    EXPECT_EQ(layout.align, 16u);
    EXPECT_EQ(layout.size, 16u) << "size is still rounded up to the alignment";
}

TEST(Soundness_Layout, AMinAlignRequestMustBeAPowerOfTwo) {
    auto s = std::make_shared<StructType>("S");
    LayoutEngine e;
    e.beginDeciding(s);
    EXPECT_FALSE(e.requestMinAlign(s, 3).empty());
    EXPECT_FALSE(e.requestMinAlign(s, 0).empty());
}

// --- known defects ---------------------------------------------------------

TEST(KnownDefect_Layout, AFixedArrayHasNoExtentToLayOut) {
    // `[int; 4]` and `[int; 8]` are the same semantic type. TypeNode carries
    // `array_size` as an expression, CloneTypes and the macro expander both
    // preserve it, and *nothing in src/semantics reads it* -- so ArrayType has
    // `is_fixed_size` and no size. This is the same defect class as the field
    // order this unit's predecessor fixed: the AST has the answer and the semantic
    // type throws it away.
    //
    // Which is why the layout pass refuses every array rather than only dynamic
    // ones: a fixed array whose extent were guessed would produce a struct that
    // is the wrong size, which is the exact failure this whole suite exists to
    // make impossible. Whoever resolves the extent into ArrayType should invert
    // this test and add the `N * stride` case, including the stride's own padding.
    LayoutEngine e;
    auto four = std::make_shared<ArrayType>(prim("int"), true);
    auto dynamic = std::make_shared<ArrayType>(prim("int"), false);
    EXPECT_TRUE(four->equals(*std::make_shared<ArrayType>(prim("int"), true)))
        << "two fixed int arrays of different lengths are indistinguishable";
    EXPECT_FALSE(e.layoutOf(four).ok());
    EXPECT_FALSE(e.layoutOf(dynamic).ok());
}

TEST(KnownDefect_Layout, AWidthAnnotationDoesNotChangeTheSize) {
    // docs/plan.md, "Integer widths are a lie": resolveTypeFromAST walks
    // `uint{8}`'s width annotation for side effects and hands back the
    // *unannotated* type, so `uint{8}` and `uint{64}` are both `uint`. lib/std
    // defines i64, u64, size_t on top of that.
    //
    // The layout pass inherits the lie exactly rather than papering over it: it is
    // handed a `uint` and answers four. Papering over it would mean guessing which
    // width was written, at the one place in the compiler where a guess becomes an
    // ABI. Whoever makes widths real should invert this.
    auto narrow = typeFromSource("struct S { pub a <uint{8}>, }\n", "S");
    ASSERT_TRUE(narrow != nullptr);
    LayoutEngine e;
    auto layout = must(e.layoutOf(narrow));
    ASSERT_EQ(layout.fields.size(), 1u);
    EXPECT_EQ(layout.fields[0].size, 4u) << "one byte is what was written";
    EXPECT_EQ(layout.size, 4u);
}
