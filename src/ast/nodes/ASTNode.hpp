#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <variant>
#include "location.hh"
#include "../NodeKind.hpp"

namespace fin {

class Visitor;

enum class ASTTokenKind {
    INTEGER, FLOAT, STRING_LITERAL, CHAR_LITERAL, BOOL, KW_NULL,
    // `m1778` is an expression meaning "not implemented" and nothing else
    // (ADR 0001). It is a Literal kind rather than an Identifier so it never
    // resolves against a scope: `blame m1778;` is the canonical idiom across
    // the standard library and must not depend on a variable of that name.
    M1778,
    PLUS, MINUS, MULT, DIV, MOD,
    PLUSEQUAL, MINUSEQUAL, MULTEQUAL, DIVEQUAL,
    AND, OR, NOT,
    EQUAL, EQEQ, NOTEQ, LT, GT, LTEQ, GTEQ,
    AMPERSAND,
    INCREMENT, DECREMENT,
    ARROW, RARROW,
    QUESTION,
    TILDE,
    // Extended operators
    PIPE, CARET, SHIFTLEFT, SHIFTRIGHT, SHIFTLEFTEQUAL, SHIFTRIGHTEQUAL,
    // `operator %=`, `operator &=` and `operator |=` -- declared by
    // tests/samples/stdlib/operators.fin:71,77,83. The other seven compound
    // assignments were already kinds; these three were missing, so the three
    // operators could not be named.
    MODEQUAL, AMPERSANDEQUAL, PIPEEQUAL,
    // `...objects` -- forwarding a variadic parameter into another call
    // (tests/samples/stdlib/stdio.fin:36, `format!(fmt, ...objects)`). A UnaryOp
    // kind because the spread applies to one operand.
    SPREAD,
    INDEX, INDEX_ASSIGN, DEREF, UNARY_MINUS, VARIADIC_CALL,
    UNKNOWN
};

class ASTNode {
public:
    fin::location loc;
    virtual ~ASTNode() = default;
    virtual void accept(Visitor& v) = 0;
    void setLoc(const fin::location& l) { loc = l; }

    // The kind discriminator (ADR 0004).  Resolved from the dynamic type through
    // the registry generated from FIN_NODE_LIST, which is why no node class
    // carries a `kind()` override: registration happens in one place
    // (src/ast/NodeKind.hpp) instead of 57.
    //
    // `NodeKind::Unknown` means the node's C++ type was never registered.
    NodeKind kind() const noexcept;
};

class Expression : public ASTNode {};
class Statement : public ASTNode {};

} // namespace fin
