#pragma once
#include "Type.hpp"

namespace fin {

// The type of an expression the analyser could not type, used so that one bad
// annotation does not become a diagnostic per use of what it annotated.
//
// It is a *sentinel*, not a type in the language. Three properties define it and each
// one is load-bearing:
//
//   * No comparison involving it produces a mismatch, because checkType stops before
//     it compares (Analyzer_Core.cpp:361). That is the only place the rule lives: the
//     assignability overrides this class used to carry could not be reached, since
//     checkType is isAssignableTo's only caller outside src/types.
//   * Every member, method call and index on it yields it again -- handled at the
//     three sites in Analyzer_Expr that would otherwise say `is not a struct`,
//     `does not have methods` and `is not an array or pointer`. That is what stops a
//     cascade from merely changing shape: defining the entity without this only turns
//     `Undefined variable 'a'` into `Type 'auto' is not a struct`, which was measured
//     before this class existed.
//   * It cannot be written. `<error>` is not an identifier, so no program can name it
//     and no lookup can return it. If it ever reaches a rendered diagnostic that is a
//     propagation bug rather than a user error, and it is visibly one --
//     Soundness_ErrorRecovery.TheErrorSentinelNeverAppearsInADiagnostic watches for it.
//
// `auto` was the obvious candidate and is wrong: a declaration annotated `auto` infers
// from its initialiser, so reusing it would make `let x <NoSuchType> = 5;` compile
// clean as an `int`. The sentinel has to be distinct from every type a program may
// legitimately mention.
class ErrorType : public Type {
public:
    std::string toString() const override { return "<error>"; }
    bool equals(const Type& other) const override { return other.as<ErrorType>() != nullptr; }
    TypePtr substitute(const TypeMap&, TypePtr = nullptr) override { return std::make_shared<ErrorType>(); }
    TypePtr clone() const override { return std::make_shared<ErrorType>(); }
};

// One shared instance. Nothing distinguishes two of these, and sharing keeps pointer
// comparisons in the analyser from accidentally meaning anything.
const TypePtr& errorType();

// True when a type is the sentinel, or wraps it. A `&<error>` or `[<error>]` reaches
// the same suppression sites as a bare one -- `fun f(a: &NoSuchType)` is as common in
// the corpus as the unwrapped spelling.
bool isErrorType(const TypePtr& t);

} // namespace fin
