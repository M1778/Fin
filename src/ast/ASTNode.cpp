#include "ASTNode.hpp"
#include "Visitor.hpp"

namespace fin {

// --- Types ---
TypeNode::TypeNode(std::string n) : name(std::move(n)) {}
void TypeNode::accept(Visitor& v) { v.visit(*this); }

FunctionTypeNode::FunctionTypeNode(std::vector<std::unique_ptr<TypeNode>> params, std::unique_ptr<TypeNode> ret)
    : TypeNode("fn"), param_types(std::move(params)), return_type(std::move(ret)) {}
void FunctionTypeNode::accept(Visitor& v) { v.visit(*this); }

PointerTypeNode::PointerTypeNode(std::unique_ptr<TypeNode> p) 
    : TypeNode("ptr"), pointee(std::move(p)) {}
void PointerTypeNode::accept(Visitor& v) { v.visit(*this); }

GenericParam::GenericParam(std::string n, std::unique_ptr<TypeNode> c) 
    : name(std::move(n)), constraint(std::move(c)) {}
void GenericParam::accept(Visitor&) {}

ArrayTypeNode::ArrayTypeNode(std::unique_ptr<TypeNode> elem, std::unique_ptr<Expression> s)
    : TypeNode("array"), element_type(std::move(elem)), size(std::move(s)) {}
void ArrayTypeNode::accept(Visitor& v) { v.visit(*this); }

Attribute::Attribute(std::string n, bool flag) : name(std::move(n)), is_flag(flag) {}
Attribute::Attribute(std::string n, std::string v) : name(std::move(n)), value_str(std::move(v)), is_flag(false) {}
void Attribute::accept(Visitor&) {}

// --- Expressions ---
Literal::Literal(std::string v, ASTTokenKind k) : value(std::move(v)), kind(k) {}
void Literal::accept(Visitor& v) { v.visit(*this); }

Identifier::Identifier(std::string n) : name(std::move(n)) {}
void Identifier::accept(Visitor& v) { v.visit(*this); }

BinaryOp::BinaryOp(std::unique_ptr<Expression> l, ASTTokenKind o, std::unique_ptr<Expression> r)
    : left(std::move(l)), op(o), right(std::move(r)) {}
void BinaryOp::accept(Visitor& v) { v.visit(*this); }

UnaryOp::UnaryOp(ASTTokenKind o, std::unique_ptr<Expression> e) : op(o), operand(std::move(e)) {}
void UnaryOp::accept(Visitor& v) { v.visit(*this); }

FunctionCall::FunctionCall(std::string n, std::vector<std::unique_ptr<Expression>> a) 
    : name(std::move(n)), args(std::move(a)) {}
void FunctionCall::accept(Visitor& v) { v.visit(*this); }

MethodCall::MethodCall(std::unique_ptr<Expression> obj, std::string name, 
           std::vector<std::unique_ptr<Expression>> a,
           std::vector<std::unique_ptr<TypeNode>> g)
    : object(std::move(obj)), method_name(std::move(name)), args(std::move(a)), generic_args(std::move(g)) {}
void MethodCall::accept(Visitor& v) { v.visit(*this); }

CastExpression::CastExpression(std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> e)
    : target_type(std::move(t)), expr(std::move(e)) {}
void CastExpression::accept(Visitor& v) { v.visit(*this); }

StructInstantiation::StructInstantiation(std::string n, 
                    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f,
                    std::vector<std::unique_ptr<TypeNode>> g)
    : struct_name(std::move(n)), fields(std::move(f)), generic_args(std::move(g)) {}
void StructInstantiation::accept(Visitor& v) { v.visit(*this); }

MemberAccess::MemberAccess(std::unique_ptr<Expression> obj, std::string m) 
    : object(std::move(obj)), member(std::move(m)) {}
void MemberAccess::accept(Visitor& v) { v.visit(*this); }

ArrayLiteral::ArrayLiteral(std::vector<std::unique_ptr<Expression>> e) : elements(std::move(e)) {}
void ArrayLiteral::accept(Visitor& v) { v.visit(*this); }

ArrayAccess::ArrayAccess(std::unique_ptr<Expression> a, std::unique_ptr<Expression> i) 
    : array(std::move(a)), index(std::move(i)) {}
void ArrayAccess::accept(Visitor& v) { v.visit(*this); }

NewExpression::NewExpression(std::unique_ptr<TypeNode> t, std::vector<std::unique_ptr<Expression>> a)
    : type(std::move(t)), args(std::move(a)) {}
NewExpression::NewExpression(std::unique_ptr<TypeNode> t, std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f)
    : type(std::move(t)), init_fields(std::move(f)) {}
void NewExpression::accept(Visitor& v) { v.visit(*this); }

MacroCall::MacroCall(std::string n, std::vector<std::unique_ptr<Expression>> a)
    : name(std::move(n)), args(std::move(a)) {}
void MacroCall::accept(Visitor& v) { v.visit(*this); }

MacroInvocation::MacroInvocation(std::string n, std::vector<std::unique_ptr<Expression>> a)
    : name(std::move(n)), args(std::move(a)) {}
void MacroInvocation::accept(Visitor& v) { v.visit(*this); }

SizeofExpression::SizeofExpression(std::unique_ptr<TypeNode> t) : type_target(std::move(t)) {}
SizeofExpression::SizeofExpression(std::unique_ptr<Expression> e) : expr_target(std::move(e)) {}
void SizeofExpression::accept(Visitor& v) { v.visit(*this); }

TernaryOp::TernaryOp(std::unique_ptr<Expression> c, std::unique_ptr<Expression> t, std::unique_ptr<Expression> f)
    : condition(std::move(c)), true_expr(std::move(t)), false_expr(std::move(f)) {}
void TernaryOp::accept(Visitor& v) { v.visit(*this); }

QuoteExpression::QuoteExpression(std::unique_ptr<Block> b) : block(std::move(b)) {}
void QuoteExpression::accept(Visitor& v) { v.visit(*this); }

LambdaExpression::LambdaExpression(std::vector<std::unique_ptr<Parameter>> p, 
                 std::unique_ptr<TypeNode> rt, 
                 std::unique_ptr<Block> b)
    : params(std::move(p)), return_type(std::move(rt)), body(std::move(b)) {}

LambdaExpression::LambdaExpression(std::vector<std::unique_ptr<Parameter>> p, 
                 std::unique_ptr<TypeNode> rt, 
                 std::unique_ptr<Expression> expr)
    : params(std::move(p)), return_type(std::move(rt)), expression_body(std::move(expr)) {}
void LambdaExpression::accept(Visitor& v) { v.visit(*this); }

// Case 1: super { ... }
SuperExpression::SuperExpression(std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f)
    : init_fields(std::move(f)) {}

// Case 2: super::Parent(...) or super(...)
SuperExpression::SuperExpression(std::string p, std::vector<std::unique_ptr<Expression>> a)
    : parent_name(std::move(p)), args(std::move(a)) {}

// Case 3: super::Parent { ... }
SuperExpression::SuperExpression(std::string p, std::vector<std::pair<std::string, std::unique_ptr<Expression>>> f)
    : init_fields(std::move(f)), parent_name(std::move(p)) {}

void SuperExpression::accept(Visitor& v) { v.visit(*this); }


// --- Statements ---
Block::Block(std::vector<std::unique_ptr<Statement>> s) : statements(std::move(s)) {}
void Block::accept(Visitor& v) { v.visit(*this); }

Parameter::Parameter(std::string n, std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> d, bool v)
    : name(std::move(n)), type(std::move(t)), default_value(std::move(d)), is_vararg(v) {}
void Parameter::accept(Visitor& v) { v.visit(*this); } 

VariableDeclaration::VariableDeclaration(bool mut, std::string n, std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> init)
    : is_mutable(mut), name(std::move(n)), type(std::move(t)), initializer(std::move(init)) {}
void VariableDeclaration::accept(Visitor& v) { v.visit(*this); }

ReturnStatement::ReturnStatement(std::unique_ptr<Expression> v) : value(std::move(v)) {}
void ReturnStatement::accept(Visitor& v) { v.visit(*this); }

ExpressionStatement::ExpressionStatement(std::unique_ptr<Expression> e) : expr(std::move(e)) {}
void ExpressionStatement::accept(Visitor& v) { v.visit(*this); }

IfStatement::IfStatement(std::unique_ptr<Expression> c, std::unique_ptr<Block> t, std::unique_ptr<Statement> e)
    : condition(std::move(c)), then_block(std::move(t)), else_stmt(std::move(e)) {}
void IfStatement::accept(Visitor& v) { v.visit(*this); }

WhileLoop::WhileLoop(std::unique_ptr<Expression> c, std::unique_ptr<Block> b)
    : condition(std::move(c)), body(std::move(b)) {}
void WhileLoop::accept(Visitor& v) { v.visit(*this); }

ForLoop::ForLoop(std::unique_ptr<Statement> i, std::unique_ptr<Expression> c, std::unique_ptr<Expression> inc, std::unique_ptr<Block> b)
    : init(std::move(i)), condition(std::move(c)), increment(std::move(inc)), body(std::move(b)) {}
void ForLoop::accept(Visitor& v) { v.visit(*this); }

ForeachLoop::ForeachLoop(std::string n, std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> i, std::unique_ptr<Block> b)
    : var_name(std::move(n)), var_type(std::move(t)), iterable(std::move(i)), body(std::move(b)) {}
void ForeachLoop::accept(Visitor& v) { v.visit(*this); }

void BreakStatement::accept(Visitor& v) { v.visit(*this); }
void ContinueStatement::accept(Visitor& v) { v.visit(*this); }

DeleteStatement::DeleteStatement(std::unique_ptr<Expression> e) : expr(std::move(e)) {}
void DeleteStatement::accept(Visitor& v) { v.visit(*this); }

TryCatch::TryCatch(std::unique_ptr<Block> t, std::string cv, std::unique_ptr<TypeNode> ct, std::unique_ptr<Block> cb)
    : try_block(std::move(t)), catch_var(std::move(cv)), catch_type(std::move(ct)), catch_block(std::move(cb)) {}
void TryCatch::accept(Visitor& v) { v.visit(*this); }

BlameStatement::BlameStatement(std::unique_ptr<Expression> e) : error_expr(std::move(e)) {}
void BlameStatement::accept(Visitor& v) { v.visit(*this); }

ImportModule::ImportModule(std::string src, bool pkg, std::string al, std::vector<std::string> tgts)
    : source(std::move(src)), is_package(pkg), alias(std::move(al)), targets(std::move(tgts)) {}
void ImportModule::accept(Visitor& v) { v.visit(*this); }

FunctionDeclaration::FunctionDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p, std::unique_ptr<TypeNode> rt, std::unique_ptr<Block> b)
    : name(std::move(n)), params(std::move(p)), return_type(std::move(rt)), body(std::move(b)) {}
void FunctionDeclaration::accept(Visitor& v) { v.visit(*this); }

OperatorDeclaration::OperatorDeclaration(ASTTokenKind o, std::vector<std::unique_ptr<Parameter>> p, 
                    std::unique_ptr<TypeNode> rt, std::unique_ptr<Block> b, bool pub)
    : op(o), params(std::move(p)), return_type(std::move(rt)), body(std::move(b)), is_public(pub) {}
void OperatorDeclaration::accept(Visitor& v) { v.visit(*this); }

StructMember::StructMember(std::string n, std::unique_ptr<TypeNode> t, bool pub)
    : name(std::move(n)), type(std::move(t)), is_public(pub) {}
void StructMember::accept(Visitor& v) { v.visit(*this); }

StructDeclaration::StructDeclaration(std::string n, std::vector<std::unique_ptr<StructMember>> m, bool pub)
    : name(std::move(n)), members(std::move(m)), is_public(pub) {}
void StructDeclaration::accept(Visitor& v) { v.visit(*this); }

InterfaceDeclaration::InterfaceDeclaration(std::string n, 
                     std::vector<std::unique_ptr<StructMember>> m, 
                     std::vector<std::unique_ptr<FunctionDeclaration>> f, 
                     std::vector<std::unique_ptr<OperatorDeclaration>> o,
                     std::vector<std::unique_ptr<ConstructorDeclaration>> c,
                     std::unique_ptr<DestructorDeclaration> d,
                     bool pub)
    : name(std::move(n)), members(std::move(m)), methods(std::move(f)), 
      operators(std::move(o)), constructors(std::move(c)), destructor(std::move(d)), 
      is_public(pub) {}
void InterfaceDeclaration::accept(Visitor& v) { v.visit(*this); }

EnumDeclaration::EnumDeclaration(std::string n, std::vector<std::pair<std::string, std::unique_ptr<Expression>>> v, bool pub)
    : name(std::move(n)), values(std::move(v)), is_public(pub) {}
void EnumDeclaration::accept(Visitor& v) { v.visit(*this); }

DefineDeclaration::DefineDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p, std::unique_ptr<TypeNode> rt, bool v)
    : name(std::move(n)), params(std::move(p)), return_type(std::move(rt)), is_vararg(v) {}
void DefineDeclaration::accept(Visitor& v) { v.visit(*this); }

MacroDeclaration::MacroDeclaration(std::string n, std::vector<MacroParam> p, std::unique_ptr<Block> b)
    : name(std::move(n)), params(std::move(p)), body(std::move(b)) {}
void MacroDeclaration::accept(Visitor& v) { v.visit(*this); }

ConstructorDeclaration::ConstructorDeclaration(std::string n, std::vector<std::unique_ptr<Parameter>> p, std::unique_ptr<Block> b, std::unique_ptr<TypeNode> rt)
    : name(std::move(n)), params(std::move(p)), body(std::move(b)), return_type(std::move(rt)) {}
void ConstructorDeclaration::accept(Visitor& v) { v.visit(*this); }

DestructorDeclaration::DestructorDeclaration(std::string n, std::unique_ptr<Block> b)
    : name(std::move(n)), body(std::move(b)) {}
void DestructorDeclaration::accept(Visitor& v) { v.visit(*this); }

Program::Program(std::vector<std::unique_ptr<Statement>> s) : statements(std::move(s)) {}
void Program::accept(Visitor& v) { v.visit(*this); }

} // namespace fin