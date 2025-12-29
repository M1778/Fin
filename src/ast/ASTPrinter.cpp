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
    else if (auto* n = dynamic_cast<const StructInstantiation*>(node)) {
        fmt::print("{}StructInit '{}'", currentPrefix, n->struct_name);
        
        // Print Generics
        if (!n->generic_args.empty()) {
            fmt::print("::<");
            for (size_t i = 0; i < n->generic_args.size(); ++i) {
                fmt::print("{}", n->generic_args[i]->name);
                if (i < n->generic_args.size() - 1) fmt::print(", ");
            }
            fmt::print(">");
        }
        fmt::print("\n");

        for (auto& field : n->fields) {
            fmt::print("{}    Field: {}\n", currentPrefix, field.first);
            printNode(field.second.get(), childPrefix + "    ", true);
        }
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
    // --- Expressions ---
    else if (auto* n = dynamic_cast<const BinaryOp*>(node)) {
        printBinary(n, currentPrefix, false);
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
    else if (auto* n = dynamic_cast<const CastExpression*>(node)) {
    fmt::print("{}Cast\n", currentPrefix);
    printNode(n->expr.get(), childPrefix, true);
    }
    else if (auto* n = dynamic_cast<const NewExpression*>(node)) {
        fmt::print("{}New\n", currentPrefix);
        // Print type and args...
    }
    else if (auto* n = dynamic_cast<const InterfaceDeclaration*>(node)) {
        printInterface(n, currentPrefix, false);
    }
    else if (auto* n = dynamic_cast<const MemberAccess*>(node)) {
        fmt::print("{}MemberAccess '{}'\n", currentPrefix, n->member);
        printNode(n->object.get(), childPrefix, true);
    }
    else if (auto* n = dynamic_cast<const DefineDeclaration*>(node)) {
        fmt::print("{}Define '{}'\n", currentPrefix, n->name);
    }
    else if (auto* n = dynamic_cast<const MacroDeclaration*>(node)) {
        fmt::print("{}Macro '{}'\n", currentPrefix, n->name);
    }
    else if (auto* n = dynamic_cast<const ForeachLoop*>(node)) {
        fmt::print("{}Foreach\n", currentPrefix);
        printNode(n->body.get(), childPrefix, true);
    }
    else if (auto* n = dynamic_cast<const BreakStatement*>(node)) {
        fmt::print("{}Break\n", currentPrefix);
    }
    else if (auto* n = dynamic_cast<const ContinueStatement*>(node)) {
        fmt::print("{}Continue\n", currentPrefix);
    }
    else if (auto* n = dynamic_cast<const DeleteStatement*>(node)) {
        fmt::print("{}Delete\n", currentPrefix);
        printNode(n->expr.get(), childPrefix, true);
    }
    else if (auto* n = dynamic_cast<const OperatorDeclaration*>(node)) {
        fmt::print("{}Operator\n", currentPrefix);
        printNode(n->body.get(), childPrefix, true);
    }
    else if (auto* n = dynamic_cast<const MethodCall*>(node)) {
        printMethodCall(n, currentPrefix, false);
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
    fmt::print("\n");
    
    if (node->body) {
        printNode(node->body.get(), "    ", true);
    } else {
        fmt::print("{}    (Abstract)\n", prefix);
    }
}

void ASTPrinter::printVarDecl(const VariableDeclaration* node, std::string prefix, bool) {
    fmt::print(fg(fmt::color::light_green), "{}VarDecl ", prefix);
    // Use the helper here:
    fmt::print("{} <{}>\n", node->name, astTypeToString(node->type.get()));
    if (node->initializer) {
        printNode(node->initializer.get(), "    ", true);
    }
}

// Update printStruct to show methods and attributes:
void ASTPrinter::printStruct(const StructDeclaration* node, std::string prefix, bool) {
    fmt::print(fg(fmt::color::orange), "{}Struct ", prefix);
    fmt::print("{}\n", node->name);
    
    // 1. Print Attributes
    for (auto& member : node->members) {
        fmt::print("{}  Member: {} <{}>\n", prefix, member->name, astTypeToString(member->type.get()));
    }

    // 2. Print Members
    for (auto& member : node->members) {
        fmt::print("{}  Member: {} <{}>\n", prefix, member->name, astTypeToString(member->type.get()));
    }
    
    // 3. Print Methods (Recursively!)
    for (size_t i = 0; i < node->methods.size(); ++i) {
        // We treat methods as children of the struct
        // We use a simple indent here to keep the tree visual
        // In a perfect implementation, we'd pass the tree lines down, 
        // but adding "    " works well enough for visualization.
        printNode(node->methods[i].get(), prefix + "    ", i == node->methods.size() - 1);
    }
}

void ASTPrinter::printInterface(const InterfaceDeclaration* node, std::string prefix, bool) {
    fmt::print(fg(fmt::color::magenta), "{}Interface ", prefix);
    fmt::print("{}\n", node->name);

    for (auto& member : node->members) {
        fmt::print("{}  Member: {} <{}>\n", prefix, member->name, member->type->name);
    }
    
    for (auto& method : node->methods) {
        fmt::print(fg(fmt::color::cornflower_blue), "{}  Abstract Method: ", prefix);
        fmt::print("{}\n", method->name);
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
    // FIXED: Use comma syntax for color
    fmt::print(fg(fmt::color::red), "{}Return\n", prefix);
    if (node->value) printNode(node->value.get(), "    ", true);
}

void ASTPrinter::printExprStmt(const ExpressionStatement* node, std::string prefix, bool) {
    fmt::print("{}ExprStmt\n", prefix);
    printNode(node->expr.get(), "    ", true);
}

void ASTPrinter::printBinary(const BinaryOp* node, std::string prefix, bool) {
    fmt::print("{}BinaryOp\n", prefix);
    printNode(node->left.get(), "    ", false);
    printNode(node->right.get(), "    ", true);
}

void ASTPrinter::printLiteral(const Literal* node, std::string prefix, bool) {
    // FIXED: Use comma syntax for color
    fmt::print(fg(fmt::color::yellow), "{}Literal ", prefix);
    fmt::print("{}\n", node->value);
}

void ASTPrinter::printMethodCall(const MethodCall* node, std::string prefix, bool) {
    fmt::print("{}MethodCall '{}'\n", prefix, node->method_name);
    fmt::print("{}  Object:\n", prefix);
    printNode(node->object.get(), prefix + "    ", false);
    fmt::print("{}  Args:\n", prefix);
    for (auto& arg : node->args) {
        printNode(arg.get(), prefix + "    ", false);
    }
}

void ASTPrinter::printIdentifier(const Identifier* node, std::string prefix, bool) {
    fmt::print("{}ID '{}'\n", prefix, node->name);
}

void ASTPrinter::printCall(const FunctionCall* node, std::string prefix, bool) {
    fmt::print("{}Call '{}'", prefix, node->name);
    
    // Print Generic Args ::<T>
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

} // namespace fin