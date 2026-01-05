#pragma once

#include "../ast/Visitor.hpp"
#include "../ast/ASTNode.hpp"
#include "../diagnostics/DiagnosticEngine.hpp"
#include "../semantics/Scope.hpp"
#include <unordered_map>
#include <string>
#include <memory>

namespace fin {

class ModuleLoader; 

class MacroExpander : public Visitor {
public:
    MacroExpander(DiagnosticEngine& diag, Scope* scope);
    
    void setModuleLoader(ModuleLoader* loader) { this->loader = loader; }
    void expand(Program& node);

    // --- Visitor Implementation ---
    
    // Root
    void visit(Program& node) override;

    // Declarations
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
    
    // Helpers (ADDED THESE)
    void visit(Parameter& node) override;
    void visit(StructMember& node) override;

    // Statements
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

    // Expressions
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
    
    // Type definitions
    void visit(PointerTypeNode& node) override;
    void visit(ArrayTypeNode& node) override;

private:
    DiagnosticEngine& diag;
    Scope* currentScope; 
    ModuleLoader* loader = nullptr;
    
    std::unique_ptr<Expression> expandedExpression; 
    
    MacroDeclaration* resolveMacro(const std::string& name);
};

} // namespace fin