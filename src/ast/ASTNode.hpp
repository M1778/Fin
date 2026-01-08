#pragma once

// Master AST Include
#include "nodes/ASTNode.hpp"
#include "nodes/Parameter.hpp"

#include "types/TypeNode.hpp"
#include "types/GenericParam.hpp"
#include "types/Attribute.hpp"

#include "exprs/Literal.hpp"
#include "exprs/Identifier.hpp"
#include "exprs/BinaryOp.hpp"
#include "exprs/UnaryOp.hpp"
#include "exprs/FunctionCall.hpp"
#include "exprs/StructureExpr.hpp"
#include "exprs/ArrayExpr.hpp"
#include "exprs/Lambda.hpp"
#include "exprs/MiscExpr.hpp"

#include "stmts/Statement.hpp"
#include "stmts/ControlFlow.hpp"
#include "stmts/ErrorHandling.hpp"
#include "stmts/VariableDecl.hpp"
#include "stmts/Import.hpp"

#include "decls/FunctionDecl.hpp"
#include "decls/StructDecl.hpp"
#include "decls/MacroDecl.hpp"
#include "decls/DefineDecl.hpp"
#include "decls/Program.hpp"
#include "decls/TypeDef.hpp"
#include "decls/ClassDecl.hpp"