#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <variant>
#include "location.hh"

namespace fin {

class Visitor;

enum class ASTTokenKind {
    INTEGER, FLOAT, STRING_LITERAL, CHAR_LITERAL, BOOL, KW_NULL,
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
    INDEX, INDEX_ASSIGN, DEREF, UNARY_MINUS, VARIADIC_CALL,
    UNKNOWN
};

class ASTNode {
public:
    fin::location loc;
    virtual ~ASTNode() = default;
    virtual void accept(Visitor& v) = 0;
    void setLoc(const fin::location& l) { loc = l; }
};

class Expression : public ASTNode {};
class Statement : public ASTNode {};

} // namespace fin
