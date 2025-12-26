#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include "Visitor.hpp"
#include "../lexer/tokens.hpp" // For TokenKind

namespace fin {

// ============================================================================
// Base Node
// ============================================================================
class ASTNode {
public:
    int line = 0;
    int column = 0;

    virtual ~ASTNode() = default;
    virtual void accept(Visitor& v) = 0;

    // Helper to set location from a token
    void setLoc(int l, int c) { line = l; column = c; }
};

// ============================================================================
// Expressions
// ============================================================================
class Expression : public ASTNode {};

class Literal : public Expression {
public:
    std::string value; // Storing everything as string for now (parsed later)
    TokenKind type;    // INTEGER, FLOAT, STRING_LITERAL

    Literal(std::string v, TokenKind t) : value(std::move(v)), type(t) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class Identifier : public Expression {
public:
    std::string name;

    Identifier(std::string n) : name(std::move(n)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class BinaryOp : public Expression {
public:
    std::unique_ptr<Expression> left;
    TokenKind op;
    std::unique_ptr<Expression> right;

    BinaryOp(std::unique_ptr<Expression> l, TokenKind o, std::unique_ptr<Expression> r)
        : left(std::move(l)), op(o), right(std::move(r)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class FunctionCall : public Expression {
public:
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;
    // TODO: Add generic_args later

    FunctionCall(std::string n, std::vector<std::unique_ptr<Expression>> a)
        : name(std::move(n)), args(std::move(a)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class MacroCall : public Expression {
public:
    std::string name; // e.g., "log_all" (without $)
    std::vector<std::unique_ptr<Expression>> args;

    MacroCall(std::string n, std::vector<std::unique_ptr<Expression>> a)
        : name(std::move(n)), args(std::move(a)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

// ============================================================================
// Statements
// ============================================================================
class Statement : public ASTNode {};

class Block : public Statement {
public:
    std::vector<std::unique_ptr<Statement>> statements;

    Block(std::vector<std::unique_ptr<Statement>> stmts)
        : statements(std::move(stmts)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class VariableDeclaration : public Statement {
public:
    bool is_mutable;
    std::string name;
    std::string type; // e.g., "int" or "Box<int>"
    std::unique_ptr<Expression> initializer; // Optional (can be null)

    VariableDeclaration(bool mut, std::string n, std::string t, std::unique_ptr<Expression> init)
        : is_mutable(mut), name(std::move(n)), type(std::move(t)), initializer(std::move(init)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class ReturnStatement : public Statement {
public:
    std::unique_ptr<Expression> value; // Optional

    ReturnStatement(std::unique_ptr<Expression> v) : value(std::move(v)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class ExpressionStatement : public Statement {
public:
    std::unique_ptr<Expression> expr;

    ExpressionStatement(std::unique_ptr<Expression> e) : expr(std::move(e)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class IfStatement : public Statement {
public:
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> then_branch;
    std::unique_ptr<Statement> else_branch; // Can be Block or another IfStatement (elseif)

    IfStatement(std::unique_ptr<Expression> cond, std::unique_ptr<Block> thenB, std::unique_ptr<Statement> elseB)
        : condition(std::move(cond)), then_branch(std::move(thenB)), else_branch(std::move(elseB)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class WhileLoop : public Statement {
public:
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> body;

    WhileLoop(std::unique_ptr<Expression> cond, std::unique_ptr<Block> b)
        : condition(std::move(cond)), body(std::move(b)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

// ============================================================================
// Declarations (Top Level)
// ============================================================================
class FunctionDeclaration : public Statement {
public:
    std::string name;
    // TODO: Params
    std::string return_type;
    std::unique_ptr<Block> body;
    bool is_public;

    FunctionDeclaration(std::string n, std::string ret, std::unique_ptr<Block> b, bool pub)
        : name(std::move(n)), return_type(std::move(ret)), body(std::move(b)), is_public(pub) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class Program : public ASTNode {
public:
    std::vector<std::unique_ptr<Statement>> statements;

    Program(std::vector<std::unique_ptr<Statement>> stmts)
        : statements(std::move(stmts)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

} // namespace fin
