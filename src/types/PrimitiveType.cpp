#include "PrimitiveType.hpp"
#include "TypeImpl.hpp" // For typesEqual and others if needed
#include "Layout.hpp"

namespace fin {

bool PrimitiveType::equals(const Type& other) const {
    if (auto* o = other.as<PrimitiveType>()) return name == o->name;
    return false;
}
bool PrimitiveType::isAssignableTo(const Type& other) const {
    if (Type::isAssignableTo(other)) return true;
    if (name == "int" && other.toString() == "float") return true;

    // An integer converts implicitly to a wider integer (ADR 0022).
    //
    // The corpus asks for this at five sites in one file and writes a cast at none
    // of them: tests/samples/stdlib/stdio.fin declares `stream_length <ulong>` and
    // `pointer <ulong>` (:81, :82, :96, :97) while every buffer length and loop
    // index around them is an `int`, so `self.stream_length = _temp.length` (:130)
    // and `= data.length` (:135) hand an `int` to a `ulong`, and `nbytes == -1`
    // (:110) compares one against the other. A language that wanted the conversion
    // written would have five casts here; this one has none, and lib/std's derived
    // copy has none either.
    //
    // Widths come from scalarByName and are not restated here. That table is the
    // compiler's only one (Layout.hpp), which is what makes `int64` widen to
    // nothing and `int` widen to it -- the aliases resolve to a width rather than to
    // a second list that could disagree.
    //
    // Both directions of sign are permitted when the target is wider, which is the
    // ruling as given: `ushort` -> `int` loses nothing at all, and `int` -> `ulong`
    // loses only a negative, which this corpus does write -- `nbytes: ulong = -1` on
    // :109 is a sentinel the body replaces. So the rule is the width and not the sign.
    //
    // What that costs is a negative constant silently becoming a large unsigned one,
    // and it is paid, by ordering rather than by an exception: checkType reads the
    // constant *before* it asks about assignability (Analyzer_Core.cpp:467), so
    // constantFitsType's `!negative` rule still gets its say. Two tests hold it in
    // both places it has to hold -- Soundness_IntegerWidening.WideningDoesNotAdmitA-
    // NegativeConstantToAnUnsignedTarget for a declaration and .AComparisonDoesNot-
    // AdmitANegativeConstantToAnUnsigned for a comparison. This comment used to say
    // narrowing it back was "a separate ruling with no corpus site asking for it
    // yet"; those two tests are that ruling, and they landed after this paragraph.
    //
    // It also used to say that refusing `-1` against a `ulong` in a comparison while
    // accepting it in an initialiser would be the compiler disagreeing with itself
    // about one line of one file. That disagreement was real and is now gone, and it
    // was never about signs: `stdio.fin:109` was accepted because **a default argument
    // was not type-checked against its parameter's type at all**, so `fun f(a: int =
    // "hello")` compiled. The check landed one layer up, in visitParameterDefaults, and
    // nothing here changed for it -- Soundness_DefaultArguments.ADefaultArgumentIs-
    // CheckedAgainstItsParameterType is the inverted test, and it keeps both directions
    // in one place for the reason the original gave.
    //
    // Equal widths pass only when the sign agrees, so `int32` -> `int` would be the
    // identity it actually is while `int` -> `uint` stays refused: reinterpreting a
    // sign is not a widening, and no corpus line writes one. No source spelling
    // reaches that branch today -- the analyzer registers `int`, `uint`, `short`,
    // `ushort`, `long`, `ulong`, `char` and no alias, so two names for one scalar
    // cannot be written, and the corpus gets to 64 bits through Fin-level aliases
    // over a width annotation instead (`pub type i64 = int{64};`,
    // lib/std/types.fin:19). Kept rather than dropped because the day an alias is
    // registered, its absence would refuse `int32 = int`, and because the branch is
    // what makes the single table mean one scalar rather than one name.
    // Soundness_IntegerWidening.TheTablesAliasSpellingsAreNotTypeNamesYet pins which
    // of the two spellings is live, and fails if that changes.
    //
    // Integers only. `ScalarKind` keeps `bool` out on its own -- a bool is 1 bit and
    // its own kind -- and `float`/`double` are left alone because the ruling is
    // about integers and the one float rule the corpus needs is the `int` -> `float`
    // line above.
    if (auto* o = other.as<PrimitiveType>()) {
        const auto from = scalarByName(name);
        const auto to = scalarByName(o->name);
        if (from && to && from->kind == ScalarKind::Int && to->kind == ScalarKind::Int) {
            if (to->bits > from->bits) return true;
            if (to->bits == from->bits && to->isSigned == from->isSigned) return true;
        }
    }
    return false;
}

}
