#pragma once

#include "../ast/Visitor.hpp"
#include "../ast/ASTNode.hpp"
#include "../diagnostics/DiagnosticEngine.hpp"
#include "Scope.hpp"
#include "../types/Type.hpp"
#include <vector>
#include <string>
#include <fmt/core.h>
#include <fmt/color.h>

namespace fin {

class ModuleLoader; // Forward declaration

struct AnalysisContext {
    bool inLoop = false;
    std::shared_ptr<Type> currentFuncReturnType = nullptr;
};

class SemanticAnalyzer : public Visitor {
public:
    bool hasError = false;

    SemanticAnalyzer(DiagnosticEngine& diag, bool debug = false);
    ~SemanticAnalyzer();

    void setModuleLoader(ModuleLoader* loader) { this->loader = loader; }
    
    std::shared_ptr<Scope> getGlobalScope() { return globalScope; }

    // --- Visitor Implementation ---
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
    void visit(ConstructorDeclaration& node) override;
    void visit(DestructorDeclaration& node) override;
    void visit(TypeDefinition& node) override;
    void visit(SpecialDeclaration& node) override;
    void visit(ClassDeclaration& node) override;
    void visit(ImplementsBlock& node) override;

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
    void visit(PrototypeLiteral& node) override;
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
    void visit(PointerTypeNode& node) override;
    void visit(ArrayTypeNode& node) override;
    void visit(StaticMethodCall& node) override;
    void visit(Parameter& node) override;
    void visit(StructMember& node) override;

private:
    DiagnosticEngine& diag;
    bool debugMode;
    ModuleLoader* loader = nullptr; // Reference to loader

    std::vector<std::shared_ptr<Scope>> scopeStack;

    // Shared pointer for memory safety
    std::shared_ptr<Scope> globalScope;
    std::shared_ptr<Scope> currentScope;
    
    AnalysisContext context;
    std::shared_ptr<Type> lastExprType;
    std::shared_ptr<Type> currentStructContext = nullptr; 

    void enterScope();
    void exitScope();
    
    // Resolves a written type, honouring `TypeNode::is_nullable`. Every caller
    // wants that, which is why the flag is read here and not at the twenty
    // grammar sites that set it.
    std::shared_ptr<Type> resolveTypeFromAST(TypeNode* node);

    // The body of the above, without the nullable wrap. Split out rather than
    // handled at each `return` because there are eight of them and a new arm
    // silently forgetting the wrap is exactly the bug this feature was.
    std::shared_ptr<Type> resolveTypeUnwrapped(TypeNode* node);

    void error(ASTNode& node, const std::string& msg);
    bool checkType(ASTNode& node, std::shared_ptr<Type> actual, std::shared_ptr<Type> expected);

    // checkType, except that `null` is accepted whatever the declared type is.
    // For a declaration's initialiser and a member or parameter default only --
    // see the comment on the definition for why the two cannot share one check.
    bool checkInitializer(ASTNode& node, std::shared_ptr<Type> actual, std::shared_ptr<Type> expected);

    // Whether an integer constant written as `node` may take the type `target`.
    //
    // An integer constant has no type of its own until its context supplies one,
    // which is what makes `let p <ulong> = 0;` and `blame myarr[0] == 0;` legal.
    // It cannot live in PrimitiveType::isAssignableTo because the answer depends
    // on the expression and not only on the two types: `1` and `i` both have type
    // `int`, and only one of them may become a `uint`.
    //
    // Static because visit(BinaryOp&) asks the same question about an operand.
    static bool constantFitsType(const ASTNode& node, const Type& target);
    bool checkConstraint(TypeNode* typeNode, std::shared_ptr<Type> actualType, std::shared_ptr<Type> constraint);

    // Declares `<T>` / `<T: C>` into the current scope and resolves each
    // constraint. Every declaration form that can be generic calls this, which is
    // the point: the six sites had six near-identical loops, three of which
    // resolved the constraint and three of which did not, so
    // `fun f<T: NoSuchType>()` compiled clean while `struct S<T: NoSuchType>` did
    // not. Held by Soundness_GenericBounds in tests/test_soundness.cpp, one
    // test per site.
    //
    // Two passes on purpose: every name is defined before any constraint is
    // resolved, so a constraint may refer to a parameter declared beside it or to
    // the one it constrains -- `<T: Comparable<T>>`, `<T: Castable, U: T>`. The
    // single-pass order the struct site used rejected both.
    //
    // `collect` receives the GenericTypes in declaration order, for the callers
    // that also record them on a StructType as its generic_args.
    void declareGenericParams(const std::vector<std::unique_ptr<GenericParam>>& params,
                              std::vector<std::shared_ptr<Type>>* collect = nullptr);
    bool checkReturnPaths(Statement* node);
    
    template <typename... Args>
    void debugLog(const fmt::text_style& style, fmt::format_string<Args...> format, Args&&... args) {
        if (debugMode) {
            std::string msg = fmt::format(format, std::forward<Args>(args)...);
            fmt::print(style, "{}", msg);
        }
    }
};

} // namespace fin