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
    // :109 is a sentinel, and it is *accepted* there today. Refusing the same pair
    // at :110's comparison while accepting it at :109's initialiser would be the
    // compiler disagreeing with itself about one line of one file, so the rule is
    // the width and not the sign. What that costs is a negative constant silently
    // becoming a large unsigned one; the check that would catch it is
    // constantFitsType, which no longer runs once this returns true, and narrowing
    // it back is a separate ruling with no corpus site asking for it yet.
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
