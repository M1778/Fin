#pragma once

#include "../ast/Visitor.hpp"
#include "../ast/ASTNode.hpp"
#include "Scope.hpp"
#include "Type.hpp"
#include <vector>
#include <string>

namespace fin {

struct AnalysisContext {
    bool inLoop = false;
    std::shared_ptr<Type> currentFuncReturnType = nullptr;
};

class SemanticAnalyzer : public Visitor {
public:
    bool hasError = false;

    SemanticAnalyzer();
    ~SemanticAnalyzer();

    // --- Visitor Implementation (Declarations) ---
    void visit(Program& node) override;
    void visit(VariableDeclaration& node) override;
    void visit(FunctionDeclaration& node) override;
    void visit(StructDeclaration& node) override;
    void visit(InterfaceDeclaration& node) override;
    void visit(EnumDeclaration& node) override;
    void visit(ImportModule& node) override;
    void visit(DefineDeclaration& node) override;
    void visit(MacroDeclaration& node) override;
    void visit(OperatorDeclaration& node) override;

    // --- Visitor Implementation (Statements) ---
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
    
    // --- Visitor Implementation (Expressions) ---
    void visit(BinaryOp& node) override;
    void visit(UnaryOp& node) override;
    void visit(Literal& node) override;
    void visit(Identifier& node) override;
    void visit(FunctionCall& node) override;
    void visit(MacroCall& node) override;
    void visit(CastExpression& node) override;
    void visit(NewExpression& node) override;
    void visit(MemberAccess& node) override;
    void visit(StructInstantiation& node) override;
    void visit(ArrayLiteral& node) override;
    void visit(SizeofExpression& node) override;
    void visit(MethodCall& node) override; // <--- ADD THIS LINE

private:
    Scope* currentScope;
    AnalysisContext context;
    
    // Last evaluated type (for expressions)
    std::shared_ptr<Type> lastExprType;

    std::shared_ptr<Type> currentStructContext = nullptr; 

    void enterScope();
    void exitScope();
    
    std::shared_ptr<Type> resolveTypeFromAST(TypeNode* node);
    void error(ASTNode& node, const std::string& msg);
    
    // Helper to check type compatibility
    bool checkType(ASTNode& node, std::shared_ptr<Type> actual, std::shared_ptr<Type> expected);
};

} // namespace fin