#pragma once
#include "../nodes/ASTNode.hpp"
#include "Statement.hpp" // For Block
#include "../types/TypeNode.hpp" // For ForeachLoop type
#include <memory>
#include <string>

namespace fin {

class ReturnStatement : public Statement {
public:
    std::unique_ptr<Expression> value;
    ReturnStatement(std::unique_ptr<Expression> v);
    void accept(Visitor& v) override;
};

class IfStatement : public Statement {
public:
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> then_block;
    std::unique_ptr<Statement> else_stmt; 
    IfStatement(std::unique_ptr<Expression> c, std::unique_ptr<Block> t, std::unique_ptr<Statement> e);
    void accept(Visitor& v) override;
};

class WhileLoop : public Statement {
public:
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> body;
    // `do { ... } while (c);` rather than `while (c) { ... }`. The two forms
    // share every field and differ only in whether the first pass tests the
    // condition, so this is a bit and not a class: a DoWhileLoop would need an
    // `accept` override in src/ast/Visitor.hpp, which wave 2 does not own.
    bool is_do_while = false;
    WhileLoop(std::unique_ptr<Expression> c, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

class ForLoop : public Statement {
public:
    std::unique_ptr<Statement> init;
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> increment;
    std::unique_ptr<Block> body;
    ForLoop(std::unique_ptr<Statement> i, std::unique_ptr<Expression> c, std::unique_ptr<Expression> inc, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

class ForeachLoop : public Statement {
public:
    std::string var_name;
    std::unique_ptr<TypeNode> var_type;
    std::unique_ptr<Expression> iterable;
    std::unique_ptr<Block> body;
    // The optional index binding of `foreach (idx <int>, element <int> in a)`
    // (tests/samples/loops.fin:19). Empty name means the one-binding form.
    std::string index_name;
    std::unique_ptr<TypeNode> index_type;
    ForeachLoop(std::string n, std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> i, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

class BreakStatement : public Statement {
public:
    void accept(Visitor& v) override;
};

class ContinueStatement : public Statement {
public:
    void accept(Visitor& v) override;
};

}
