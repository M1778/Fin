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

    // The target with its type arguments filled in: `Vec2<float>` for the
    // `Vec2::from_angle(0.7854)` of tests/samples/letssee.fin:59, whose target as
    // written is the bare template (HANDOFF section 6, item 6).
    //
    // Set by SemanticAnalyzer::recordResolvedTarget from the inference the analyzer
    // already does, and only when that inference produced a type the analyzer can spell
    // back out as a node. Concrete throughout is the ordinary case; a type *parameter* is
    // spelled as its own name and is equally sound, because this node is read once per
    // instantiation of whatever body encloses it and the mapper resolves a bare parameter
    // through the substitution active at that emission -- exactly as it does for a
    // hand-written `Box<T>`. The `spellType` of Analyzer_Expr.cpp is where both that rule
    // and its exceptions are written down.
    //
    // Null everywhere else: where the arguments and the annotation between them left a
    // parameter unbound, and where the type has no spelling (`auto`, a meta-type, a
    // function type). So the backend that reads this either gets an answer it can map or
    // gets nothing and refuses as it did before.
    //
    // Beside `target_type` rather than written into it. `target_type` is what the source
    // says and is the node a diagnostic about the target points at; an inferred argument
    // has no source spelling to point at (`Vec2::zero()` names no type anywhere), so
    // overwriting the written node would move a caret onto text nobody wrote. The same
    // record-don't-replace rule MethodCall::resolved_call follows, for the same reason.
    std::unique_ptr<TypeNode> resolved_target;

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
