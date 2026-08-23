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
class TypeDefinition;
class SpecialDeclaration;
class ImplementsBlock;
class ClassDeclaration;
class Parameter;
class StructMember;
class Attribute;
class GenericParam;

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
class PrototypeLiteral;

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
    virtual void visit(TypeDefinition& node) = 0;
    virtual void visit(SpecialDeclaration& node) = 0;
    virtual void visit(ClassDeclaration& node) = 0;
    virtual void visit(ImplementsBlock& node) = 0;
    
    // Helpers
    virtual void visit(Parameter& node) = 0;     
    virtual void visit(StructMember& node) = 0;   

    // Attribute and GenericParam are ASTNodes and are cloned like every other
    // node, but they had no overload here at all -- so their `accept` bodies were
    // empty, `CloneVisitor::clone` returned null for them, and cloning a generic
    // declaration replaced its attributes and generic parameters with a vector of
    // the right length full of nulls (ADR 0004).  This is the root: dispatch, not
    // a special case in CloneVisitor.
    //
    // These two are the only non-pure methods on Visitor.  The other 55 are pure
    // because exhaustiveness is what Visitor is *for*; these two are not, because
    // promoting them would force an override into SemanticAnalyzer,
    // MacroExpander and SubstitutionVisitor, none of which reads attributes and
    // two of which are another agent's files.  A consumer that must be
    // exhaustive over attributes overrides them; the compiler will not insist.
    // Promoting them later is a three-line change.
    virtual void visit(Attribute& node) { (void)node; }
    virtual void visit(GenericParam& node) { (void)node; }

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
    virtual void visit(PrototypeLiteral& node) = 0;
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