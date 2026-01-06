#include "ASTPrinter.hpp"
#include <fmt/core.h>
#include <fmt/color.h>

namespace fin {

// Helper to recursively print AST TypeNodes
std::string astTypeToString(const TypeNode* type) {
    if (!type) return "unknown";
    std::string s = type->name;
    
    if (!type->generics.empty()) {
        s += "<";
        for (size_t i = 0; i < type->generics.size(); ++i) {
            s += astTypeToString(type->generics[i].get());
            if (i < type->generics.size() - 1) s += ", ";
        }
        s += ">";
    }
    
    // Handle pointers and arrays
    for(int i=0; i<type->pointer_depth; ++i) {
        s = "&" + s;
    }
    
    if (type->is_array) s = "[" + s + "]";
    
    return s;
}

void ASTPrinter::print(const ASTNode& node) {
    printNode(&node, "", true);
}

void ASTPrinter::printNode(const ASTNode* node, std::string prefix, bool isLast) {
    if (!node) return;

    // Tree formatting
    std::string marker = isLast ? "└── " : "├── ";
    std::string childPrefix = prefix + (isLast ? "    " : "│   ");

    // Dispatch to specific handler
    dispatch(node, prefix + marker, childPrefix);
}

void ASTPrinter::dispatch(const ASTNode* node, std::string currentPrefix, std::string childPrefix) {
    // --- Top Level ---
    if (auto* n = dynamic_cast<const Program*>(node)) {
        printProgram(n, currentPrefix, true); 
    }
    else if (auto* n = dynamic_cast<const FunctionDeclaration*>(node)) {
        printFunction(n, currentPrefix, false); 
    }
    else if (auto* n = dynamic_cast<const StructDeclaration*>(node)) {
        printStruct(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const InterfaceDeclaration*>(node)) {
        printInterface(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const EnumDeclaration*>(node)) {
        printEnum(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const DefineDeclaration*>(node)) {
        printDefine(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const MacroDeclaration*>(node)) {
        printMacro(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const OperatorDeclaration*>(node)) {
        printOperator(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const ImportModule*>(node)) {
        printImport(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const ConstructorDeclaration*>(node)) {
        printConstructor(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const DestructorDeclaration*>(node)) {
        printDestructor(n, currentPrefix, false);
    }
    // --- Helpers ---
    else if (auto* n = dynamic_cast<const Parameter*>(node)) {
        printParameter(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const StructMember*>(node)) {
        printStructMember(n, currentPrefix, false);
    }
    // --- Statements ---
    else if (auto* n = dynamic_cast<const VariableDeclaration*>(node)) {
        printVarDecl(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const Block*>(node)) {
        printBlock(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const IfStatement*>(node)) {
        printIf(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const ReturnStatement*>(node)) {
        printReturn(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const ExpressionStatement*>(node)) {
        printExprStmt(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const ForeachLoop*>(node)) {
        printForeach(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const BreakStatement*>(node)) {
        printBreak(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const ContinueStatement*>(node)) {
        printContinue(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const DeleteStatement*>(node)) {
        printDelete(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const TryCatch*>(node)) {
        printTryCatch(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const BlameStatement*>(node)) {
        printBlame(n, currentPrefix, false);
    }
    // --- Expressions ---
    else if (auto* n = dynamic_cast<const BinaryOp*>(node)) {
        printBinary(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const UnaryOp*>(node)) {
        printUnary(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const Literal*>(node)) {
        printLiteral(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const Identifier*>(node)) {
        printIdentifier(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const FunctionCall*>(node)) {
        printCall(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const MethodCall*>(node)) {
        printMethodCall(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const MacroCall*>(node)) {
        printMacroCall(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const MacroInvocation*>(node)) {
        printMacroInvocation(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const CastExpression*>(node)) {
        printCast(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const NewExpression*>(node)) {
        printNew(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const MemberAccess*>(node)) {
        printMemberAccess(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const StructInstantiation*>(node)) {
        printStructInit(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const ArrayLiteral*>(node)) {
        printArrayLiteral(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const ArrayAccess*>(node)) {
        printArrayAccess(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const SizeofExpression*>(node)) {
        printSizeof(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const TernaryOp*>(node)) {
        printTernary(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const FunctionTypeNode*>(node)) {
        printFunctionType(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const LambdaExpression*>(node)) {
        printLambda(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const QuoteExpression*>(node)) {
        printQuote(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const TypeNode*>(node)) {
        printTypeNode(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const SuperExpression*>(node)) {
        printSuper(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const PointerTypeNode*>(node)) {
        printPointerType(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const ArrayTypeNode*>(node)) {
        printArrayType(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const StaticMethodCall*>(node)) {
        printStaticMethodCall(n, currentPrefix, false);
    }
    else {
        fmt::print("{}Unknown Node\n", currentPrefix);
    }
}

// ============================================================================
// Implementations
// ============================================================================

void ASTPrinter::printProgram(const Program* node, std::string prefix, bool) {
    fmt::print("{}Program\n", prefix);
    for (size_t i = 0; i < node->statements.size(); ++i) {
        printNode(node->statements[i].get(), "", i == node->statements.size() - 1); 
    }
}

void ASTPrinter::printFunction(const FunctionDeclaration* node, std::string prefix, bool) {
    fmt::print(fg(fmt::color::cornflower_blue), "{}FunctionDecl ", prefix);
    fmt::print("'{}'", node->name);
    
    if (!node->generic_params.empty()) {
        fmt::print("<");
        for (size_t i = 0; i < node->generic_params.size(); ++i) {
            auto& g = node->generic_params[i];
            fmt::print("{}", g->name);
            if (g->constraint) fmt::print(": {}", g->constraint->name);
            if (i < node->generic_params.size() - 1) fmt::print(", ");
        }
        fmt::print(">");
    }
    if (node->return_type) {
        fmt::print(" -> {}", astTypeToString(node->return_type.get()));
    }
    
    fmt::print("\n");
    
    if (node->body) {
        printNode(node->body.get(), "    ", true);
    } else {
        fmt::print("{}    (Abstract)\n", prefix);
    }
}

void ASTPrinter::printVarDecl(const VariableDeclaration* node, std::string prefix, bool) {
    fmt::print(fg(fmt::color::light_green), "{}VarDecl ", prefix);
    fmt::print("{} <{}>\n", node->name, astTypeToString(node->type.get()));
    if (node->initializer) {
        printNode(node->initializer.get(), "    ", true);
    }
}

void ASTPrinter::printStruct(const StructDeclaration* node, std::string prefix, bool) {
    fmt::print(fg(fmt::color::orange), "{}Struct ", prefix);
    fmt::print("{}", node->name);
    if (!node->parents.empty()) {
        fmt::print(" : ");
        for (size_t i = 0; i < node->parents.size(); ++i) {
            fmt::print("{}", astTypeToString(node->parents[i].get()));
            if (i < node->parents.size() - 1) fmt::print(", ");
        }
    }
    fmt::print("\n");
    
    for (auto& attr : node->attributes) {
        fmt::print("{}  #[{}]\n", prefix, attr->name);
    }

    for (auto& member : node->members) {
        fmt::print("{}  Member: {} <{}>\n", prefix, member->name, astTypeToString(member->type.get()));
    }
    
    for (size_t i = 0; i < node->methods.size(); ++i) {
        printNode(node->methods[i].get(), prefix + "    ", false);
    }
    
    for (auto& ctor : node->constructors) {
        printNode(ctor.get(), prefix + "    ", false);
    }
    if (node->destructor) {
        printNode(node->destructor.get(), prefix + "    ", false);
    }
}

void ASTPrinter::printInterface(const InterfaceDeclaration* node, std::string prefix, bool isLast) {
    fmt::print(fg(fmt::color::magenta), "{}Interface ", prefix);
    fmt::print("{}", node->name);
    if (!node->generic_params.empty()) {
        fmt::print("<");
        for (size_t i = 0; i < node->generic_params.size(); ++i) {
            fmt::print("{}", node->generic_params[i]->name);
            if (node->generic_params[i]->constraint) {
                fmt::print(": {}", node->generic_params[i]->constraint->name);
            }
            if (i < node->generic_params.size() - 1) fmt::print(", ");
        }
        fmt::print(">");
    }
    fmt::print("\n");

    for (auto& member : node->members) {
        fmt::print("{}  Member: {} <{}>\n", prefix, member->name, astTypeToString(member->type.get()));
    }
    for (auto& ctor : node->constructors) {
        fmt::print(fg(fmt::color::cyan), "{}  Abstract Constructor: Self(...)\n", prefix);
    }
    if (node->destructor) {
        fmt::print(fg(fmt::color::cyan), "{}  Abstract Destructor: ~Self()\n", prefix);
    }
    for (auto& method : node->methods) {
        fmt::print(fg(fmt::color::cornflower_blue), "{}  Abstract Method: {}\n", prefix, method->name);
    }
    for (auto& op : node->operators) {
        fmt::print(fg(fmt::color::cyan), "{}  Abstract Operator: {}\n", prefix, (int)op->op);
    }
}

void ASTPrinter::printEnum(const EnumDeclaration* node, std::string prefix, bool isLast) {
    fmt::print(fg(fmt::color::yellow), "{}Enum '{}'\n", prefix, node->name);
    for(auto& v : node->values) {
        fmt::print("{}    Value: {}\n", prefix, v.first);
    }
}

void ASTPrinter::printDefine(const DefineDeclaration* node, std::string prefix, bool isLast) {
    fmt::print(fg(fmt::color::magenta), "{}Define ", prefix);
    fmt::print("'{}'", node->name);
    if (node->is_vararg) fmt::print(" (vararg)");
    fmt::print("\n");
}

void ASTPrinter::printMacro(const MacroDeclaration* node, std::string prefix, bool isLast) {
    fmt::print(fg(fmt::color::magenta), "{}Macro ", prefix);
    fmt::print("'{}'\n", node->name);
    for (const auto& param : node->params) {
        fmt::print("{}    Param: {}: {}{}\n", prefix, param.name, param.type, param.is_vararg ? "..." : "");
    }
    printNode(node->body.get(), prefix + "    ", true);
}

void ASTPrinter::printOperator(const OperatorDeclaration* node, std::string prefix, bool isLast) {
    fmt::print("{}Operator\n", prefix);
    printNode(node->body.get(), prefix + "    ", true);
}

void ASTPrinter::printImport(const ImportModule* node, std::string prefix, bool isLast) {
    fmt::print(fg(fmt::color::blue), "{}Import '{}'\n", prefix, node->source);
}

void ASTPrinter::printConstructor(const ConstructorDeclaration* node, std::string prefix, bool isLast) {
    fmt::print(fg(fmt::color::cyan), "{}Constructor '{}'\n", prefix, node->name);
    printNode(node->body.get(), prefix + "    ", true);
}

void ASTPrinter::printDestructor(const DestructorDeclaration* node, std::string prefix, bool isLast) {
    fmt::print(fg(fmt::color::cyan), "{}Destructor '~{}'\n", prefix, node->name);
    printNode(node->body.get(), prefix + "    ", true);
}

void ASTPrinter::printParameter(const Parameter* node, std::string prefix, bool isLast) {
    fmt::print("{}Param: {} <{}>\n", prefix, node->name, astTypeToString(node->type.get()));
    if (node->default_value) {
        printNode(node->default_value.get(), prefix + "    ", true);
    }
}

void ASTPrinter::printStructMember(const StructMember* node, std::string prefix, bool isLast) {
    fmt::print("{}Member: {} <{}>\n", prefix, node->name, astTypeToString(node->type.get()));
    if (node->default_value) {
        printNode(node->default_value.get(), prefix + "    ", true);
    }
}

void ASTPrinter::printBlock(const Block* node, std::string prefix, bool) {
    fmt::print("{}Block\n", prefix);
    for (size_t i = 0; i < node->statements.size(); ++i) {
        printNode(node->statements[i].get(), "    ", i == node->statements.size() - 1);
    }
}

void ASTPrinter::printIf(const IfStatement* node, std::string prefix, bool) {
    fmt::print("{}If\n", prefix);
    printNode(node->condition.get(), "    ", false);
    printNode(node->then_block.get(), "    ", node->else_stmt == nullptr);
    if (node->else_stmt) {
        printNode(node->else_stmt.get(), "    ", true);
    }
}

void ASTPrinter::printReturn(const ReturnStatement* node, std::string prefix, bool) {
    fmt::print(fg(fmt::color::red), "{}Return\n", prefix);
    if (node->value) printNode(node->value.get(), "    ", true);
}

void ASTPrinter::printExprStmt(const ExpressionStatement* node, std::string prefix, bool) {
    fmt::print("{}ExprStmt\n", prefix);
    printNode(node->expr.get(), "    ", true);
}

void ASTPrinter::printForeach(const ForeachLoop* node, std::string prefix, bool isLast) {
    fmt::print("{}Foreach\n", prefix);
    printNode(node->body.get(), prefix + "    ", true);
}

void ASTPrinter::printBreak(const BreakStatement* node, std::string prefix, bool isLast) {
    fmt::print("{}Break\n", prefix);
}

void ASTPrinter::printContinue(const ContinueStatement* node, std::string prefix, bool isLast) {
    fmt::print("{}Continue\n", prefix);
}

void ASTPrinter::printDelete(const DeleteStatement* node, std::string prefix, bool isLast) {
    fmt::print("{}Delete\n", prefix);
    printNode(node->expr.get(), prefix + "    ", true);
}

void ASTPrinter::printTryCatch(const TryCatch* node, std::string prefix, bool isLast) {
    fmt::print("{}TryCatch\n", prefix);
    printNode(node->try_block.get(), prefix + "    ", false);
    printNode(node->catch_block.get(), prefix + "    ", true);
}

void ASTPrinter::printBlame(const BlameStatement* node, std::string prefix, bool isLast) {
    fmt::print("{}Blame\n", prefix);
    printNode(node->error_expr.get(), prefix + "    ", true);
}

void ASTPrinter::printBinary(const BinaryOp* node, std::string prefix, bool) {
    fmt::print("{}BinaryOp\n", prefix);
    printNode(node->left.get(), "    ", false);
    printNode(node->right.get(), "    ", true);
}

void ASTPrinter::printUnary(const UnaryOp* node, std::string prefix, bool isLast) {
    std::string opStr;
    switch(node->op) {
        case ASTTokenKind::MINUS: opStr = "-"; break;
        case ASTTokenKind::NOT: opStr = "!"; break;
        case ASTTokenKind::AMPERSAND: opStr = "&"; break;
        case ASTTokenKind::MULT: opStr = "*"; break;
        case ASTTokenKind::INCREMENT: opStr = "++"; break;
        case ASTTokenKind::DECREMENT: opStr = "--"; break;
        default: opStr = "?"; break;
    }
    fmt::print("{}UnaryOp '{}'\n", prefix, opStr);
    printNode(node->operand.get(), prefix + "    ", true);
}

void ASTPrinter::printLiteral(const Literal* node, std::string prefix, bool) {
    fmt::print(fg(fmt::color::yellow), "{}Literal ", prefix);
    fmt::print("{}\n", node->value);
}

void ASTPrinter::printIdentifier(const Identifier* node, std::string prefix, bool) {
    fmt::print("{}ID '{}'\n", prefix, node->name);
}

void ASTPrinter::printCall(const FunctionCall* node, std::string prefix, bool) {
    fmt::print("{}Call '{}'", prefix, node->name);
    if (!node->generic_args.empty()) {
        fmt::print("::<");
        for (size_t i = 0; i < node->generic_args.size(); ++i) {
            fmt::print("{}", node->generic_args[i]->name);
            if (i < node->generic_args.size() - 1) fmt::print(", ");
        }
        fmt::print(">");
    }
    fmt::print("\n");
    for (auto& arg : node->args) {
        printNode(arg.get(), "    ", false);
    }
}

void ASTPrinter::printMethodCall(const MethodCall* node, std::string prefix, bool isLast) {
    fmt::print("{}MethodCall '{}'\n", prefix, node->method_name);
    fmt::print("{}  Object:\n", prefix);
    printNode(node->object.get(), prefix + "    ", false);
    fmt::print("{}  Args:\n", prefix);
    for (auto& arg : node->args) {
        printNode(arg.get(), prefix + "    ", false);
    }
}

void ASTPrinter::printMacroCall(const MacroCall* node, std::string prefix, bool isLast) {
    fmt::print("{}MacroCall '{}'\n", prefix, node->name);
}

void ASTPrinter::printMacroInvocation(const MacroInvocation* node, std::string prefix, bool isLast) {
    fmt::print(fg(fmt::color::magenta), "{}MacroInvocation ", prefix);
    fmt::print("'{}!'\n", node->name);
    for (auto& arg : node->args) {
        printNode(arg.get(), prefix + "    ", false);
    }
}

void ASTPrinter::printCast(const CastExpression* node, std::string prefix, bool isLast) {
    fmt::print("{}Cast\n", prefix);
    printNode(node->expr.get(), prefix + "    ", true);
}

void ASTPrinter::printNew(const NewExpression* node, std::string prefix, bool isLast) {
    fmt::print("{}New\n", prefix);
}

void ASTPrinter::printMemberAccess(const MemberAccess* node, std::string prefix, bool isLast) {
    fmt::print("{}MemberAccess '{}'\n", prefix, node->member);
    printNode(node->object.get(), prefix + "    ", true);
}

void ASTPrinter::printStructInit(const StructInstantiation* node, std::string prefix, bool isLast) {
    fmt::print("{}StructInit '{}'", prefix, node->struct_name);
    if (!node->generic_args.empty()) {
        fmt::print("::<");
        for (size_t i = 0; i < node->generic_args.size(); ++i) {
            fmt::print("{}", node->generic_args[i]->name);
            if (i < node->generic_args.size() - 1) fmt::print(", ");
        }
        fmt::print(">");
    }
    fmt::print("\n");
    for (auto& field : node->fields) {
        fmt::print("{}    Field: {}\n", prefix, field.first);
        printNode(field.second.get(), prefix + "        ", true);
    }
}

void ASTPrinter::printArrayLiteral(const ArrayLiteral* node, std::string prefix, bool isLast) {
    fmt::print("{}ArrayLiteral\n", prefix);
    for (size_t i = 0; i < node->elements.size(); ++i) {
        printNode(node->elements[i].get(), prefix + "    ", i == node->elements.size() - 1);
    }
}

void ASTPrinter::printArrayAccess(const ArrayAccess* node, std::string prefix, bool isLast) {
    fmt::print("{}ArrayAccess\n", prefix);
    fmt::print("{}  Array:\n", prefix);
    printNode(node->array.get(), prefix + "    ", false);
    fmt::print("{}  Index:\n", prefix);
    printNode(node->index.get(), prefix + "    ", true);
}

void ASTPrinter::printSizeof(const SizeofExpression* node, std::string prefix, bool isLast) {
    fmt::print("{}Sizeof\n", prefix);
}

void ASTPrinter::printTernary(const TernaryOp* node, std::string prefix, bool isLast) {
    fmt::print("{}Ternary\n", prefix);
    printNode(node->condition.get(), prefix + "    ", false);
    printNode(node->true_expr.get(), prefix + "    ", false);
    printNode(node->false_expr.get(), prefix + "    ", true);
}

void ASTPrinter::printFunctionType(const FunctionTypeNode* node, std::string prefix, bool isLast) {
    fmt::print("{}FunctionType\n", prefix);
    fmt::print("{}  Params:\n", prefix);
    for(auto& p : node->param_types) {
        fmt::print("{}    {}\n", prefix, astTypeToString(p.get()));
    }
    fmt::print("{}  Return: {}\n", prefix, astTypeToString(node->return_type.get()));
}

void ASTPrinter::printLambda(const LambdaExpression* node, std::string prefix, bool isLast) {
    fmt::print(fg(fmt::color::cyan), "{}Lambda\n", prefix);
    for(auto& p : node->params) {
        fmt::print("{}  Param: {} <{}>\n", prefix, p->name, astTypeToString(p->type.get()));
    }
    if(node->body) {
        printNode(node->body.get(), prefix + "  ", true);
    } else {
        printNode(node->expression_body.get(), prefix + "  ", true);
    }
}

void ASTPrinter::printQuote(const QuoteExpression* node, std::string prefix, bool isLast) {
    fmt::print(fg(fmt::color::magenta), "{}Quote\n", prefix);
    printNode(node->block.get(), prefix + "    ", true);
}

void ASTPrinter::printTypeNode(const TypeNode* node, std::string prefix, bool isLast) {
    fmt::print("{}TypeNode '{}'\n", prefix, astTypeToString(node));
}

void ASTPrinter::printSuper(const SuperExpression* node, std::string prefix, bool isLast) {
    if (!node->init_fields.empty()) {
        fmt::print("{}SuperInit\n", prefix);
        for (auto& f : node->init_fields) {
            fmt::print("{}    Field: {}\n", prefix, f.first);
            printNode(f.second.get(), prefix + "        ", true);
        }
    } else {
        fmt::print("{}SuperCall '{}'\n", prefix, node->parent_name.empty() ? "implicit" : node->parent_name);
        for (auto& arg : node->args) {
            printNode(arg.get(), prefix + "    ", false);
        }
    }
}

void ASTPrinter::printPointerType(const PointerTypeNode* node, std::string prefix, bool isLast) {
    fmt::print("{}PointerType\n", prefix);
    printNode(node->pointee.get(), prefix + "    ", true);
}

void ASTPrinter::printArrayType(const ArrayTypeNode* node, std::string prefix, bool isLast) {
    fmt::print("{}ArrayType\n", prefix);
    printNode(node->element_type.get(), prefix + "    ", node->size == nullptr);
    if(node->size) printNode(node->size.get(), prefix + "    ", true);
}

void ASTPrinter::printStaticMethodCall(const StaticMethodCall* node, std::string prefix, bool) {
    fmt::print("{}StaticCall\n", prefix);
    fmt::print("{}  Type: {}\n", prefix, astTypeToString(node.target_type.get()));
    fmt::print("{}  Method: {}\n", prefix, node.method_name);
    for (auto& arg : node.args) {
        printNode(arg.get(), prefix + "    ", false);
    }
}


} // namespace fin
