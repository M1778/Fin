#pragma once
#include "../nodes/ASTNode.hpp"
#include "../types/TypeNode.hpp"
#include <string>
#include <vector>
#include <memory>
#include <utility>

namespace fin {

class CastExpression : public Expression {
public:
    std::unique_ptr<TypeNode> target_type;
    std::unique_ptr<Expression> expr;
    CastExpression(std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> e);
    void accept(Visitor& v) override;
};

// `struct { ... }` and `interface { ... }` in expression position --
// literal_struct.fin:24 and literal_interface.fin:20. The body is the body the
// named declaration uses, so this holds that declaration rather than a second
// representation of a type body: `decl` is a StructDeclaration or an
// InterfaceDeclaration, always with a generated name (parser.y names it from its
// location, because two literals in one scope are two types).
class TypeLiteralExpression : public Expression {
public:
    std::unique_ptr<Statement> decl;
    bool is_interface = false;
    TypeLiteralExpression(std::unique_ptr<Statement> d, bool iface);
    void accept(Visitor& v) override;
};

class SizeofExpression : public Expression {
public:
    std::unique_ptr<TypeNode> type_target;
    std::unique_ptr<Expression> expr_target;
    SizeofExpression(std::unique_ptr<TypeNode> t);
    SizeofExpression(std::unique_ptr<Expression> e);
    void accept(Visitor& v) override;
};

class TernaryOp : public Expression {
public:
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> true_expr;
    std::unique_ptr<Expression> false_expr;
    TernaryOp(std::unique_ptr<Expression> c, std::unique_ptr<Expression> t, std::unique_ptr<Expression> f);
    void accept(Visitor& v) override;
};

class SuperExpression : public Expression {
public:
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> init_fields;
    std::string parent_name; 
    std::vector<std::unique_ptr<Expression>> args;
    // `super::<Person>::name` -- tests/samples/deeptest2.fin:71-73 names the parent
    // as a generic argument. `parent_name` holds its name as the other three forms
    // do; this holds the argument list as written, so a parent with generics of its
    // own is not flattened to a bare name.
    std::vector<std::unique_ptr<TypeNode>> parent_generics;
    // True when this `super::<Parent>` is the qualifier of a member access and not
    // a call of the parent's constructor -- an empty `args` means "no arguments" in
    // the other forms, and the two must not be confused.
    bool is_qualifier = false;
    
    // Case 1: super { ... }
    SuperExpression(std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f);

    // Case 2: super::Parent(...)
    SuperExpression(std::string p, std::vector<std::unique_ptr<Expression>> a);

    // Case 3: super::Parent { ... }
    SuperExpression(std::string p, std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f);

    void accept(Visitor& v) override;
};

class MacroInvocation : public Expression {
public:
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;
    MacroInvocation(std::string n, std::vector<std::unique_ptr<Expression>> a);
    void accept(Visitor& v) override;
};

}
