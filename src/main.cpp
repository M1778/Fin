#include <iostream>
#include <vector>
#include <fmt/core.h>
#include "lexer/tokens.hpp"
#include "lexer/lexer.hpp"
#include "ast/ASTNode.hpp"

// A simple printer visitor to test the AST
class ASTPrinter : public fin::Visitor {
public:
    void visit(fin::Program& node) override {
        fmt::print("Program\n");
        for (auto& stmt : node.statements) stmt->accept(*this);
    }
    void visit(fin::FunctionDeclaration& node) override {
        fmt::print("  Function: {} -> {}\n", node.name, node.return_type);
        if (node.body) node.body->accept(*this);
    }
    void visit(fin::Block& node) override {
        for (auto& stmt : node.statements) stmt->accept(*this);
    }
    void visit(fin::VariableDeclaration& node) override {
        fmt::print("    Var: {} <{}>\n", node.name, node.type);
    }
    // Stubs for others
    void visit(fin::ReturnStatement&) override {}
    void visit(fin::ExpressionStatement&) override {}
    void visit(fin::IfStatement&) override {}
    void visit(fin::WhileLoop&) override {}
    void visit(fin::BinaryOp&) override {}
    void visit(fin::Literal&) override {}
    void visit(fin::Identifier&) override {}
    void visit(fin::FunctionCall&) override {}
    void visit(fin::MacroCall&) override {}
};

int main() {
    fmt::print("Fin Compiler - AST Test\n");

    // Manually build: fun main() <void> { let x <int>; }
    
    // 1. Create Variable Declaration
    auto varDecl = std::make_unique<fin::VariableDeclaration>(
        true, "x", "int", nullptr
    );

    // 2. Create Block
    std::vector<std::unique_ptr<fin::Statement>> blockStmts;
    blockStmts.push_back(std::move(varDecl));
    auto body = std::make_unique<fin::Block>(std::move(blockStmts));

    // 3. Create Function
    auto func = std::make_unique<fin::FunctionDeclaration>(
        "main", "void", std::move(body), true
    );

    // 4. Create Program
    std::vector<std::unique_ptr<fin::Statement>> progStmts;
    progStmts.push_back(std::move(func));
    fin::Program program(std::move(progStmts));

    // 5. Visit
    ASTPrinter printer;
    program.accept(printer);

    return 0;
}
