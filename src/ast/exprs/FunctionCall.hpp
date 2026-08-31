#pragma once
#include "../nodes/ASTNode.hpp"
#include "../types/TypeNode.hpp" 
#include <string>
#include <vector>
#include <memory>

namespace fin {

class FunctionCall : public Expression {
public:
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;
    std::vector<std::unique_ptr<TypeNode>> generic_args;
    // Written `@name(args)`: a call of a special function, which is resolved by
    // the compiler and not by name lookup in the program
    // (tests/samples/stdlib/memory.fin:14, stdlib/enums.fin:18,
    // literal_interface.fin:6). `name` never keeps the `@`.
    bool is_special = false;
    FunctionCall(std::string n, std::vector<std::unique_ptr<Expression>> a);
    void accept(Visitor& v) override;
};

class MethodCall : public Expression {
public:
    std::unique_ptr<Expression> object;
    std::string method_name;
    std::vector<std::unique_ptr<Expression>> args;
    std::vector<std::unique_ptr<TypeNode>> generic_args;

    // The free call this one resolved to, when the qualifier was a module:
    // `stdio.printf("Big")` (complex.fin:14) resolves to `printf("Big")` and leaves it
    // here. Null on every other method call, which is all of them until the analyzer's
    // namespace branch fills it in.
    //
    // The qualifier is spent once this is set -- the same sense in which a consumed
    // `import` is spent (SemanticAnalyzer::dropConsumedImports). `args` and
    // `generic_args` are *moved* into the call below rather than copied, so every
    // argument expression has exactly one owner in the tree and no consumer can walk
    // one twice; `forEachChild` emits this member, so a structural pass sees the
    // resolved call and not the qualified spelling.
    //
    // Why the resolution is recorded on the node rather than replacing it. Replacing
    // one Expression with another needs the `unique_ptr` slot the parent holds it in,
    // and the tree has no traversal that yields slots: `forEachChild` yields
    // references, and the two ways to build one -- a second exhaustive switch beside
    // `forEachChild`, or a copy of `SubstitutionVisitor`'s 55 overrides -- each
    // duplicate the tree's shape with nothing to keep the copies in step, which is the
    // failure ADR 0004 exists to remove. What the rewrite is *for* is that no backend
    // logic reads a namespace, and this gives that: CodeGen_LLVM's visit(MethodCall&)
    // delegates here in its first statement.
    std::unique_ptr<Expression> resolved_call;

    MethodCall(std::unique_ptr<Expression> obj, std::string name, 
               std::vector<std::unique_ptr<Expression>> a,
               std::vector<std::unique_ptr<TypeNode>> g = {});
    void accept(Visitor& v) override;
};

class StaticMethodCall : public Expression {
public:
    std::unique_ptr<TypeNode> target_type; 
    std::string method_name;               
    std::vector<std::unique_ptr<Expression>> args;
    std::vector<std::unique_ptr<TypeNode>> generic_args; 

    StaticMethodCall(std::unique_ptr<TypeNode> target, std::string name, 
                     std::vector<std::unique_ptr<Expression>> a,
                     std::vector<std::unique_ptr<TypeNode>> g = {});
          
    void accept(Visitor& v) override;
};

class MacroCall : public Expression {
public:
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;
    MacroCall(std::string n, std::vector<std::unique_ptr<Expression>> a);
    void accept(Visitor& v) override;
};

}
