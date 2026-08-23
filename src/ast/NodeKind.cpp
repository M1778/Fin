#include "NodeKind.hpp"

#include "ASTNode.hpp" // the umbrella: every concrete node type, complete

#include <typeindex>
#include <unordered_map>

// The category switch below is exhaustive over NodeKind by construction: it has
// no `default:` label and -Wswitch is an error in this file, so adding an
// enumerator to FIN_NODE_LIST breaks the build here until it is categorised.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic error "-Wswitch"
#elif defined(_MSC_VER)
#pragma warning(error : 4062)
#endif

namespace fin {

const char* nodeKindName(NodeKind kind) noexcept {
    switch (kind) {
        case NodeKind::Unknown:
            return "Unknown";
#define FIN_NODE_KIND_NAME_CASE(Name) \
    case NodeKind::Name:              \
        return #Name;
            FIN_NODE_LIST(FIN_NODE_KIND_NAME_CASE)
#undef FIN_NODE_KIND_NAME_CASE
    }
    return "Unknown";
}

NodeCategory nodeCategory(NodeKind kind) noexcept {
    switch (kind) {
        case NodeKind::Unknown:
            return NodeCategory::Unknown;

        // --- derive from ASTNode directly ---
        case NodeKind::Program:
        case NodeKind::Parameter:
        case NodeKind::StructMember:
        case NodeKind::Attribute:
        case NodeKind::GenericParam:
            return NodeCategory::Helper;

        // --- derive from Statement ---
        case NodeKind::FunctionDeclaration:
        case NodeKind::OperatorDeclaration:
        case NodeKind::ConstructorDeclaration:
        case NodeKind::DestructorDeclaration:
        case NodeKind::StructDeclaration:
        case NodeKind::InterfaceDeclaration:
        case NodeKind::ClassDeclaration:
        case NodeKind::EnumDeclaration:
        case NodeKind::DefineDeclaration:
        case NodeKind::MacroDeclaration:
        case NodeKind::TypeDefinition:
        case NodeKind::SpecialDeclaration:
        case NodeKind::ImplementsBlock:
        case NodeKind::ImportModule:
        case NodeKind::VariableDeclaration:
        case NodeKind::Block:
        case NodeKind::ReturnStatement:
        case NodeKind::ExpressionStatement:
        case NodeKind::IfStatement:
        case NodeKind::WhileLoop:
        case NodeKind::ForLoop:
        case NodeKind::ForeachLoop:
        case NodeKind::BreakStatement:
        case NodeKind::ContinueStatement:
        case NodeKind::DeleteStatement:
        case NodeKind::TryCatch:
        case NodeKind::BlameStatement:
            return NodeCategory::Statement;

        // --- derive from Expression ---
        case NodeKind::Literal:
        case NodeKind::Identifier:
        case NodeKind::BinaryOp:
        case NodeKind::UnaryOp:
        case NodeKind::TernaryOp:
        case NodeKind::FunctionCall:
        case NodeKind::MethodCall:
        case NodeKind::StaticMethodCall:
        case NodeKind::MacroCall:
        case NodeKind::MacroInvocation:
        case NodeKind::MemberAccess:
        case NodeKind::ArrayAccess:
        case NodeKind::ArrayLiteral:
        case NodeKind::PrototypeLiteral:
        case NodeKind::StructInstantiation:
        case NodeKind::NewExpression:
        case NodeKind::CastExpression:
        case NodeKind::SizeofExpression:
        case NodeKind::SuperExpression:
        case NodeKind::LambdaExpression:
        case NodeKind::QuoteExpression:
            return NodeCategory::Expression;

        // --- derive from TypeNode ---
        case NodeKind::TypeNode:
        case NodeKind::FunctionTypeNode:
        case NodeKind::PointerTypeNode:
        case NodeKind::ArrayTypeNode:
            return NodeCategory::TypeNode;
    }
    return NodeCategory::Unknown;
}

namespace {

using KindRegistry = std::unordered_map<std::type_index, NodeKind>;

const KindRegistry& kindRegistry() {
    // Generated from FIN_NODE_LIST, so registration cannot drift from the enum.
    static const KindRegistry registry = {
#define FIN_NODE_KIND_REGISTRY_ENTRY(Name) {std::type_index(typeid(Name)), NodeKind::Name},
        FIN_NODE_LIST(FIN_NODE_KIND_REGISTRY_ENTRY)
#undef FIN_NODE_KIND_REGISTRY_ENTRY
    };
    return registry;
}

} // namespace

NodeKind kindOfTypeInfo(const std::type_info& type) noexcept {
    const KindRegistry& registry = kindRegistry();
    auto it = registry.find(std::type_index(type));
    return it == registry.end() ? NodeKind::Unknown : it->second;
}

NodeKind kindOf(const ASTNode& node) noexcept {
    return kindOfTypeInfo(typeid(node));
}

NodeKind ASTNode::kind() const noexcept {
    return kindOf(*this);
}

UnregisteredNodeError::UnregisteredNodeError(const ASTNode& node)
    : std::logic_error(std::string("fin: AST node type '") + typeid(node).name() +
                       "' is not registered in FIN_NODE_LIST (src/ast/NodeKind.hpp), "
                       "so its children cannot be enumerated"),
      cxx_type_name_(typeid(node).name()) {}

} // namespace fin
