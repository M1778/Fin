#pragma once

#include "Visitor.hpp"
#include "ASTNode.hpp"
#include <memory>

namespace fin {

class CloneVisitor : public Visitor {
public:
    std::unique_ptr<ASTNode> result;

    template <typename T>
    std::unique_ptr<T> clone(const T* node) {
        if (!node) return nullptr;
        const_cast<T*>(node)->accept(*this);
        return std::unique_ptr<T>(static_cast<T*>(result.release()));
    }

    // --- Visitor Implementation ---
    void visit(Program& node) override;
    void visit(FunctionDeclaration& node) override;
    void visit(VariableDeclaration& node) override;
    void visit(StructDeclaration& node) override;
    void visit(InterfaceDeclaration& node) override;
    void visit(EnumDeclaration& node) override;
    void visit(DefineDeclaration& node) override;
    void visit(MacroDeclaration& node) override;
    void visit(OperatorDeclaration& node) override;
    void visit(ImportModule& node) override;
    void visit(ConstructorDeclaration& node) override;
    void visit(DestructorDeclaration& node) override;

    void visit(Block& node) override;
    void visit(ReturnStatement& node) override;
    void visit(ExpressionStatement& node) override;
    void visit(IfStatement& node) override;
    void visit(WhileLoop& node) override;
    void visit(ForLoop& node) override;
    void visit(ForeachLoop& node) override;
    void visit(BreakStatement& node) override;
    void visit(ContinueStatement& node) override;
    void visit(DeleteStatement& node) override;
    void visit(TryCatch& node) override;
    void visit(BlameStatement& node) override;

    void visit(BinaryOp& node) override;
    void visit(UnaryOp& node) override;
    void visit(Literal& node) override;
    void visit(Identifier& node) override;
    void visit(FunctionCall& node) override;
    void visit(MethodCall& node) override;
    void visit(MacroCall& node) override;
    void visit(MacroInvocation& node) override;
    void visit(CastExpression& node) override;
    void visit(NewExpression& node) override;
    void visit(MemberAccess& node) override;
    void visit(StructInstantiation& node) override;
    void visit(ArrayLiteral& node) override;
    void visit(ArrayAccess& node) override;
    void visit(SizeofExpression& node) override;
    void visit(TernaryOp& node) override;
    void visit(FunctionTypeNode& node) override;
    void visit(LambdaExpression& node) override;
    void visit(QuoteExpression& node) override;
    void visit(TypeNode& node) override;
    void visit(SuperExpression& node) override;
    void visit(Parameter& node) override;
    void visit(StructMember& node) override;
    void visit(PointerTypeNode& node) override;
    void visit(ArrayTypeNode& node) override;
};

} // namespace fin