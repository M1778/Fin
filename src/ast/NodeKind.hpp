#pragma once

// NodeKind -- the AST's kind discriminator (ADR 0004).
//
// Before this existed the AST had one dispatch mechanism, a 55-method
// pure-virtual `Visitor`, and asking "what kind of node is this?" from outside a
// visitor meant a `dynamic_cast` chain.  A NodeKind answers that in one lookup.
//
// ----------------------------------------------------------------------------
// ADDING A NODE TYPE -- read this before you add one
// ----------------------------------------------------------------------------
// There are exactly three steps, and two of them are enforced by the compiler:
//
//   1. Add one line to FIN_NODE_LIST below.  This is the only registration:
//      the enumerator, the printable name, and the typeid -> NodeKind entry
//      used by `kindOf` are all generated from it.
//
//   2. `nodeCategory()` in NodeKind.cpp will stop compiling until you say
//      which category the new kind is in.  That switch has no `default:` and
//      the file promotes -Wswitch to an error, deliberately.
//
//   3. `forEachChild()` in StructuralWalk.cpp will stop compiling until you
//      list the new node's children.  Same mechanism, same reason: a node type
//      whose children nobody enumerated is a silently skipped subtree, which is
//      the exact failure ADR 0004 is trying to remove.
//
// Nothing else.  In particular you do NOT add a method to the node class, and
// you do NOT add a `visit` overload unless the node genuinely needs per-type
// dispatch from `Visitor`.
//
// The one hole this design leaves: a node type absent from FIN_NODE_LIST
// altogether compiles fine and `kindOf` answers `Unknown`.  That is not silent
// either -- `StructuralWalk` refuses to walk an unregistered node and names the
// C++ type in the error -- but it is a runtime failure rather than a build one.

#include <stdexcept>
#include <string>
#include <typeinfo>

namespace fin {

class ASTNode;

// The single registration point.  Every name here is the C++ class name of a
// concrete node type, which is what lets the typeid registry be generated.
//
// Order matters only for readability: the groups below mirror the C++ hierarchy
// (plain ASTNode, Statement, Expression, TypeNode) and `nodeCategory()` is what
// actually decides membership.
#define FIN_NODE_LIST(V)      \
    /* --- plain ASTNode --- */ \
    V(Program)                \
    V(Parameter)              \
    V(StructMember)           \
    V(Attribute)              \
    V(GenericParam)           \
    /* --- Statement --- */   \
    V(FunctionDeclaration)    \
    V(OperatorDeclaration)    \
    V(ConstructorDeclaration) \
    V(DestructorDeclaration)  \
    V(StructDeclaration)      \
    V(InterfaceDeclaration)   \
    V(ClassDeclaration)       \
    V(EnumDeclaration)        \
    V(DefineDeclaration)      \
    V(MacroDeclaration)       \
    V(TypeDefinition)         \
    V(SpecialDeclaration)     \
    V(ImplementsBlock)        \
    V(ImportModule)           \
    V(VariableDeclaration)    \
    V(Block)                  \
    V(ReturnStatement)        \
    V(ExpressionStatement)    \
    V(IfStatement)            \
    V(WhileLoop)              \
    V(ForLoop)                \
    V(ForeachLoop)            \
    V(BreakStatement)         \
    V(ContinueStatement)      \
    V(DeleteStatement)        \
    V(TryCatch)               \
    V(BlameStatement)         \
    /* --- Expression --- */  \
    V(Literal)                \
    V(Identifier)             \
    V(BinaryOp)               \
    V(UnaryOp)                \
    V(TernaryOp)              \
    V(FunctionCall)           \
    V(MethodCall)             \
    V(StaticMethodCall)       \
    V(MacroCall)              \
    V(MacroInvocation)        \
    V(MemberAccess)           \
    V(ArrayAccess)            \
    V(ArrayLiteral)           \
    V(PrototypeLiteral)       \
    V(StructInstantiation)    \
    V(NewExpression)          \
    V(CastExpression)         \
    V(TypeLiteralExpression)  \
    V(SizeofExpression)       \
    V(SuperExpression)        \
    V(LambdaExpression)       \
    V(QuoteExpression)        \
    /* --- TypeNode --- */    \
    V(TypeNode)               \
    V(FunctionTypeNode)       \
    V(PointerTypeNode)        \
    V(ArrayTypeNode)

enum class NodeKind {
    // `Unknown` is 0 so a zero-initialised NodeKind is never a real node type.
    Unknown = 0,
#define FIN_NODE_KIND_ENUMERATOR(Name) Name,
    FIN_NODE_LIST(FIN_NODE_KIND_ENUMERATOR)
#undef FIN_NODE_KIND_ENUMERATOR
};

#define FIN_NODE_KIND_COUNT_ONE(Name) +1
// Includes `Unknown`.
inline constexpr int kNodeKindCount = 1 FIN_NODE_LIST(FIN_NODE_KIND_COUNT_ONE);
#undef FIN_NODE_KIND_COUNT_ONE

// Which C++ base a kind derives from.  This is the hierarchy, not a semantic
// grouping: `Statement` here means "static_cast<Statement*> is legal".
enum class NodeCategory {
    Unknown,
    Helper,      // derives from ASTNode directly
    Statement,   // derives from Statement
    Expression,  // derives from Expression
    TypeNode,    // derives from TypeNode
};

// The class name, e.g. "FunctionDeclaration". Never null; "Unknown" for Unknown.
const char* nodeKindName(NodeKind kind) noexcept;

NodeCategory nodeCategory(NodeKind kind) noexcept;

inline bool isStatementKind(NodeKind k) noexcept { return nodeCategory(k) == NodeCategory::Statement; }
inline bool isExpressionKind(NodeKind k) noexcept { return nodeCategory(k) == NodeCategory::Expression; }
inline bool isTypeNodeKind(NodeKind k) noexcept { return nodeCategory(k) == NodeCategory::TypeNode; }

// The kind of a node's *dynamic* type.  `Unknown` means the type is missing
// from FIN_NODE_LIST.
NodeKind kindOf(const ASTNode& node) noexcept;

// The kind registered for a C++ type, whether or not there is an instance.
NodeKind kindOfTypeInfo(const std::type_info& type) noexcept;

// The kind registered for a node class, without an instance:
//     if (node.kind() == nodeKindOfType<Attribute>()) ...
// Returns `Unknown` for an unregistered type.
template <typename T>
NodeKind nodeKindOfType() noexcept {
    return kindOfTypeInfo(typeid(T));
}

// Thrown when a walk reaches a node whose C++ type is not in FIN_NODE_LIST.
// It is an exception rather than a returned error because the alternative --
// walking on -- silently drops a subtree.
class UnregisteredNodeError : public std::logic_error {
public:
    explicit UnregisteredNodeError(const ASTNode& node);
    // The C++ type name as `typeid` reports it (implementation-mangled).
    const std::string& cxxTypeName() const noexcept { return cxx_type_name_; }

private:
    std::string cxx_type_name_;
};

} // namespace fin
