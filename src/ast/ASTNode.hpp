#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <variant>
#include "Visitor.hpp"
#include "location.hh"

namespace fin {

// Forward declaration
class Visitor;

enum class ASTTokenKind {
    INTEGER, FLOAT, STRING_LITERAL, CHAR_LITERAL, BOOL, KW_NULL,
    PLUS, MINUS, MULT, DIV, MOD,
    PLUSEQUAL, MINUSEQUAL, MULTEQUAL, DIVEQUAL,
    AND, OR, NOT,
    EQUAL, EQEQ, NOTEQ, LT, GT, LTEQ, GTEQ,
    AMPERSAND,
    INCREMENT, DECREMENT,
    ARROW, RARROW,
    QUESTION,
    TILDE,
    UNKNOWN
};

class ASTNode {
public:
    fin::location loc;
    virtual ~ASTNode() = default;
    virtual void accept(Visitor& v) = 0;
    void setLoc(const fin::location& l) { loc = l; }
};

class Expression : public ASTNode {};
class Statement : public ASTNode {};

// --- Types ---
class TypeNode : public ASTNode {
public:
    std::string name; 
    std::vector<std::unique_ptr<TypeNode>> generics;
    int pointer_depth = 0; 
    bool is_array = false;
    std::unique_ptr<Expression> array_size = nullptr;
    TypeNode(std::string n);
    void accept(Visitor& v) override;
};

class FunctionTypeNode : public TypeNode {
public:
    std::vector<std::unique_ptr<TypeNode>> param_types;
    std::unique_ptr<TypeNode> return_type;
    FunctionTypeNode(std::vector<std::unique_ptr<TypeNode>> params, std::unique_ptr<TypeNode> ret);
    void accept(Visitor& v) override;
};

class GenericParam : public ASTNode {
public:
    std::string name;
    std::unique_ptr<TypeNode> constraint;
    GenericParam(std::string n, std::unique_ptr<TypeNode> c = nullptr);
    void accept(Visitor& v) override;
};

class Attribute : public ASTNode {
public:
    std::string name;
    std::string value_str; 
    bool is_flag = true;
    Attribute(std::string n, bool flag);
    Attribute(std::string n, std::string v);
    void accept(Visitor& v) override;
};

// --- Expressions ---
class Literal : public Expression {
public:
    std::string value;
    ASTTokenKind kind;
    Literal(std::string v, ASTTokenKind k);
    void accept(Visitor& v) override;
};

class Identifier : public Expression {
public:
    std::string name;
    Identifier(std::string n);
    void accept(Visitor& v) override;
};

class BinaryOp : public Expression {
public:
    std::unique_ptr<Expression> left;
    ASTTokenKind op;
    std::unique_ptr<Expression> right;
    BinaryOp(std::unique_ptr<Expression> l, ASTTokenKind o, std::unique_ptr<Expression> r);
    void accept(Visitor& v) override;
};

class UnaryOp : public Expression {
public:
    ASTTokenKind op;
    std::unique_ptr<Expression> operand;
    UnaryOp(ASTTokenKind o, std::unique_ptr<Expression> e);
    void accept(Visitor& v) override;
};

class FunctionCall : public Expression {
public:
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;
    std::vector<std::unique_ptr<TypeNode>> generic_args;
    FunctionCall(std::string n, std::vector<std::unique_ptr<Expression>> a);
    void accept(Visitor& v) override;
};

class MethodCall : public Expression {
public:
    std::unique_ptr<Expression> object;
    std::string method_name;
    std::vector<std::unique_ptr<Expression>> args;
    std::vector<std::unique_ptr<TypeNode>> generic_args;
    MethodCall(std::unique_ptr<Expression> obj, std::string name, 
               std::vector<std::unique_ptr<Expression>> a,
               std::vector<std::unique_ptr<TypeNode>> g = {});
    void accept(Visitor& v) override;
};

class CastExpression : public Expression {
public:
    std::unique_ptr<TypeNode> target_type;
    std::unique_ptr<Expression> expr;
    CastExpression(std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> e);
    void accept(Visitor& v) override;
};

class StructInstantiation : public Expression {
public:
    std::string struct_name;
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> fields;
    std::vector<std::unique_ptr<TypeNode>> generic_args;
    StructInstantiation(std::string n, 
                        std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f,
                        std::vector<std::unique_ptr<TypeNode>> g = {});
    void accept(Visitor& v) override;
};

class MemberAccess : public Expression {
public:
    std::unique_ptr<Expression> object;
    std::string member;
    MemberAccess(std::unique_ptr<Expression> obj, std::string m);
    void accept(Visitor& v) override;
};

class ArrayLiteral : public Expression {
public:
    std::vector<std::unique_ptr<Expression>> elements;
    ArrayLiteral(std::vector<std::unique_ptr<Expression>> e);
    void accept(Visitor& v) override;
};

class ArrayAccess : public Expression {
public:
    std::unique_ptr<Expression> array;
    std::unique_ptr<Expression> index;
    ArrayAccess(std::unique_ptr<Expression> a, std::unique_ptr<Expression> i);
    void accept(Visitor& v) override;
};

class NewExpression : public Expression {
public:
    std::unique_ptr<TypeNode> type;
    std::vector<std::unique_ptr<Expression>> args;
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> init_fields;
    NewExpression(std::unique_ptr<TypeNode> t, std::vector<std::unique_ptr<Expression>> a = {});
    NewExpression(std::unique_ptr<TypeNode> t, std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f);
    void accept(Visitor& v) override;
};

class MacroCall : public Expression {
public:
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;
    MacroCall(std::string n, std::vector<std::unique_ptr<Expression>> a);
    void accept(Visitor& v) override;
};

class MacroInvocation : public Expression {
public:
    std::string name;
    std::vector<std::unique_ptr<Expression>> args;
    MacroInvocation(std::string n, std::vector<std::unique_ptr<Expression>> a);
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

// --- Statements ---
class Block : public Statement {
public:
    std::vector<std::unique_ptr<Statement>> statements;
    Block(std::vector<std::unique_ptr<Statement>> s);
    void accept(Visitor& v) override;
};

class Parameter : public ASTNode {
public:
    std::string name;
    std::unique_ptr<TypeNode> type;
    std::unique_ptr<Expression> default_value;
    bool is_vararg = false;
    Parameter(std::string n, std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> d, bool v);
    void accept(Visitor&) override;
};

class QuoteExpression : public Expression {
public:
    std::unique_ptr<Block> block;
    QuoteExpression(std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

class LambdaExpression : public Expression {
public:
    std::vector<std::unique_ptr<Parameter>> params;
    std::unique_ptr<TypeNode> return_type;
    std::unique_ptr<Block> body;
    std::unique_ptr<Expression> expression_body;
    
    LambdaExpression(std::vector<std::unique_ptr<Parameter>> p, 
                     std::unique_ptr<TypeNode> rt, 
                     std::unique_ptr<Block> b);

    LambdaExpression(std::vector<std::unique_ptr<Parameter>> p, 
                     std::unique_ptr<TypeNode> rt, 
                     std::unique_ptr<Expression> expr);
    void accept(Visitor& v) override;
};

class SuperExpression : public Expression {
public:
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> init_fields;
    std::string parent_name; // Now used for both Init and Call styles
    std::vector<std::unique_ptr<Expression>> args;
    // Case 1: super { ... }
    SuperExpression(std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f);

    // Case 2: super::Parent(...)
    SuperExpression(std::string p, std::vector<std::unique_ptr<Expression>> a);

    // Case 3: super::Parent { ... }
    SuperExpression(std::string p, std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f);

    void accept(Visitor& v) override;
};

class VariableDeclaration : public Statement {
public:
    bool is_mutable;
    std::string name;
    std::unique_ptr<TypeNode> type;
    std::unique_ptr<Expression> initializer;
    VariableDeclaration(bool mut, std::string n, std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> init);
    void accept(Visitor& v) override;
};

class ReturnStatement : public Statement {
public:
    std::unique_ptr<Expression> value;
    ReturnStatement(std::unique_ptr<Expression> v);
    void accept(Visitor& v) override;
};

class ExpressionStatement : public Statement {
public:
    std::unique_ptr<Expression> expr;
    ExpressionStatement(std::unique_ptr<Expression> e);
    void accept(Visitor& v) override;
};

class IfStatement : public Statement {
public:
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> then_block;
    std::unique_ptr<Statement> else_stmt; 
    IfStatement(std::unique_ptr<Expression> c, std::unique_ptr<Block> t, std::unique_ptr<Statement> e);
    void accept(Visitor& v) override;
};

class WhileLoop : public Statement {
public:
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Block> body;
    WhileLoop(std::unique_ptr<Expression> c, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

class ForLoop : public Statement {
public:
    std::unique_ptr<Statement> init;
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> increment;
    std::unique_ptr<Block> body;
    ForLoop(std::unique_ptr<Statement> i, std::unique_ptr<Expression> c, std::unique_ptr<Expression> inc, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

class ForeachLoop : public Statement {
public:
    std::string var_name;
    std::unique_ptr<TypeNode> var_type;
    std::unique_ptr<Expression> iterable;
    std::unique_ptr<Block> body;
    ForeachLoop(std::string n, std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> i, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

class BreakStatement : public Statement {
public:
    void accept(Visitor& v) override;
};

class ContinueStatement : public Statement {
public:
    void accept(Visitor& v) override;
};

class DeleteStatement : public Statement {
public:
    std::unique_ptr<Expression> expr;
    DeleteStatement(std::unique_ptr<Expression> e);
    void accept(Visitor& v) override;
};

class TryCatch : public Statement {
public:
    std::unique_ptr<Block> try_block;
    std::string catch_var;
    std::unique_ptr<TypeNode> catch_type;
    std::unique_ptr<Block> catch_block;
    TryCatch(std::unique_ptr<Block> t, std::string cv, std::unique_ptr<TypeNode> ct, std::unique_ptr<Block> cb);
    void accept(Visitor& v) override;
};

class BlameStatement : public Statement {
public:
    std::unique_ptr<Expression> error_expr;
    BlameStatement(std::unique_ptr<Expression> e);
    void accept(Visitor& v) override;
};

class ImportModule : public Statement {
public:
    std::string source;
    bool is_package;
    std::string alias;
    std::vector<std::string> targets;
    ImportModule(std::string src, bool pkg, std::string al, std::vector<std::string> tgts);
    void accept(Visitor& v) override;
};

// --- Declarations ---

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

    FunctionDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p, std::unique_ptr<TypeNode> rt, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

class OperatorDeclaration : public Statement {
public:
    ASTTokenKind op;
    std::vector<std::unique_ptr<Parameter>> params;
    std::unique_ptr<TypeNode> return_type;
    std::unique_ptr<Block> body;
    bool is_public;
    std::vector<std::unique_ptr<GenericParam>> generic_params;

    OperatorDeclaration(ASTTokenKind o, std::vector<std::unique_ptr<Parameter>> p, 
                        std::unique_ptr<TypeNode> rt, std::unique_ptr<Block> b, bool pub);
    void accept(Visitor& v) override;
};

class ConstructorDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<Parameter>> params;
    std::unique_ptr<Block> body;
    std::unique_ptr<TypeNode> return_type;
    
    ConstructorDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p, std::unique_ptr<Block> b, std::unique_ptr<TypeNode> rt = nullptr);
    void accept(Visitor& v) override;
};

class DestructorDeclaration : public Statement {
public:
    std::string name;
    std::unique_ptr<Block> body;
    
    DestructorDeclaration(std::string n, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

class StructMember : public ASTNode {
public:
    std::string name;
    std::unique_ptr<TypeNode> type;
    std::unique_ptr<Expression> default_value;
    bool is_public;
    std::vector<std::unique_ptr<Attribute>> attributes;
    StructMember(std::string n, std::unique_ptr<TypeNode> t, bool pub);
    void accept(Visitor&) override;
};

class StructDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<StructMember>> members;
    std::vector<std::unique_ptr<FunctionDeclaration>> methods;
    std::vector<std::unique_ptr<OperatorDeclaration>> operators;
    std::vector<std::unique_ptr<ConstructorDeclaration>> constructors;
    std::unique_ptr<DestructorDeclaration> destructor;
    std::vector<std::unique_ptr<GenericParam>> generic_params;
    std::vector<std::unique_ptr<Attribute>> attributes;
    std::vector<std::unique_ptr<TypeNode>> parents;
    bool is_public;
    
    StructDeclaration(std::string n, std::vector<std::unique_ptr<StructMember>> m, bool pub);
    void accept(Visitor& v) override;
};

class InterfaceDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<StructMember>> members;
    std::vector<std::unique_ptr<FunctionDeclaration>> methods;
    std::vector<std::unique_ptr<OperatorDeclaration>> operators;
    std::vector<std::unique_ptr<ConstructorDeclaration>> constructors;
    std::unique_ptr<DestructorDeclaration> destructor;
    
    std::vector<std::unique_ptr<Attribute>> attributes;
    std::vector<std::unique_ptr<GenericParam>> generic_params;
    bool is_public;
    
    InterfaceDeclaration(std::string n, 
                         std::vector<std::unique_ptr<StructMember>> m, 
                         std::vector<std::unique_ptr<FunctionDeclaration>> f, 
                         std::vector<std::unique_ptr<OperatorDeclaration>> o,
                         std::vector<std::unique_ptr<ConstructorDeclaration>> c,
                         std::unique_ptr<DestructorDeclaration> d,
                         bool pub);
    void accept(Visitor& v) override;
};

class EnumDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> values;
    std::vector<std::unique_ptr<Attribute>> attributes;
    bool is_public;
    EnumDeclaration(std::string n, std::vector<std::pair<std::string, std::unique_ptr<Expression>>> v, bool pub);
    void accept(Visitor& v) override;
};

class DefineDeclaration : public Statement {
public:
    std::string name;
    std::vector<std::unique_ptr<Parameter>> params;
    std::unique_ptr<TypeNode> return_type;
    bool is_vararg;
    
    DefineDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p, std::unique_ptr<TypeNode> rt, bool v);
    void accept(Visitor& v) override;
};

// --- NEW: MacroParam Struct ---
struct MacroParam {
    std::string name;
    std::string type; // "expr", "block", "ident"
    bool is_vararg = false; // <--- ADDED
};

class PointerTypeNode : public TypeNode {
public:
    std::unique_ptr<TypeNode> pointee;
    PointerTypeNode(std::unique_ptr<TypeNode> p);
    void accept(Visitor& v) override;
};

// Array Type AST ([T] or [T, size])
class ArrayTypeNode : public TypeNode {
public:
    std::unique_ptr<TypeNode> element_type;
    std::unique_ptr<Expression> size; // Optional
    ArrayTypeNode(std::unique_ptr<TypeNode> elem, std::unique_ptr<Expression> s = nullptr);
    void accept(Visitor& v) override;
};

class MacroDeclaration : public Statement {
public:
    std::string name;
    std::vector<MacroParam> params; // Stores macro parameters
    std::unique_ptr<Block> body;
    
    MacroDeclaration(std::string n, std::vector<MacroParam> p, std::unique_ptr<Block> b);
    void accept(Visitor& v) override;
};

class Program : public ASTNode {
public:
    std::vector<std::unique_ptr<Statement>> statements;
    Program(std::vector<std::unique_ptr<Statement>> s);
    void accept(Visitor& v) override;
};

} // namespace fin