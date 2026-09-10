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

// `int{8}` -- the name plus a written width, which is a distinct type from the
// name alone unless the width is the one the name already means.
TypePtr primWidth(const std::string& n, unsigned bits) {
    return std::make_shared<PrimitiveType>(n, bits);
}

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

TEST(Soundness_Layout, AnEmptyStructIsOneByteSoItsValuesHaveDistinctAddresses) {
    // Inverted and renamed, not relaxed. What stood here ruled the other way, and its
    // reasoning is kept because it was argued: "C++ gives an empty class one byte so that
    // two objects have distinct addresses, and Fin has no rule that requires that of a
    // struct. LLVM's `{}` is zero-sized, the backend is what has to agree with this
    // number, so zero it is -- a pad byte would be a byte this compiler invented."
    //
    // The owner ruled one byte on 2026-08-27 and supplied the rule that was said to be
    // missing: two distinct empty-struct values must not share an address. So the pad byte
    // is no longer invented, it is required, and the old argument's own closing clause is
    // what forces this side to move -- the backend now lays down an i8 for an empty
    // struct, and this pass is the one that had to agree with it.
    //
    // Left at zero, the two passes disagreed on the offset of every field placed after an
    // empty-struct member, which is a miscompile rather than a discrepancy: this pass
    // would put the next field where the backend had already written a byte.
    auto t = typeFromSource("struct S { }\n", "S");
    ASSERT_TRUE(t != nullptr);
    LayoutEngine e;
    auto layout = must(e.layoutOf(t));
    EXPECT_EQ(layout.size, 1u);
    EXPECT_EQ(layout.align, 1u);
}

TEST(Soundness_Layout, AFieldAfterAnEmptyStructMemberDoesNotOverlapIt) {
    // The consequence that made the disagreement worth fixing rather than booking. If an
    // empty struct occupied nothing, `b` would sit at offset 0 -- on top of the byte the
    // backend had already placed for `a`.
    auto t = typeFromSource(
        "struct E { }\n"
        "struct S { pub a <E>, pub b <char>, }\n",
        "S");
    ASSERT_TRUE(t != nullptr);
    LayoutEngine e;
    auto layout = must(e.layoutOf(t));
    const auto* a = layout.field("a");
    const auto* b = layout.field("b");
    ASSERT_TRUE(a != nullptr);
    ASSERT_TRUE(b != nullptr);
    EXPECT_EQ(a->offset, 0u);
    EXPECT_EQ(b->offset, 1u) << "b must not share a byte with the empty struct before it";
    EXPECT_EQ(layout.size, 2u);
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

TEST(Soundness_Layout, ADiamondBaseIsSharedOnce) {
    // ADR 0029: `MultiInherit: <Person, Student>` where `Student: <Person>`
    // shares the ancestor. `Person`'s bytes appear once at offset 0 via
    // `Student`, so both upcasts stay free and `name` names one slot.
    auto t = typeFromSource(
        "struct Person { pub name <int>, pub age <int>, }\n"
        "struct Student : <Person> { pub grade <int>, }\n"
        "struct MultiInherit : <Person, Student> { pub tag <int>, }\n",
        "MultiInherit");
    ASSERT_TRUE(t != nullptr);
    LayoutEngine e;
    auto layout = must(e.layoutOf(t));
    ASSERT_EQ(layout.fields.size(), 4u);
    EXPECT_EQ(layout.fields[0].name, "name");
    EXPECT_EQ(layout.fields[0].offset, 0u);
    EXPECT_TRUE(layout.fields[0].inherited);
    EXPECT_EQ(layout.fields[1].name, "age");
    EXPECT_EQ(layout.fields[2].name, "grade");
    EXPECT_EQ(layout.fields[3].name, "tag");
    EXPECT_FALSE(layout.fields[3].inherited);
    EXPECT_EQ(layout.field("name")->offset, 0u);
}

TEST(Soundness_Layout, TwoUnrelatedBasesAreStillRefused) {
    // ADR 0029's explicit no: sharing covers transitive ancestry only. Two
    // unrelated bases have no ruled placement, so the second base refuses
    // rather than guessing an ABI.
    auto t = typeFromSource(
        "struct P { pub p <int>, }\n"
        "struct Q { pub q <int>, }\n"
        "struct Both : <P, Q> { pub z <int>, }\n",
        "Both");
    ASSERT_TRUE(t != nullptr);
    LayoutEngine e;
    auto r = e.layoutOf(t);
    EXPECT_FALSE(r.ok());
    EXPECT_NE(r.refusal.find("more than one base struct"), std::string::npos)
        << r.refusal;
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

// --- fixed arrays ----------------------------------------------------------
//
// The predecessor of this section was KnownDefect_Layout.AFixedArrayHasNoExtentToLayOut,
// and its instruction was "invert this test and add the `N * stride` case, including
// the stride's own padding". What it was asserting is that `[int, 4]` and `[int, 8]`
// were the same semantic type -- ArrayTypeNode carried the extent as an expression,
// the cloner and the macro expander both preserved it, and nothing in src/semantics
// read it -- so the layout pass refused *every* array rather than only the dynamic
// ones, because a guessed extent is a struct that is silently the wrong size.
//
// The extent is in the type now, so the fixed case has an answer and the dynamic one
// still does not: a `[T]`'s representation (a pointer and a length side by side, a
// header ahead of the elements, something else) is undecided, and an extent does not
// decide it.

TEST(Soundness_Layout, AFixedArrayIsItsExtentTimesItsStride) {
    LayoutEngine e;
    const TypeLayout four = must(e.layoutOf(std::make_shared<ArrayType>(prim("int"), uint64_t{4})));
    EXPECT_EQ(four.size, 16u);
    // Its element's alignment, not its own size's. Sixteen bytes of ints is
    // 4-aligned; asking for 16 would over-align every array field in every struct.
    EXPECT_EQ(four.align, 4u);
    // Elements are not fields. A field has a name and `field("xs")` looks it up;
    // an element has an index, and an index is not a lookup this struct answers.
    EXPECT_TRUE(four.fields.empty());
    EXPECT_TRUE(four.pointers.empty());

    const TypeLayout eight = must(e.layoutOf(std::make_shared<ArrayType>(prim("double"), uint64_t{8})));
    EXPECT_EQ(eight.size, 64u);
    EXPECT_EQ(eight.align, 8u);

    const TypeLayout one = must(e.layoutOf(std::make_shared<ArrayType>(prim("char"), uint64_t{1})));
    EXPECT_EQ(one.size, 1u);
    EXPECT_EQ(one.align, 1u);
}

TEST(Soundness_Layout, TwoFixedArraysOfDifferentLengthsAreDifferentTypes) {
    // The defect itself, asserted directly rather than through a size: these two
    // compared *equal*, so a program could put eight ints where four fit with the
    // type system agreeing. Every number in the tests above depends on this line.
    auto four = std::make_shared<ArrayType>(prim("int"), uint64_t{4});
    auto eight = std::make_shared<ArrayType>(prim("int"), uint64_t{8});
    EXPECT_FALSE(four->equals(*eight));
    EXPECT_TRUE(four->equals(*std::make_shared<ArrayType>(prim("int"), uint64_t{4})));
    // And neither of them is the dynamic array, which promises nothing about how
    // many. This was true before -- `is_fixed_size` distinguished that much -- and
    // is asserted here so that the extent replacing the flag does not lose it.
    EXPECT_FALSE(four->equals(*std::make_shared<ArrayType>(prim("int"))));
    EXPECT_FALSE(std::make_shared<ArrayType>(prim("int"))->equals(*four));
}

TEST(Soundness_Layout, AnElementsOwnPaddingIsPartOfTheStride) {
    // `N * stride`, and the stride is the element's *size*, which already includes
    // whatever tail padding the element needs to be followed by another of itself.
    // Two ways of getting this wrong: multiplying by the element's payload and
    // packing (the second element then starts misaligned), or aligning up the total
    // instead of each element (only the last one lands right).
    auto p = typeFromSource("struct P { pub a <char>, pub b <int>, }\n", "P");
    ASSERT_TRUE(p != nullptr);
    LayoutEngine e;
    const TypeLayout one = must(e.layoutOf(p));
    ASSERT_EQ(one.size, 8u);   // char, 3 bytes of padding, int
    ASSERT_EQ(one.align, 4u);

    const TypeLayout three = must(e.layoutOf(std::make_shared<ArrayType>(p, uint64_t{3})));
    EXPECT_EQ(three.size, 24u);
    EXPECT_EQ(three.align, 4u);
}

TEST(Soundness_Layout, EveryElementOfAPointerArrayIsInTheMap) {
    // The reason this file exists (ADR 0003): a tracing collector handed a struct
    // with a `[&Node, 3]` field must be told about three pointers, not one and not
    // none. An array is the first type whose map is a *repeat* of its element's.
    LayoutEngine e;
    auto arr = std::make_shared<ArrayType>(std::make_shared<PointerType>(prim("int")), uint64_t{3});
    const TypeLayout l = must(e.layoutOf(arr));
    EXPECT_EQ(l.size, 24u);
    EXPECT_EQ(l.align, 8u);
    ASSERT_EQ(l.pointers.size(), 3u);
    EXPECT_EQ(l.pointers[0].offset, 0u);
    EXPECT_EQ(l.pointers[1].offset, 8u);
    EXPECT_EQ(l.pointers[2].offset, 16u);
    for (const auto& slot : l.pointers) {
        ASSERT_TRUE(slot.pointee != nullptr);
        EXPECT_EQ(slot.pointee->toString(), "int");
    }
}

TEST(Soundness_Layout, AnArrayOfStructsRepeatsTheElementsWholeMap) {
    auto node = typeFromSource("struct Node { pub next <&Node>, pub v <int>, }\n", "Node");
    ASSERT_TRUE(node != nullptr);
    LayoutEngine e;
    const TypeLayout one = must(e.layoutOf(node));
    ASSERT_EQ(one.size, 16u);
    ASSERT_EQ(one.pointers.size(), 1u);

    const TypeLayout two = must(e.layoutOf(std::make_shared<ArrayType>(node, uint64_t{2})));
    EXPECT_EQ(two.size, 32u);
    EXPECT_EQ(two.align, 8u);
    ASSERT_EQ(two.pointers.size(), 2u);
    EXPECT_EQ(two.pointers[0].offset, 0u);
    EXPECT_EQ(two.pointers[1].offset, 16u);
}

TEST(Soundness_Layout, ANestedArrayIsAnArrayOfArrays) {
    LayoutEngine e;
    auto inner = std::make_shared<ArrayType>(prim("int"), uint64_t{2});
    auto outer = std::make_shared<ArrayType>(inner, uint64_t{3});
    const TypeLayout l = must(e.layoutOf(outer));
    EXPECT_EQ(l.size, 24u);
    EXPECT_EQ(l.align, 4u);
}

TEST(Soundness_Layout, AnArrayOfNoElementsHasNoBytes) {
    // Following AnEmptyStructHasNoBytes rather than deciding anything new: a type
    // with nothing in it is zero bytes here, and a struct that contains one has its
    // next field at the same offset. Whether the *language* accepts `[int, 0]` is a
    // separate question, answered by the analyzer, not by this file.
    LayoutEngine e;
    const TypeLayout l = must(e.layoutOf(std::make_shared<ArrayType>(prim("int"), uint64_t{0})));
    EXPECT_EQ(l.size, 0u);
    EXPECT_EQ(l.align, 4u);
    EXPECT_TRUE(l.pointers.empty());
}

TEST(Soundness_Layout, AFixedArrayWhoseElementHasNoLayoutIsRefusedAndSaysWhy) {
    LayoutEngine e;
    auto arr = std::make_shared<ArrayType>(std::make_shared<GenericType>("T"), uint64_t{4});
    const LayoutResult r = e.layoutOf(arr);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.refusal.find("element"), std::string::npos) << r.refusal;
    EXPECT_NE(r.refusal.find("monomorphisation"), std::string::npos) << r.refusal;
}

TEST(Soundness_Layout, AnArrayTooLargeToMeasureIsRefusedRatherThanWrapped) {
    // `extent * stride` in uint64_t. The wrap is the failure mode this whole suite
    // is about: `[long, 2^61]` would come out as zero bytes, which is a number, and
    // a number is what a backend and a collector both believe.
    LayoutEngine e;
    auto huge = std::make_shared<ArrayType>(prim("long"), uint64_t{1} << 62);
    const LayoutResult r = e.layoutOf(huge);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.refusal.find("does not fit"), std::string::npos) << r.refusal;
}

TEST(Soundness_Layout, ADynamicArrayHasAPointerAndLengthLayout) {
    LayoutEngine e;
    const LayoutResult r = e.layoutOf(std::make_shared<ArrayType>(prim("int")));
    ASSERT_TRUE(r.ok()) << r.refusal;
    const uint64_t pointerSize = e.target().pointerSize;
    EXPECT_EQ(r.layout.size, pointerSize + 4u + (pointerSize >= 8 ? 4u : 0u));
    EXPECT_EQ(r.layout.align, pointerSize);
    ASSERT_EQ(r.layout.pointers.size(), 1u);
    EXPECT_EQ(r.layout.pointers[0].offset, 0u);
}

TEST(Soundness_Layout, AFixedArrayFieldTakesItsWholeExtentInAStruct) {
    // End to end, and the only test here that proves *the front end* resolves the
    // extent: every other one hands LayoutEngine an ArrayType built by hand, which
    // cannot tell whether resolveTypeFromAST reads `[int, 4]`'s size expression or
    // throws it away as it used to.
    auto s = typeFromSource("struct S { pub xs <[int, 4]>, pub n <int>, }\n", "S");
    ASSERT_TRUE(s != nullptr);
    LayoutEngine e;
    const TypeLayout l = must(e.layoutOf(s));
    EXPECT_EQ(l.size, 20u);
    EXPECT_EQ(l.align, 4u);
    ASSERT_EQ(l.fields.size(), 2u);
    EXPECT_EQ(l.fields[0].offset, 0u);
    EXPECT_EQ(l.fields[0].size, 16u);
    EXPECT_EQ(l.fields[1].offset, 16u);
    EXPECT_EQ(l.fields[1].size, 4u);
    const FieldLayout* xs = l.field("xs");
    ASSERT_TRUE(xs != nullptr);
    EXPECT_EQ(xs->size, 16u);
}

TEST(Soundness_Layout, APointerArrayFieldsSlotsAreFlattenedIntoTheOuterMap) {
    auto s = typeFromSource("struct S { pub a <int>, pub ps <[&int, 2]>, }\n", "S");
    ASSERT_TRUE(s != nullptr);
    LayoutEngine e;
    const TypeLayout l = must(e.layoutOf(s));
    EXPECT_EQ(l.align, 8u);
    EXPECT_EQ(l.size, 24u);       // int, 4 bytes of padding, two pointers
    ASSERT_EQ(l.fields.size(), 2u);
    EXPECT_EQ(l.fields[1].offset, 8u);
    ASSERT_EQ(l.pointers.size(), 2u);
    EXPECT_EQ(l.pointers[0].offset, 8u);
    EXPECT_EQ(l.pointers[1].offset, 16u);
}

TEST(Soundness_Layout, AFixedArrayOfAStructWithNoLayoutIsRefusedThroughTheField) {
    auto s = typeFromSource("interface I { pub fun f() <int>; }\n"
                            "struct S { pub xs <[I, 2]>, }\n", "S");
    ASSERT_TRUE(s != nullptr);
    LayoutEngine e;
    const LayoutResult r = e.layoutOf(s);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.refusal.find("field 'xs'"), std::string::npos) << r.refusal;
}

// --- written widths ---------------------------------------------------------
//
// `int{64}` is a written width and it is the type, so the layout pass answers
// from the width rather than from the base name. This section is small because
// the change was: `scalarOf` applies a PrimitiveType's width to the ScalarInfo
// `scalarByName` returns, and everything downstream -- sizeOfScalar,
// alignOfScalar, the struct walk, the array stride -- was already written in
// terms of `bits` and needed nothing.
//
// The set is the four widths the table names: 8, 16, 32 and 64. A well-formed
// width outside it (`uint{7}`, `int{128}`) has no layout and says so, which is
// this file's usual third outcome rather than a special case -- and it is a
// refusal here rather than a diagnostic in the front end because "Fin has no
// 128-bit integer" is a sentence nobody has ruled, while "this compiler does not
// represent one" is true and is what a refusal says. tests/samples/stdlib/
// types.fin:47 and :50 write `i128` and `u128` on purpose.
//
// Why not every width LLVM can build, which would be 1 to 2^23: LLVM's
// DataLayout rounds an integer's *store* size up to a power of two while
// sizeOfScalar computes `(bits + 7) / 8`, so the two agree at 1-16, 25-32 and
// 57-64 and disagree at 17-24, 33-56 and 65 up -- an i17 allocates four bytes
// where this file would say three, and an i128 sixteen where this file would say
// sixteen with an alignment of eight rather than sixteen. Measured against
// LLVM 22 on x86_64-pc-linux-gnu before the set was chosen. A language whose
// widths are that agreement set is not one anyone designed; four named widths is
// a set the table states, and Soundness_Codegen.TheLayoutTableAgreesWithLLVM is
// what holds those four to LLVM.

TEST(Soundness_Layout, AWrittenWidthIsTheSize) {
    // Was KnownDefect_Layout.AWidthAnnotationDoesNotChangeTheSize, whose argument
    // was that resolveTypeFromAST walked `uint{8}`'s annotation for side effects
    // and handed back the unannotated type, so this pass was handed a `uint` and
    // answered four. It said papering over that would mean guessing which width
    // was written "at the one place in the compiler where a guess becomes an ABI".
    // Nothing is guessed now: the width is on the type.
    auto narrow = typeFromSource("struct S { pub a <uint{8}>, }\n", "S");
    ASSERT_TRUE(narrow != nullptr);
    LayoutEngine e;
    auto layout = must(e.layoutOf(narrow));
    ASSERT_EQ(layout.fields.size(), 1u);
    EXPECT_EQ(layout.fields[0].size, 1u) << "one byte is what was written";
    EXPECT_EQ(layout.fields[0].align, 1u);
    EXPECT_EQ(layout.size, 1u);
    EXPECT_EQ(layout.align, 1u);
}

TEST(Soundness_Layout, EveryRepresentableWidthHasItsOwnSize) {
    // The four widths, both signs, asked of the layout pass directly. Literal
    // numbers for the same reason the scalar table above uses them: a suite that
    // recomputes `(bits + 7) / 8` agrees with any bug in `(bits + 7) / 8`.
    //
    // A width and the name that means it produce the same two numbers, which is
    // the whole claim -- `int{64}` is `long`, not something that converts to one --
    // so each row asserts against the name's row in EveryScalarHasTheWidthThe-
    // BackendEmits rather than against a formula.
    LayoutEngine e;
    struct Case { const char* name; unsigned bits; uint64_t size; uint64_t align; };
    const Case cases[] = {
        {"char", 8, 1, 1},   {"byte", 8, 1, 1},
        {"short", 16, 2, 2}, {"ushort", 16, 2, 2},
        {"int", 32, 4, 4},   {"uint", 32, 4, 4},
        {"long", 64, 8, 8},  {"ulong", 64, 8, 8},
        // A width that is not the name's own. This is the row the old defect could
        // not have: `int{8}` and `int` were one type, so one of these two numbers
        // was necessarily wrong whichever way the pass answered.
        {"int", 8, 1, 1},    {"int", 16, 2, 2},   {"int", 64, 8, 8},
        {"uint", 8, 1, 1},   {"uint", 16, 2, 2},  {"uint", 64, 8, 8},
        {"long", 8, 1, 1},   {"char", 64, 8, 8},
    };
    for (const auto& c : cases) {
        auto layout = must(e.layoutOf(primWidth(c.name, c.bits)));
        EXPECT_EQ(layout.size, c.size) << c.name << "{" << c.bits << "}";
        EXPECT_EQ(layout.align, c.align) << c.name << "{" << c.bits << "}";
        EXPECT_TRUE(layout.fields.empty()) << c.name << "{" << c.bits << "}";
        EXPECT_TRUE(layout.pointers.empty()) << c.name << "{" << c.bits << "}";
    }
}

TEST(Soundness_Layout, AWidthThisCompilerCannotRepresentHasNoLayout) {
    // The third outcome, and the reason the front end does not refuse these: they
    // are well-formed types -- one positive integer constant, exactly as `int{64}`
    // is -- that this pass has no representation for. So they get a refusal that
    // names the width, and a program that writes one is told what is missing
    // rather than told it wrote something illegal.
    //
    // 7 and 17 are inside LLVM's range and outside this table's; 128 is what
    // tests/samples/stdlib/types.fin:47 writes and docs/plan.md:3175 reserves the
    // ABI for; 24 is the one a reader is most likely to assume works, because
    // three bytes is a shape hardware has.
    LayoutEngine e;
    struct Case { const char* name; unsigned bits; };
    const Case cases[] = {
        {"uint", 7}, {"int", 7}, {"int", 17}, {"uint", 24},
        {"int", 1}, {"int", 63}, {"int", 128}, {"uint", 128},
    };
    for (const auto& c : cases) {
        const LayoutResult r = e.layoutOf(primWidth(c.name, c.bits));
        ASSERT_FALSE(r.ok()) << c.name << "{" << c.bits << "} has a size of "
                             << r.layout.size;
        // The type as written, so the reader sees the width they typed.
        EXPECT_NE(r.refusal.find(std::string(c.name) + "{" + std::to_string(c.bits) + "}"),
                  std::string::npos) << r.refusal;
        // And the set, because "no layout" without it sends the reader here rather
        // than to the four widths that do work.
        EXPECT_NE(r.refusal.find("8, 16, 32 or 64"), std::string::npos) << r.refusal;
    }
}

TEST(Soundness_Layout, AnUnrepresentableWidthIsRefusedThroughTheField) {
    // Reached through a struct rather than asked about directly, which is the
    // path a program takes: the field's refusal has to name the field *and* carry
    // the width's, because a struct three levels up is what the caller asked about
    // and "no layout" on its own would name neither.
    auto s = typeFromSource("struct S { pub a <int>, pub b <uint{7}>, }\n", "S");
    ASSERT_TRUE(s != nullptr);
    LayoutEngine e;
    const LayoutResult r = e.layoutOf(s);
    ASSERT_FALSE(r.ok());
    EXPECT_NE(r.refusal.find("field 'b'"), std::string::npos) << r.refusal;
    EXPECT_NE(r.refusal.find("uint{7}"), std::string::npos) << r.refusal;
}

TEST(Soundness_Layout, AWidthChangesAStructsSizeAndItsOffsets) {
    // What makes a dropped width an ABI bug rather than a value bug, asserted as
    // a pair: the same two fields, one struct written with widths and one with the
    // base names, are different shapes. Before this unit both were the second one.
    auto narrow = typeFromSource("struct N { pub a <uint{8}>, pub b <uint{8}>, }\n", "N");
    auto wide = typeFromSource("struct W { pub a <uint>, pub b <uint>, }\n", "W");
    ASSERT_TRUE(narrow != nullptr);
    ASSERT_TRUE(wide != nullptr);
    LayoutEngine e;
    const TypeLayout n = must(e.layoutOf(narrow));
    ASSERT_EQ(n.fields.size(), 2u);
    EXPECT_EQ(n.fields[1].offset, 1u);
    EXPECT_EQ(n.size, 2u);
    EXPECT_EQ(n.align, 1u);
    const TypeLayout w = must(e.layoutOf(wide));
    ASSERT_EQ(w.fields.size(), 2u);
    EXPECT_EQ(w.fields[1].offset, 4u);
    EXPECT_EQ(w.size, 8u);
    EXPECT_EQ(w.align, 4u);
}

TEST(Soundness_Layout, AWidthedFieldPadsAndAlignsLikeTheNameForThatWidth) {
    // A narrow field followed by a wide one, so the width is load-bearing twice:
    // once for `a`'s own byte and once for the seven bytes of padding it forces
    // before `b`. `uint{8}` then `int{64}` is `byte` then `long`, and the numbers
    // are the ones Soundness_Layout.PaddingIsWhereTheAlignmentRuleSaysItIs
    // already asserts for that pair of names.
    auto s = typeFromSource("struct S { pub a <uint{8}>, pub b <int{64}>, }\n", "S");
    ASSERT_TRUE(s != nullptr);
    LayoutEngine e;
    const TypeLayout l = must(e.layoutOf(s));
    ASSERT_EQ(l.fields.size(), 2u);
    EXPECT_EQ(l.fields[0].offset, 0u);
    EXPECT_EQ(l.fields[0].size, 1u);
    EXPECT_EQ(l.fields[1].offset, 8u);
    EXPECT_EQ(l.fields[1].size, 8u);
    EXPECT_EQ(l.size, 16u);
    EXPECT_EQ(l.align, 8u);
}

TEST(Soundness_Layout, AWidthedElementIsTheArrayStride) {
    // The array case, and the reason it is worth its own test: the stride is the
    // element's size, so a dropped width in an element position is not a narrow
    // value but a different amount of memory than the program reserved. Four
    // `uint{8}`s are four bytes; four `uint`s are sixteen.
    LayoutEngine e;
    auto narrow = std::make_shared<ArrayType>(primWidth("uint", 8), uint64_t{4});
    const TypeLayout n = must(e.layoutOf(narrow));
    EXPECT_EQ(n.size, 4u);
    EXPECT_EQ(n.align, 1u);
    auto wide = std::make_shared<ArrayType>(prim("uint"), uint64_t{4});
    const TypeLayout w = must(e.layoutOf(wide));
    EXPECT_EQ(w.size, 16u);
    EXPECT_EQ(w.align, 4u);
    // And an element with no representation refuses through the element, the way
    // AFixedArrayWhoseElementHasNoLayoutIsRefusedAndSaysWhy does for an interface.
    const LayoutResult refused =
        e.layoutOf(std::make_shared<ArrayType>(primWidth("int", 128), uint64_t{2}));
    ASSERT_FALSE(refused.ok());
    EXPECT_NE(refused.refusal.find("int{128}"), std::string::npos) << refused.refusal;
}

TEST(Soundness_Layout, AWidthOnANonIntegerDoesNotChangeItsSize) {
    // A width is a count of value bits, an IEEE format is not built from one, and
    // Fin has ruled on no floating-point format but the two in the table -- so
    // `float{128}` is `float`, which is what tests/samples/type_annotations.fin:14
    // needs to keep resolving. `bool{1}` and `string{8}` are here for the same
    // reason: the front end drops a width on anything that is not an integer
    // scalar, and this is the assertion that dropping it left the size alone
    // rather than producing a sixteen-byte float or a one-byte string.
    struct Case { const char* source; uint64_t size; uint64_t align; };
    const Case cases[] = {
        {"struct S { pub a <float{128}>, }\n", 4, 4},
        {"struct S { pub a <double{32}>, }\n", 8, 8},
        {"struct S { pub a <bool{1}>, }\n", 1, 1},
        {"struct S { pub a <bool{64}>, }\n", 1, 1},
        {"struct S { pub a <string{8}>, }\n", 8, 8},
    };
    for (const auto& c : cases) {
        auto s = typeFromSource(c.source, "S");
        ASSERT_TRUE(s != nullptr) << c.source;
        LayoutEngine e;
        const TypeLayout l = must(e.layoutOf(s));
        ASSERT_EQ(l.fields.size(), 1u) << c.source;
        EXPECT_EQ(l.fields[0].size, c.size) << c.source;
        EXPECT_EQ(l.size, c.size) << c.source;
        EXPECT_EQ(l.align, c.align) << c.source;
    }
}

TEST(Soundness_Layout, AWidthSurvivesCloneAndSubstitute) {
    // The two operations that rebuild a PrimitiveType from its parts, and the two
    // places a width is silently lost if it is added as a field and not threaded:
    // `clone()` is what an alias and a macro expansion go through, and
    // `substitute()` is monomorphisation. Both used to construct
    // `PrimitiveType(name)` and drop everything else, which for a type whose only
    // member was `name` was correct.
    auto original = primWidth("int", 8);
    LayoutEngine e;
    EXPECT_EQ(must(e.layoutOf(original->clone())).size, 1u) << "clone dropped the width";
    TypeMap empty;
    EXPECT_EQ(must(e.layoutOf(original->substitute(empty))).size, 1u)
        << "substitute dropped the width";
    // And the width is part of type identity, not decoration on it: two widths of
    // one name are two types, which is what makes the narrowing rule in
    // test_soundness.cpp have something to be true about.
    EXPECT_FALSE(primWidth("int", 8)->equals(*prim("int")));
    EXPECT_FALSE(primWidth("int", 8)->equals(*primWidth("int", 16)));
    EXPECT_TRUE(primWidth("int", 8)->equals(*primWidth("int", 8)));
    EXPECT_TRUE(primWidth("int", 32)->equals(*prim("int")))
        // `int{32}` is `int` -- the annotation states the width the name already
        // means, so writing it changes nothing. Anything else would make `int{32}`
        // a fifth integer type that happens to have int's size.
        << "a width that matches the name is the name";
}
