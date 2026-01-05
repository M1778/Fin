#pragma once

#include "../ast/Visitor.hpp"
#include "../ast/ASTNode.hpp"
#include <unordered_map>
#include <string>
#include <memory>

namespace fin {

class SubstitutionVisitor : public Visitor {
public:
    SubstitutionVisitor(const std::unordered_map<std::string, std::unique_ptr<Expression>>& args);

    void substitute(ASTNode* node);
    std::unique_ptr<Expression> replacementExpr;

    // --- Visitor Implementation ---
    void visit(Program& node) override;
    void visit(Block& node) override;
    void visit(VariableDeclaration& node) override;
    void visit(ReturnStatement& node) override;
    void visit(ExpressionStatement& node) override;
    void visit(IfStatement& node) override;
    void visit(WhileLoop& node) override;
    void visit(ForLoop& node) override;
    void visit(ForeachLoop& node) override;
    void visit(DeleteStatement& node) override;
    void visit(BlameStatement& node) override;
    
    // Expressions
    void visit(BinaryOp& node) override;
    void visit(UnaryOp& node) override;
    void visit(FunctionCall& node) override;
    void visit(MethodCall& node) override;
    void visit(CastExpression& node) override;
    void visit(NewExpression& node) override;
    void visit(MemberAccess& node) override;
    void visit(StructInstantiation& node) override;
    void visit(ArrayLiteral& node) override;
    void visit(ArrayAccess& node) override;
    void visit(TernaryOp& node) override;
    void visit(LambdaExpression& node) override;
    void visit(Identifier& node) override;
    
    void visit(SuperExpression& node) override;
    void visit(ConstructorDeclaration& node) override;
    void visit(DestructorDeclaration& node) override;

    
    void visit(FunctionDeclaration&) override {}
    void visit(StructDeclaration&) override {}
    void visit(InterfaceDeclaration&) override {}
    void visit(EnumDeclaration&) override {}
    void visit(DefineDeclaration&) override {}
    void visit(MacroDeclaration&) override {}
    void visit(OperatorDeclaration&) override {}
    void visit(ImportModule&) override {}
    void visit(BreakStatement&) override {}
    void visit(ContinueStatement&) override {}
    void visit(TryCatch&) override {}
    void visit(Literal&) override {}
    void visit(MacroCall&) override {}
    void visit(MacroInvocation&) override {}
    void visit(QuoteExpression&) override {}
    void visit(SizeofExpression&) override {}
    void visit(FunctionTypeNode&) override {}
    void visit(TypeNode&) override {}
    void visit(Parameter& node) override;
    void visit(StructMember& node) override;
    void visit(PointerTypeNode& node) override;
    void visit(ArrayTypeNode& node) override;

private:
    const std::unordered_map<std::string, std::unique_ptr<Expression>>& args;
    std::unique_ptr<Expression> cloneArg(const std::string& name);
};

}