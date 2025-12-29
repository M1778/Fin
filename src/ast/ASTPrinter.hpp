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
    
    // FIXED: Signature now takes childPrefix (string) instead of isLast (bool)
    void dispatch(const ASTNode* node, std::string currentPrefix, std::string childPrefix);
    
    // Specific printers
    void printProgram(const Program* node, std::string prefix, bool isLast);
    void printFunction(const FunctionDeclaration* node, std::string prefix, bool isLast);
    void printVarDecl(const VariableDeclaration* node, std::string prefix, bool isLast);
    void printStruct(const StructDeclaration* node, std::string prefix, bool isLast);
    void printInterface(const InterfaceDeclaration* node, std::string prefix, bool isLast);
    void printBlock(const Block* node, std::string prefix, bool isLast);
    void printIf(const IfStatement* node, std::string prefix, bool isLast);
    void printReturn(const ReturnStatement* node, std::string prefix, bool isLast);
    void printExprStmt(const ExpressionStatement* node, std::string prefix, bool isLast);
    
    
    // Expressions
    void printBinary(const BinaryOp* node, std::string prefix, bool isLast);
    void printLiteral(const Literal* node, std::string prefix, bool isLast);
    void printIdentifier(const Identifier* node, std::string prefix, bool isLast);
    void printCall(const FunctionCall* node, std::string prefix, bool isLast);
    void printMethodCall(const MethodCall* node, std::string prefix, bool isLast);
};

} // namespace fin