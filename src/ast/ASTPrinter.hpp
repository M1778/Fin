#pragma once

#include "ASTNode.hpp"
#include <iostream>
#include <string>

namespace fin {

class ASTPrinter {
public:
    void print(const ASTNode& node);

private:
    void printNode(const ASTNode* node, std::string prefix, bool isLast);
    void dispatch(const ASTNode* node, std::string currentPrefix, std::string childPrefix);
    
    // Declarations
    void printProgram(const Program* node, std::string prefix, bool isLast);
    void printFunction(const FunctionDeclaration* node, std::string prefix, bool isLast);
    void printVarDecl(const VariableDeclaration* node, std::string prefix, bool isLast);
    void printStruct(const StructDeclaration* node, std::string prefix, bool isLast);
    void printInterface(const InterfaceDeclaration* node, std::string prefix, bool isLast);
    void printEnum(const EnumDeclaration* node, std::string prefix, bool isLast);
    void printDefine(const DefineDeclaration* node, std::string prefix, bool isLast);
    void printMacro(const MacroDeclaration* node, std::string prefix, bool isLast);
    void printOperator(const OperatorDeclaration* node, std::string prefix, bool isLast);
    void printImport(const ImportModule* node, std::string prefix, bool isLast);
    void printConstructor(const ConstructorDeclaration* node, std::string prefix, bool isLast);
    void printDestructor(const DestructorDeclaration* node, std::string prefix, bool isLast);
    
    void printParameter(const Parameter* node, std::string prefix, bool isLast);
    void printStructMember(const StructMember* node, std::string prefix, bool isLast);

    // Statements
    void printBlock(const Block* node, std::string prefix, bool isLast);
    void printIf(const IfStatement* node, std::string prefix, bool isLast);
    void printReturn(const ReturnStatement* node, std::string prefix, bool isLast);
    void printExprStmt(const ExpressionStatement* node, std::string prefix, bool isLast);
    void printForeach(const ForeachLoop* node, std::string prefix, bool isLast);
    void printBreak(const BreakStatement* node, std::string prefix, bool isLast);
    void printContinue(const ContinueStatement* node, std::string prefix, bool isLast);
    void printDelete(const DeleteStatement* node, std::string prefix, bool isLast);
    void printTryCatch(const TryCatch* node, std::string prefix, bool isLast);
    void printBlame(const BlameStatement* node, std::string prefix, bool isLast);

    // Expressions
    void printBinary(const BinaryOp* node, std::string prefix, bool isLast);
    void printUnary(const UnaryOp* node, std::string prefix, bool isLast);
    void printLiteral(const Literal* node, std::string prefix, bool isLast);
    void printIdentifier(const Identifier* node, std::string prefix, bool isLast);
    void printCall(const FunctionCall* node, std::string prefix, bool isLast);
    void printMethodCall(const MethodCall* node, std::string prefix, bool isLast);
    void printMacroCall(const MacroCall* node, std::string prefix, bool isLast);
    void printMacroInvocation(const MacroInvocation* node, std::string prefix, bool isLast);
    void printCast(const CastExpression* node, std::string prefix, bool isLast);
    void printNew(const NewExpression* node, std::string prefix, bool isLast);
    void printMemberAccess(const MemberAccess* node, std::string prefix, bool isLast);
    void printStructInit(const StructInstantiation* node, std::string prefix, bool isLast);
    void printArrayLiteral(const ArrayLiteral* node, std::string prefix, bool isLast);
    void printArrayAccess(const ArrayAccess* node, std::string prefix, bool isLast);
    void printSizeof(const SizeofExpression* node, std::string prefix, bool isLast);
    void printTernary(const TernaryOp* node, std::string prefix, bool isLast);
    void printFunctionType(const FunctionTypeNode* node, std::string prefix, bool isLast);
    void printLambda(const LambdaExpression* node, std::string prefix, bool isLast);
    void printQuote(const QuoteExpression* node, std::string prefix, bool isLast);
    void printTypeNode(const TypeNode* node, std::string prefix, bool isLast);
    void printSuper(const SuperExpression* node, std::string prefix, bool isLast);
    void printPointerType(const PointerTypeNode* node, std::string prefix, bool isLast);
    void printArrayType(const ArrayTypeNode* node, std::string prefix, bool isLast);
};

} // namespace fin