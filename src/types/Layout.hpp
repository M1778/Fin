#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Type.hpp"

namespace fin {

class StructType;

// Size, alignment, field offsets, and which words of a value hold pointers.
//
// docs/compiler-api.md calls this "the one thing to build first if only one thing
// gets built", and the reason is ADR 0003: Fin commits to a tracing collector
// written in Fin, and a tracing collector needs, for a given type, the byte
// offsets of the words that hold pointers. Before this file there were zero
// occurrences of `offset`, `size` or `align` anywhere in src/types/ or
// src/semantics/ -- not partial, absent -- so `compiler.layout.pointer_offsets`,
// which is non-negotiable, had nothing to stand on.
//
// Three things about the shape of this file are decisions rather than accidents.
//
// **It lives in src/types/ and depends on nothing above it.** Layout is a
// property of a type, not of a backend: `finc x.fin` with no `-o` must be able to
// answer `size_of` for a compile-time handler in a build that has no LLVM at all
// (CMakeLists.txt, FIN_WITH_LLVM=OFF). So the scalar table below is the
// compiler's *only* table of machine widths, and src/codegen reads it rather than
// carrying a second copy -- two tables that agree today are two tables that
// disagree after one edit, and the disagreement is an ABI split that shows up as
// a program that runs and prints the wrong number.
//
// **Every query has three outcomes, and the third is a refusal.** Most of Fin has
// no decided representation: an interface-typed value, an enum's tag, `any`, a
// dynamic array, a closure, `int?`. For each of those the honest answers are "no
// layout, and here is what is undecided" or a fabricated number, and §1.3 of
// docs/compiler-api.md is a case study in the second one -- D's `getPointerBitmap`
// returns an all-zero bitmap for interfaces, which is not an error, is not a
// refusal, and is silently a lie. A `LayoutResult` therefore carries a layout or a
// sentence, never both and never neither.
//
// **Layout is two moments, not one.** A handler that wants a header word for a
// mark bit has to run *before* offsets are fixed; a handler that wants to read the
// offsets has to run after. docs/compiler-api.md step 7 says to build that with
// step 6 because "retrofitting is how you get D's all-zero interface bitmap", so
// the phases are here from the start even though no event fires them yet: what has
// to exist before the events do is a layout pass in which "the offsets are not
// decided yet" is a state with a diagnostic attached, rather than an absence that
// reads as zero.
//
// What is deliberately not here: the value-form and quote-form projections
// (`pointer_offset_at`, `pointer_map_quote`) that wrap this for the compiler API,
// which are step 8 and belong with the component registry; and the `owned /
// borrowed / weak / interior` axis on a pointer slot, because Fin has no ownership
// annotation to derive it from and a field that said `owned` for every pointer in
// the program would be D's bitmap with a different name.

// --- the target -------------------------------------------------------------

// The machine finc is laying out for. Only two knobs, because only two things
// vary across the targets ADR 0010's CI matrix covers, and both of them are wrong
// on some target if left implicit.
//
// The default is the host, so a self-hosted build lays out for itself. A
// cross-compilation must pass the host it is aiming at; the day `finc --target`
// exists, this struct is what it fills in.
struct TargetLayout {
    // Pointer width in bytes. 8 everywhere finc currently emits.
    uint64_t pointerSize = sizeof(void*);
    // A scalar's alignment is its size, capped here. LP64 targets cap at 8;
    // 32-bit x86 caps at 4, which is why this is a knob and not `= size`.
    uint64_t maxScalarAlign = sizeof(void*);
};

// --- scalars ----------------------------------------------------------------

enum class ScalarKind { Void, Bool, Int, Float, Pointer };

// One scalar's machine shape, by the name written in the source.
//
// `bits` is the value's own width -- 1 for `bool`, because that is the i1 the
// backend emits and the thing that makes a bool comparison free. Its *storage*
// size is a byte; `sizeOfScalar` is what rounds one to the other, so a caller
// cannot accidentally use a bit count as a byte count.
struct ScalarInfo {
    ScalarKind kind = ScalarKind::Void;
    unsigned bits = 0;
    bool isSigned = true;
};

// The compiler's one table of machine widths. Nullopt for a name that is not a
// scalar -- including `auto`, `void` and the `$type` family, which are names with
// no runtime value rather than scalars of unknown size.
//
// `int` is 32 bits and `long` is 64 because the corpus writes `printf("%d", n)`
// and `printf("%ld", n)`, and a `%d` fed an i64 is undefined behaviour rather than
// a rounding error. Whether an assignment between two integer types is legal is a
// separate open ruling (docs/plan.md, "Integer widths are a lie"); this table
// fixes only the widths, and it is the single place a different ruling would edit.
//
// `char` is signed, matching what the backend already emits, and noted here
// because the analyzer deliberately leaves char's signedness undecided when
// deciding whether a negative literal fits it (Analyzer_Core.cpp). The two are
// consistent -- 8 bits either way -- and only the sign of a widening conversion
// depends on the difference.
std::optional<ScalarInfo> scalarByName(const std::string& name);

uint64_t sizeOfScalar(const ScalarInfo& info, const TargetLayout& target);
uint64_t alignOfScalar(const ScalarInfo& info, const TargetLayout& target);

// --- a laid-out type --------------------------------------------------------

struct FieldLayout {
    std::string name;
    TypePtr type;
    uint64_t offset = 0;
    uint64_t size = 0;
    uint64_t align = 1;
    // True for a field this type got from a base struct. Base fields come first
    // in `fields`, which is single inheritance's ABI trick: a pointer to the
    // derived type already is a pointer to the base, so an upcast emits nothing.
    bool inherited = false;
};

// One word of a value that holds a pointer, and what it points at.
//
// The pointee type is the surpassing capability of docs/compiler-api.md §1.9. D's
// `getPointerBitmap` answers "a pointer lives at this offset" and stops; knowing
// that it is a pointer *to Node* is the difference between a precise typed
// collector and a conservative one that must treat every word as a maybe.
struct PointerSlot {
    uint64_t offset = 0;
    TypePtr pointee;  // never null
};

struct TypeLayout {
    uint64_t size = 0;
    uint64_t align = 1;
    // Words reserved *ahead of* the object pointer, for a collector's mark bits or
    // forwarding pointer (`request_header_words`). Not part of `size`, and it does
    // not move a single field: an object pointer still points at the first field,
    // so `offset_of` gives the same answer whether or not the program links a
    // collector. Anything else would make every offset in a program depend on its
    // dependency list.
    uint64_t headerBytes = 0;
    // Declaration order, base fields first, each nested struct's fields left
    // whole rather than flattened -- a field of struct type is one entry whose
    // `size` is the whole inner struct.
    std::vector<FieldLayout> fields;
    // Flat, ascending by offset, nesting already resolved. Flat because what a
    // collector needs at run time is a list to walk, not a tree; resolved here
    // because compile time is where the walking should happen, once per type.
    std::vector<PointerSlot> pointers;

    // What an allocator must reserve for one of these, and the alignment the
    // block must have. `size` and `align` stay pure properties of the type --
    // arming a collector must not change what `size_of` reports for every type in
    // the program -- so the header's demands live here instead. headerBytes is
    // rounded up to `align` at layout time, which is what makes `block +
    // headerBytes` correctly aligned for a type whose alignment exceeds a pointer.
    uint64_t allocationSize() const { return headerBytes + size; }
    uint64_t allocationAlign(const TargetLayout& target) const {
        if (headerBytes == 0) return align;
        return align > target.pointerSize ? align : target.pointerSize;
    }

    // The field of that name, or null. Searches backwards, so a field that
    // shadows an inherited one of the same name wins -- which is the rule
    // StructType::getFieldType already applies by looking at its own fields
    // before its parents'.
    const FieldLayout* field(const std::string& name) const;
};

// A layout, or the sentence explaining why there is none. Exactly one of the two.
struct LayoutResult {
    bool ok() const { return refusal.empty(); }
    TypeLayout layout;
    std::string refusal;
};

// --- the engine -------------------------------------------------------------

class LayoutEngine {
public:
    explicit LayoutEngine(TargetLayout target = TargetLayout{}) : target_(target) {}

    const TargetLayout& target() const { return target_; }

    // The layout of a type, or a refusal naming what is undecided.
    //
    // By value rather than by reference into the cache, so that a caller cannot
    // hold a pointer into a table that a later query rehashes.
    //
    // Struct results are memoised by type identity, which is exact: one
    // declaration is one StructType instance, and `Pair<int, long>` and
    // `Pair<T, U>` are two. The cache keeps the type alive so that a freed type's
    // address cannot be reused by another and read as a hit. It follows that an
    // engine asked for a struct's layout *before* the front end has finished
    // filling that struct in memoises an incomplete answer -- which is what the
    // two phases below exist to make impossible to do by accident, and what
    // `reset()` exists to undo when a caller knows it happened.
    LayoutResult layoutOf(const TypePtr& type);

    // Enter the decide phase for a struct: its fields are known, its offsets are
    // not. `layoutOf` refuses for the duration, and the two request operations are
    // legal only here.
    void beginDeciding(const std::shared_ptr<StructType>& type);

    // Leave the decide phase and compute. The result is what `layoutOf` returns
    // from then on, and `isLayoutFinal` becomes true.
    LayoutResult finalise(const std::shared_ptr<StructType>& type);

    // False until a successful layout exists for the type, so a handler shared
    // between the two phases can ask rather than guess (`is_layout_final`).
    bool isLayoutFinal(const std::shared_ptr<StructType>& type) const;

    // Reserve n pointer-sized words ahead of the object. Additive across calls:
    // two collectors get two headers rather than a conflict. Returns "" on
    // success, or the refusal -- the phase it was called outside of, since a
    // request that silently did nothing would be a collector with no mark bits and
    // no diagnostic.
    std::string requestHeaderWords(const std::shared_ptr<StructType>& type, uint64_t words);

    // Raise the type's alignment. Additive by maximum, so a collector that tags
    // low pointer bits and a target that wants cache-line alignment both get what
    // they asked for. Must be a power of two.
    std::string requestMinAlign(const std::shared_ptr<StructType>& type, uint64_t align);

    // Forget every memoised layout and every open request. For a caller that
    // knows a type changed after it was laid out.
    void reset();

private:
    struct Requests {
        uint64_t headerWords = 0;
        uint64_t minAlign = 1;
    };

    LayoutResult layoutOfStruct(const std::shared_ptr<StructType>& type);
    LayoutResult compute(const TypePtr& type);

    TargetLayout target_;
    // Keyed by identity, holding the type alive -- see layoutOf.
    std::unordered_map<const Type*, TypePtr> keepAlive_;
    std::unordered_map<const Type*, LayoutResult> cache_;
    std::unordered_map<const Type*, Requests> requests_;
    std::unordered_map<const Type*, bool> deciding_;
    // The structs whose layout is being computed right now, innermost last. This
    // is what turns `struct S { s <S>, }` into a refusal that names the field
    // instead of a stack overflow.
    std::vector<const StructType*> inProgress_;
};

// Round `value` up to a multiple of `align`, which must be a power of two.
uint64_t alignUp(uint64_t value, uint64_t align);

}  // namespace fin
