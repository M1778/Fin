// =============================================================================
// Fin Programming Language Compiler
//
// Made with ❤️ for someone special.
//
// This project is genuinely built on love and dedication, a way to express your love can be done through Fin.
// Fin is a semi-object-orientated programming language that specialized in control, giving so much control to the user while asking for the bare minimum.
// Fin is the name of the Angel from BlackNightmare-Stories not yet published.
// “Love is the reason this project exists”
// -----------------------------------------------------------------------------
// Author: M1778
// Repository: https://github.com/M1778/Fin
// Profile: https://github.com/M1778M/
// -----------------------------------------------------------------------------
// Copyright (C) 2027 M1778
//
// This file is part of the Fin Programming Language Compiler.
//
// Fin is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Fin is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Fin.  If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================

#pragma once

#include "ASTNode.hpp"
#include <iostream>
#include <string>

namespace fin {

class PrototypeLiteral;

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
    void printTypeDefinition(const TypeDefinition* node, std::string prefix, bool isLast);
    void printSpecialDeclaration(const SpecialDeclaration* node, std::string prefix, bool isLast);
    void printClass(const ClassDeclaration* node, std::string prefix, bool isLast);
    
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
    void printPrototype(const PrototypeLiteral* node, std::string prefix, bool isLast);
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
    void printMatch(const MatchExpr* node, std::string prefix, bool isLast);
    void printFunctionType(const FunctionTypeNode* node, std::string prefix, bool isLast);
    void printLambda(const LambdaExpression* node, std::string prefix, bool isLast);
    void printQuote(const QuoteExpression* node, std::string prefix, bool isLast);
    void printTypeNode(const TypeNode* node, std::string prefix, bool isLast);
    void printSuper(const SuperExpression* node, std::string prefix, bool isLast);
    void printPointerType(const PointerTypeNode* node, std::string prefix, bool isLast);
    void printArrayType(const ArrayTypeNode* node, std::string prefix, bool isLast);
    void printStaticMethodCall(const StaticMethodCall* node, std::string prefix, bool isLast);
};

} // namespace fin