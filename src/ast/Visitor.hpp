#pragma once

namespace fin {

// Forward declarations of all nodes
class Program;
class FunctionDeclaration;
class VariableDeclaration;
class Block;
class ReturnStatement;
class ExpressionStatement;
class IfStatement;
class WhileLoop;

class BinaryOp;
class Literal;
class Identifier;
class FunctionCall;
class MacroCall;

// The Visitor Interface
class Visitor {
public:
    virtual ~Visitor() = default;

    // Root
    virtual void visit(Program& node) = 0;

    // Statements
    virtual void visit(FunctionDeclaration& node) = 0;
    virtual void visit(VariableDeclaration& node) = 0;
    virtual void visit(Block& node) = 0;
    virtual void visit(ReturnStatement& node) = 0;
    virtual void visit(ExpressionStatement& node) = 0;
    virtual void visit(IfStatement& node) = 0;
    virtual void visit(WhileLoop& node) = 0;

    // Expressions
    virtual void visit(BinaryOp& node) = 0;
    virtual void visit(Literal& node) = 0;
    virtual void visit(Identifier& node) = 0;
    virtual void visit(FunctionCall& node) = 0;
    virtual void visit(MacroCall& node) = 0;
};

} // namespace fin
