#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <variant>
#include "Visitor.hpp"

namespace fin {

enum class ASTTokenKind {
    INTEGER, FLOAT, STRING_LITERAL, CHAR_LITERAL, BOOL, KW_NULL,
    PLUS, MINUS, MULT, DIV, MOD,
    PLUSEQUAL, MINUSEQUAL, MULTEQUAL, DIVEQUAL,
    AND, OR, NOT,
    EQUAL, EQEQ, NOTEQ, LT, GT, LTEQ, GTEQ,
    AMPERSAND,
    UNKNOWN
};

class ASTNode {
public:
    int line = 0;
    int column = 0;
    virtual ~ASTNode() = default;
    virtual void accept(Visitor& v) = 0;
    template<typename Loc> void setLoc(const Loc& loc) {
        line = loc.begin.line;
        column = loc.begin.column;
    }
};

class Expression : public ASTNode {};
class Statement : public ASTNode {};

// --- Types ---
class TypeNode : public ASTNode {
public:
    std::string name; 
    std::vector<std::unique_ptr<TypeNode>> generics;
    bool is_pointer = false;
    bool is_array = false;
    std::unique_ptr<Expression> array_size = nullptr;
    TypeNode(std::string n) : name(std::move(n)) {}
    void accept(Visitor&) override {}
};

class GenericParam : public ASTNode {
public:
    std::string name;
    std::unique_ptr<TypeNode> constraint;
    GenericParam(std::string n, std::unique_ptr<TypeNode> c = nullptr) 
        : name(std::move(n)), constraint(std::move(c)) {}
    void accept(Visitor&) override {}
};

class Attribute : public ASTNode {
public:
    std::string name;
    std::string value_str; 
    bool is_flag = true;
    Attribute(std::string n, bool flag) : name(std::move(n)), is_flag(flag) {}
    Attribute(std::string n, std::string v) : name(std::move(n)), value_str(std::move(v)), is_flag(false) {}
    void accept(Visitor&) override {}
};

// --- Expressions ---
class Literal : public Expression {
public:
    std::string value;
    ASTTokenKind kind;
    Literal(std::string v, ASTTokenKind k) : value(std::move(v)), kind(k) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class Identifier : public Expression {
public:
    std::string name;
    Identifier(std::string n) : name(std::move(n)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class BinaryOp : public Expression {
public:
    std::unique_ptr<Expression> left;
    ASTTokenKind op;
    std::unique_ptr<Expression> right;
    BinaryOp(std::unique_ptr<Expression> l, ASTTokenKind o, std::unique_ptr<Expression> r)
        : left(std::move(l)), op(o), right(std::move(r)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class UnaryOp : public Expression {
public:
    ASTTokenKind op;
    std::unique_ptr<Expression> operand;
    UnaryOp(ASTTokenKind o, std::unique_ptr<Expression> e) : op(o), operand(std::move(e)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class FunctionCall : public Expression {
public:
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;
    std::vector<std::unique_ptr<TypeNode>> generic_args;
    FunctionCall(std::string n, std::vector<std::unique_ptr<Expression>> a) 
        : name(std::move(n)), args(std::move(a)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class MethodCall : public Expression {
public:
    std::unique_ptr<Expression> object;
    std::string method_name;
    std::vector<std::unique_ptr<Expression>> args;
    std::vector<std::unique_ptr<TypeNode>> generic_args; // For obj.method::<T>()

    MethodCall(std::unique_ptr<Expression> obj, std::string name, 
               std::vector<std::unique_ptr<Expression>> a,
               std::vector<std::unique_ptr<TypeNode>> g = {})
        : object(std::move(obj)), method_name(std::move(name)), args(std::move(a)), generic_args(std::move(g)) {}
        
    void accept(Visitor& v) override { v.visit(*this); }
};

class CastExpression : public Expression {
public:
    std::unique_ptr<TypeNode> target_type;
    std::unique_ptr<Expression> expr;
    CastExpression(std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> e)
        : target_type(std::move(t)), expr(std::move(e)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};


class StructInstantiation : public Expression {
public:
    std::string struct_name;
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> fields;
    std::vector<std::unique_ptr<TypeNode>> generic_args; // Added

    StructInstantiation(std::string n, 
                        std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f,
                        std::vector<std::unique_ptr<TypeNode>> g = {})
        : struct_name(std::move(n)), fields(std::move(f)), generic_args(std::move(g)) {}
        
    void accept(Visitor& v) override { v.visit(*this); }
};

class MemberAccess : public Expression {
public:
    std::unique_ptr<Expression> object;
    std::string member;
    MemberAccess(std::unique_ptr<Expression> obj, std::string m) 
        : object(std::move(obj)), member(std::move(m)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class ArrayLiteral : public Expression {
public:
    std::vector<std::unique_ptr<Expression>> elements;
    ArrayLiteral(std::vector<std::unique_ptr<Expression>> e) : elements(std::move(e)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class NewExpression : public Expression {
public:
    std::unique_ptr<TypeNode> type;
    std::vector<std::unique_ptr<Expression>> args;
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> init_fields;
    NewExpression(std::unique_ptr<TypeNode> t, std::vector<std::unique_ptr<Expression>> a = {})
        : type(std::move(t)), args(std::move(a)) {}
    NewExpression(std::unique_ptr<TypeNode> t, std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f)
        : type(std::move(t)), init_fields(std::move(f)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class MacroCall : public Expression {
public:
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;
    MacroCall(std::string n, std::vector<std::unique_ptr<Expression>> a)
        : name(std::move(n)), args(std::move(a)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class SizeofExpression : public Expression {
public:
    std::unique_ptr<TypeNode> type_target;
    std::unique_ptr<Expression> expr_target;
    SizeofExpression(std::unique_ptr<TypeNode> t) : type_target(std::move(t)) {}
    SizeofExpression(std::unique_ptr<Expression> e) : expr_target(std::move(e)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

// --- Statements ---
class Block : public Statement {
public:
    std::vector<std::unique_ptr<Statement>> statements;
    Block(std::vector<std::unique_ptr<Statement>> s) : statements(std::move(s)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class VariableDeclaration : public Statement {
public:
    bool is_mutable;
    std::string name;
    std::unique_ptr<TypeNode> type;
    std::unique_ptr<Expression> initializer;
    VariableDeclaration(bool mut, std::string n, std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> init)
        : is_mutable(mut), name(std::move(n)), type(std::move(t)), initializer(std::move(init)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class ReturnStatement : public Statement {
public:
    std::unique_ptr<Expression> value;
    ReturnStatement(std::unique_ptr<Expression> v) : value(std::move(v)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class ExpressionStatement : public Statement {
public:
    std::unique_ptr<Expression> expr;
    ExpressionStatement(std::unique_ptr<Expression> e) : expr(std::move(e)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class IfStatement : public Statement {
public:
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> then_block;
    std::unique_ptr<Statement> else_stmt; 
    IfStatement(std::unique_ptr<Expression> c, std::unique_ptr<Block> t, std::unique_ptr<Statement> e)
        : condition(std::move(c)), then_block(std::move(t)), else_stmt(std::move(e)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class WhileLoop : public Statement {
public:
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> body;
    WhileLoop(std::unique_ptr<Expression> c, std::unique_ptr<Block> b)
        : condition(std::move(c)), body(std::move(b)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class ForLoop : public Statement {
public:
    std::unique_ptr<Statement> init;
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> increment;
    std::unique_ptr<Block> body;
    ForLoop(std::unique_ptr<Statement> i, std::unique_ptr<Expression> c, std::unique_ptr<Expression> inc, std::unique_ptr<Block> b)
        : init(std::move(i)), condition(std::move(c)), increment(std::move(inc)), body(std::move(b)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class ForeachLoop : public Statement {
public:
    std::string var_name;
    std::unique_ptr<TypeNode> var_type;
    std::unique_ptr<Expression> iterable;
    std::unique_ptr<Block> body;
    ForeachLoop(std::string n, std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> i, std::unique_ptr<Block> b)
        : var_name(std::move(n)), var_type(std::move(t)), iterable(std::move(i)), body(std::move(b)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class BreakStatement : public Statement {
public:
    void accept(Visitor& v) override { v.visit(*this); }
};

class ContinueStatement : public Statement {
public:
    void accept(Visitor& v) override { v.visit(*this); }
};

class DeleteStatement : public Statement {
public:
    std::unique_ptr<Expression> expr;
    DeleteStatement(std::unique_ptr<Expression> e) : expr(std::move(e)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class TryCatch : public Statement {
public:
    std::unique_ptr<Block> try_block;
    std::string catch_var;
    std::unique_ptr<TypeNode> catch_type;
    std::unique_ptr<Block> catch_block;
    TryCatch(std::unique_ptr<Block> t, std::string cv, std::unique_ptr<TypeNode> ct, std::unique_ptr<Block> cb)
        : try_block(std::move(t)), catch_var(std::move(cv)), catch_type(std::move(ct)), catch_block(std::move(cb)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class BlameStatement : public Statement {
public:
    std::unique_ptr<Expression> error_expr;
    BlameStatement(std::unique_ptr<Expression> e) : error_expr(std::move(e)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class ImportModule : public Statement {
public:
    std::string source;
    bool is_package;
    std::string alias;
    std::vector<std::string> targets;
    ImportModule(std::string src, bool pkg, std::string al, std::vector<std::string> tgts)
        : source(std::move(src)), is_package(pkg), alias(std::move(al)), targets(std::move(tgts)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

// --- Declarations ---
class Parameter : public ASTNode {
public:
    std::string name;
    std::unique_ptr<TypeNode> type;
    std::unique_ptr<Expression> default_value;
    bool is_vararg = false;
    Parameter(std::string n, std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> d, bool v)
        : name(std::move(n)), type(std::move(t)), default_value(std::move(d)), is_vararg(v) {}
    void accept(Visitor&) override {}
};

class FunctionDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<Parameter>> params;
    std::unique_ptr<TypeNode> return_type;
    std::unique_ptr<Block> body;
    bool is_public;
    bool is_static = false;
    std::vector<std::unique_ptr<GenericParam>> generic_params;
    std::vector<std::unique_ptr<Attribute>> attributes;

    FunctionDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p, std::unique_ptr<TypeNode> rt, std::unique_ptr<Block> b)
        : name(std::move(n)), params(std::move(p)), return_type(std::move(rt)), body(std::move(b)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class OperatorDeclaration : public Statement {
public:
    ASTTokenKind op;
    std::vector<std::unique_ptr<Parameter>> params;
    std::unique_ptr<TypeNode> return_type;
    std::unique_ptr<Block> body;
    bool is_public;
    
    OperatorDeclaration(ASTTokenKind o, std::vector<std::unique_ptr<Parameter>> p, std::unique_ptr<TypeNode> rt, std::unique_ptr<Block> b, bool pub)
        : op(o), params(std::move(p)), return_type(std::move(rt)), body(std::move(b)), is_public(pub) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class StructMember : public ASTNode {
public:
    std::string name;
    std::unique_ptr<TypeNode> type;
    std::unique_ptr<Expression> default_value;
    bool is_public;
    std::vector<std::unique_ptr<Attribute>> attributes;
    StructMember(std::string n, std::unique_ptr<TypeNode> t, bool pub)
        : name(std::move(n)), type(std::move(t)), is_public(pub) {}
    void accept(Visitor&) override {}
};

class StructDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<StructMember>> members;
    std::vector<std::unique_ptr<FunctionDeclaration>> methods;
    std::vector<std::unique_ptr<OperatorDeclaration>> operators; // Added operators
    std::vector<std::unique_ptr<GenericParam>> generic_params;
    std::vector<std::unique_ptr<Attribute>> attributes;
    std::unique_ptr<TypeNode> parent; // Added inheritance
    bool is_public;
    
    StructDeclaration(std::string n, std::vector<std::unique_ptr<StructMember>> m, bool pub)
        : name(std::move(n)), members(std::move(m)), is_public(pub) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class InterfaceDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<StructMember>> members;
    std::vector<std::unique_ptr<FunctionDeclaration>> methods;
    bool is_public;
    
    InterfaceDeclaration(std::string n, std::vector<std::unique_ptr<StructMember>> m, 
                         std::vector<std::unique_ptr<FunctionDeclaration>> f, bool pub)
        : name(std::move(n)), members(std::move(m)), methods(std::move(f)), is_public(pub) {}
        
    void accept(Visitor& v) override { v.visit(*this); }
};

class EnumDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> values;
    EnumDeclaration(std::string n, std::vector<std::pair<std::string, std::unique_ptr<Expression>>> v)
        : name(std::move(n)), values(std::move(v)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class DefineDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<Parameter>> params;
    std::unique_ptr<TypeNode> return_type;
    bool is_vararg;
    
    DefineDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p, std::unique_ptr<TypeNode> rt, bool v)
        : name(std::move(n)), params(std::move(p)), return_type(std::move(rt)), is_vararg(v) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class MacroDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::string> params; // Simple string params for macros
    std::unique_ptr<Block> body;
    
    MacroDeclaration(std::string n, std::vector<std::string> p, std::unique_ptr<Block> b)
        : name(std::move(n)), params(std::move(p)), body(std::move(b)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

class Program : public ASTNode {
public:
    std::vector<std::unique_ptr<Statement>> statements;
    Program(std::vector<std::unique_ptr<Statement>> s) : statements(std::move(s)) {}
    void accept(Visitor& v) override { v.visit(*this); }
};

} // namespace fin