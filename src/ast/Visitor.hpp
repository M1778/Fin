#pragma once

#include "ASTNode.hpp"
namespace fin {

// Forward declarations
class Program;
class FunctionDeclaration;
class VariableDeclaration;
class StructDeclaration;
class InterfaceDeclaration;
class EnumDeclaration;
class DefineDeclaration;
class MacroDeclaration;
class OperatorDeclaration;
class ImportModule;
class ConstructorDeclaration;
class DestructorDeclaration;

class Block;
class ReturnStatement;
class ExpressionStatement;
class IfStatement;
class WhileLoop;
class ForLoop;
class ForeachLoop;
class BreakStatement;
class ContinueStatement;
class DeleteStatement;
class TryCatch;
class BlameStatement;

class BinaryOp;
class UnaryOp;
class Literal;
class Identifier;
class FunctionCall;
class MethodCall;
class MacroCall;
class MacroInvocation;
class QuoteExpression;
class CastExpression;
class NewExpression;
class MemberAccess;
class StructInstantiation;
class ArrayLiteral;
class ArrayAccess;
class SizeofExpression;
class TernaryOp;
class FunctionTypeNode;
class LambdaExpression;
class TypeNode;
class SuperExpression;
class PointerTypeNode;
class ArrayTypeNode;
class StaticMethodCall;

// Missing nodes
class Parameter;
class StructMember;

class Visitor {
public:
    virtual ~Visitor() = default;

    // Root
    virtual void visit(Program& node) = 0;

    // Declarations
    virtual void visit(FunctionDeclaration& node) = 0;
    virtual void visit(VariableDeclaration& node) = 0;
    virtual void visit(StructDeclaration& node) = 0;
    virtual void visit(InterfaceDeclaration& node) = 0;
    virtual void visit(EnumDeclaration& node) = 0;
    virtual void visit(DefineDeclaration& node) = 0;
    virtual void visit(MacroDeclaration& node) = 0;
    virtual void visit(OperatorDeclaration& node) = 0;
    virtual void visit(ImportModule& node) = 0;
    virtual void visit(ConstructorDeclaration& node) = 0;
    virtual void visit(DestructorDeclaration& node) = 0;
    
    // Helpers
    virtual void visit(Parameter& node) = 0;     
    virtual void visit(StructMember& node) = 0;   

    // Statements
    virtual void visit(Block& node) = 0;
    virtual void visit(ReturnStatement& node) = 0;
    virtual void visit(ExpressionStatement& node) = 0;
    virtual void visit(IfStatement& node) = 0;
    virtual void visit(WhileLoop& node) = 0;
    virtual void visit(ForLoop& node) = 0;
    virtual void visit(ForeachLoop& node) = 0;
    virtual void visit(BreakStatement& node) = 0;
    virtual void visit(ContinueStatement& node) = 0;
    virtual void visit(DeleteStatement& node) = 0;
    virtual void visit(TryCatch& node) = 0;
    virtual void visit(BlameStatement& node) = 0;

    // Expressions
    virtual void visit(BinaryOp& node) = 0;
    virtual void visit(UnaryOp& node) = 0;
    virtual void visit(Literal& node) = 0;
    virtual void visit(Identifier& node) = 0;
    virtual void visit(FunctionCall& node) = 0;
    virtual void visit(MethodCall& node) = 0;
    virtual void visit(MacroCall& node) = 0;
    virtual void visit(MacroInvocation& node) = 0;
    virtual void visit(QuoteExpression& node) = 0;
    virtual void visit(CastExpression& node) = 0;
    virtual void visit(NewExpression& node) = 0;
    virtual void visit(MemberAccess& node) = 0;
    virtual void visit(StructInstantiation& node) = 0;
    virtual void visit(ArrayLiteral& node) = 0;
    virtual void visit(ArrayAccess& node) = 0;
    virtual void visit(SizeofExpression& node) = 0;
    virtual void visit(TernaryOp& node) = 0;
    virtual void visit(FunctionTypeNode& node) = 0;
    virtual void visit(LambdaExpression& node) = 0;
    virtual void visit(TypeNode& node) = 0;
    virtual void visit(SuperExpression& node) = 0;
    virtual void visit(PointerTypeNode& node) = 0;
    virtual void visit(ArrayTypeNode& node) = 0;
    virtual void visit(StaticMethodCall& node) = 0;
};

} // namespace fin