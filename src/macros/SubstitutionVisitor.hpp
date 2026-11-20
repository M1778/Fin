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

#include "../ast/Visitor.hpp"
#include "../ast/ASTNode.hpp"
#include <unordered_map>
#include <string>
#include <memory>

namespace fin {

class SubstitutionVisitor : public Visitor {
public:
    SubstitutionVisitor(const std::unordered_map<std::string, std::unique_ptr<Expression>>& args);

    void substitute(ASTNode* node);
    std::unique_ptr<Expression> replacementExpr;

    // --- Visitor Implementation ---
    void visit(Program& node) override;
    void visit(Block& node) override;
    void visit(VariableDeclaration& node) override;
    void visit(ReturnStatement& node) override;
    void visit(ExpressionStatement& node) override;
    void visit(IfStatement& node) override;
    void visit(WhileLoop& node) override;
    void visit(ForLoop& node) override;
    void visit(ForeachLoop& node) override;
    void visit(DeleteStatement& node) override;
    void visit(BlameStatement& node) override;
    
    void visit(TypeDefinition&) override {}
    void visit(SpecialDeclaration&) override {}
    void visit(ClassDeclaration&) override {}
    void visit(ImplementsBlock&) override {}
    void visit(PrototypeLiteral& node) override;
    
    // Expressions
    void visit(BinaryOp& node) override;
    void visit(UnaryOp& node) override;
    void visit(FunctionCall& node) override;
    void visit(MethodCall& node) override;
    void visit(CastExpression& node) override;
    void visit(TypeLiteralExpression& node) override;
    void visit(NewExpression& node) override;
    void visit(MemberAccess& node) override;
    void visit(StructInstantiation& node) override;
    void visit(ArrayLiteral& node) override;
    void visit(ArrayAccess& node) override;
    void visit(TernaryOp& node) override;
    void visit(MatchExpr& node) override;
    void visit(LambdaExpression& node) override;
    void visit(Identifier& node) override;
    
    void visit(SuperExpression& node) override;
    void visit(ConstructorDeclaration& node) override;
    void visit(DestructorDeclaration& node) override;

    
    void visit(FunctionDeclaration&) override {}
    void visit(StructDeclaration&) override {}
    void visit(InterfaceDeclaration&) override {}
    void visit(EnumDeclaration&) override {}
    void visit(DefineDeclaration&) override {}
    void visit(MacroDeclaration&) override {}
    void visit(OperatorDeclaration&) override {}
    void visit(ImportModule&) override {}
    void visit(BreakStatement&) override {}
    void visit(ContinueStatement&) override {}
    void visit(TryCatch&) override {}
    void visit(Literal&) override {}
    void visit(MacroCall&) override {}
    void visit(MacroInvocation&) override {}
    void visit(QuoteExpression&) override {}
    void visit(SizeofExpression&) override {}
    void visit(FunctionTypeNode&) override {}
    void visit(TypeNode&) override {}
    void visit(Parameter& node) override;
    void visit(StructMember& node) override;
    void visit(PointerTypeNode& node) override;
    void visit(ArrayTypeNode& node) override;
    void visit(StaticMethodCall& node) override;

private:
    const std::unordered_map<std::string, std::unique_ptr<Expression>>& args;
    std::unique_ptr<Expression> cloneArg(const std::string& name);
};

}