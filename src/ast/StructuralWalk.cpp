#include "StructuralWalk.hpp"

#include "ASTNode.hpp" // the umbrella: every concrete node type, complete

// The switch in forEachChild() is exhaustive over NodeKind by construction: no
// `default:` label, and -Wswitch promoted to an error.  Adding an enumerator to
// FIN_NODE_LIST therefore breaks the build here until the new node's children
// are listed, which is the point -- see the seam notes in NodeKind.hpp.
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic error "-Wswitch"
#elif defined(_MSC_VER)
#pragma warning(error : 4062)
#endif

namespace fin {

namespace {

// --- child-emitting helpers --------------------------------------------------

void emit(const ChildCallback& out, ASTNode* child) {
    if (child != nullptr) {
        out(*child);
    }
}

template <typename T>
void emitAll(const ChildCallback& out, const std::vector<std::unique_ptr<T>>& children) {
    for (const std::unique_ptr<T>& child : children) {
        emit(out, child.get());
    }
}

// Fields keyed by a plain string: `{name, value}` pairs in enum bodies, struct
// literals and `super { ... }`.  Only the value is a node.
template <typename T>
void emitPairValues(const ChildCallback& out,
                    const std::vector<std::pair<std::string, std::unique_ptr<T>>>& pairs) {
    for (const auto& entry : pairs) {
        emit(out, entry.second.get());
    }
}

// The children TypeNode itself owns.  Its three subclasses inherit these and add
// their own, so every TypeNode case has to include them or a generic argument
// goes unvisited.
void emitTypeNodeBase(const ChildCallback& out, TypeNode& node) {
    emitAll(out, node.generics);
    emitAll(out, node.annotations);
    emit(out, node.array_size.get());
}

// Members shared by StructDeclaration, InterfaceDeclaration and
// ClassDeclaration, which are three near-copies of one another in the AST.
template <typename T>
void emitTypeBodyMembers(const ChildCallback& out, T& node) {
    emitAll(out, node.members);
    emitAll(out, node.methods);
    emitAll(out, node.operators);
    emitAll(out, node.constructors);
    emit(out, node.destructor.get());
}

} // namespace

void forEachChild(ASTNode& node, const ChildCallback& out) {
    const NodeKind kind = node.kind();

    switch (kind) {
        case NodeKind::Unknown:
            throw UnregisteredNodeError(node);

        // --- plain ASTNode -------------------------------------------------
        case NodeKind::Program:
            emitAll(out, static_cast<Program&>(node).statements);
            return;

        case NodeKind::Parameter: {
            auto& n = static_cast<Parameter&>(node);
            emit(out, n.type.get());
            emit(out, n.default_value.get());
            return;
        }

        case NodeKind::StructMember: {
            auto& n = static_cast<StructMember&>(node);
            emitAll(out, n.attributes);
            emit(out, n.type.get());
            emit(out, n.default_value.get());
            return;
        }

        case NodeKind::Attribute:
            // A leaf: name and value are strings.
            return;

        case NodeKind::GenericParam:
            emit(out, static_cast<GenericParam&>(node).constraint.get());
            return;

        // --- Statement -----------------------------------------------------
        case NodeKind::FunctionDeclaration: {
            auto& n = static_cast<FunctionDeclaration&>(node);
            emitAll(out, n.attributes);
            emitAll(out, n.generic_params);
            emitAll(out, n.params);
            emit(out, n.return_type.get());
            emit(out, n.body.get());
            return;
        }

        case NodeKind::OperatorDeclaration: {
            auto& n = static_cast<OperatorDeclaration&>(node);
            emitAll(out, n.generic_params);
            emitAll(out, n.params);
            emit(out, n.return_type.get());
            emit(out, n.implements_type.get());
            emit(out, n.body.get());
            return;
        }

        case NodeKind::ConstructorDeclaration: {
            auto& n = static_cast<ConstructorDeclaration&>(node);
            emitAll(out, n.params);
            emit(out, n.return_type.get());
            emit(out, n.body.get());
            return;
        }

        case NodeKind::DestructorDeclaration:
            emit(out, static_cast<DestructorDeclaration&>(node).body.get());
            return;

        case NodeKind::StructDeclaration: {
            auto& n = static_cast<StructDeclaration&>(node);
            emitAll(out, n.attributes);
            emitAll(out, n.generic_params);
            emitAll(out, n.parents);
            emitTypeBodyMembers(out, n);
            return;
        }

        case NodeKind::InterfaceDeclaration: {
            auto& n = static_cast<InterfaceDeclaration&>(node);
            emitAll(out, n.attributes);
            emitAll(out, n.generic_params);
            emitTypeBodyMembers(out, n);
            return;
        }

        case NodeKind::ClassDeclaration: {
            auto& n = static_cast<ClassDeclaration&>(node);
            emitAll(out, n.attributes);
            emitAll(out, n.generic_params);
            emitAll(out, n.parents);
            emitTypeBodyMembers(out, n);
            return;
        }

        case NodeKind::EnumDeclaration: {
            auto& n = static_cast<EnumDeclaration&>(node);
            emitAll(out, n.attributes);
            emitPairValues(out, n.values);
            return;
        }

        case NodeKind::DefineDeclaration: {
            auto& n = static_cast<DefineDeclaration&>(node);
            emitAll(out, n.params);
            emit(out, n.return_type.get());
            return;
        }

        case NodeKind::MacroDeclaration: {
            auto& n = static_cast<MacroDeclaration&>(node);
            emit(out, n.body.get());
            for (const MacroRule& rule : n.rules) {
                emit(out, rule.expansion.get());
            }
            return;
        }

        case NodeKind::TypeDefinition: {
            auto& n = static_cast<TypeDefinition&>(node);
            emitAll(out, n.attributes);
            emitAll(out, n.generic_params);
            emit(out, n.aliased_type.get());
            emitAll(out, n.implements_list);
            return;
        }

        case NodeKind::SpecialDeclaration: {
            auto& n = static_cast<SpecialDeclaration&>(node);
            emitAll(out, n.attributes);
            emitAll(out, n.params);
            emit(out, n.return_type.get());
            emit(out, n.body.get());
            return;
        }

        case NodeKind::ImplementsBlock: {
            auto& n = static_cast<ImplementsBlock&>(node);
            emit(out, n.interface_type.get());
            emitAll(out, n.methods);
            emitAll(out, n.operators);
            return;
        }

        case NodeKind::ImportModule:
            // A leaf: source, alias and targets are strings.
            return;

        case NodeKind::VariableDeclaration: {
            auto& n = static_cast<VariableDeclaration&>(node);
            emitAll(out, n.attributes);
            emit(out, n.type.get());
            emit(out, n.initializer.get());
            return;
        }

        case NodeKind::Block:
            emitAll(out, static_cast<Block&>(node).statements);
            return;

        case NodeKind::ReturnStatement:
            emit(out, static_cast<ReturnStatement&>(node).value.get());
            return;

        case NodeKind::ExpressionStatement:
            emit(out, static_cast<ExpressionStatement&>(node).expr.get());
            return;

        case NodeKind::IfStatement: {
            auto& n = static_cast<IfStatement&>(node);
            emit(out, n.condition.get());
            emit(out, n.then_block.get());
            emit(out, n.else_stmt.get());
            return;
        }

        case NodeKind::WhileLoop: {
            auto& n = static_cast<WhileLoop&>(node);
            emit(out, n.condition.get());
            emit(out, n.body.get());
            return;
        }

        case NodeKind::ForLoop: {
            auto& n = static_cast<ForLoop&>(node);
            emit(out, n.init.get());
            emit(out, n.condition.get());
            emit(out, n.increment.get());
            emit(out, n.body.get());
            return;
        }

        case NodeKind::ForeachLoop: {
            auto& n = static_cast<ForeachLoop&>(node);
            emit(out, n.var_type.get());
            emit(out, n.iterable.get());
            emit(out, n.body.get());
            return;
        }

        case NodeKind::BreakStatement:
        case NodeKind::ContinueStatement:
            return;

        case NodeKind::DeleteStatement:
            emit(out, static_cast<DeleteStatement&>(node).expr.get());
            return;

        case NodeKind::TryCatch: {
            auto& n = static_cast<TryCatch&>(node);
            emit(out, n.try_block.get());
            emit(out, n.catch_type.get());
            emit(out, n.catch_block.get());
            return;
        }

        case NodeKind::BlameStatement: {
            auto& n = static_cast<BlameStatement&>(node);
            emit(out, n.condition.get());
            emit(out, n.message.get());
            return;
        }

        // --- Expression ----------------------------------------------------
        case NodeKind::Literal:
        case NodeKind::Identifier:
            return;

        case NodeKind::BinaryOp: {
            auto& n = static_cast<BinaryOp&>(node);
            emit(out, n.left.get());
            emit(out, n.right.get());
            return;
        }

        case NodeKind::UnaryOp:
            emit(out, static_cast<UnaryOp&>(node).operand.get());
            return;

        case NodeKind::TernaryOp: {
            auto& n = static_cast<TernaryOp&>(node);
            emit(out, n.condition.get());
            emit(out, n.true_expr.get());
            emit(out, n.false_expr.get());
            return;
        }

        case NodeKind::FunctionCall: {
            auto& n = static_cast<FunctionCall&>(node);
            emitAll(out, n.generic_args);
            emitAll(out, n.args);
            return;
        }

        case NodeKind::MethodCall: {
            auto& n = static_cast<MethodCall&>(node);
            emit(out, n.object.get());
            emitAll(out, n.generic_args);
            emitAll(out, n.args);
            return;
        }

        case NodeKind::StaticMethodCall: {
            auto& n = static_cast<StaticMethodCall&>(node);
            emit(out, n.target_type.get());
            emitAll(out, n.generic_args);
            emitAll(out, n.args);
            return;
        }

        case NodeKind::MacroCall:
            emitAll(out, static_cast<MacroCall&>(node).args);
            return;

        case NodeKind::MacroInvocation:
            emitAll(out, static_cast<MacroInvocation&>(node).args);
            return;

        case NodeKind::MemberAccess:
            emit(out, static_cast<MemberAccess&>(node).object.get());
            return;

        case NodeKind::ArrayAccess: {
            auto& n = static_cast<ArrayAccess&>(node);
            emit(out, n.array.get());
            emit(out, n.index.get());
            return;
        }

        case NodeKind::ArrayLiteral:
            emitAll(out, static_cast<ArrayLiteral&>(node).elements);
            return;

        case NodeKind::PrototypeLiteral: {
            auto& n = static_cast<PrototypeLiteral&>(node);
            for (const auto& entry : n.elements) {
                emit(out, entry.first.get());
                emit(out, entry.second.get());
            }
            return;
        }

        case NodeKind::StructInstantiation: {
            auto& n = static_cast<StructInstantiation&>(node);
            emitAll(out, n.generic_args);
            emitPairValues(out, n.fields);
            return;
        }

        case NodeKind::NewExpression: {
            auto& n = static_cast<NewExpression&>(node);
            emit(out, n.type.get());
            emitAll(out, n.args);
            emitPairValues(out, n.init_fields);
            return;
        }

        case NodeKind::CastExpression: {
            auto& n = static_cast<CastExpression&>(node);
            emit(out, n.target_type.get());
            emit(out, n.expr.get());
            return;
        }

        case NodeKind::SizeofExpression: {
            auto& n = static_cast<SizeofExpression&>(node);
            emit(out, n.type_target.get());
            emit(out, n.expr_target.get());
            return;
        }

        case NodeKind::SuperExpression: {
            auto& n = static_cast<SuperExpression&>(node);
            emitAll(out, n.args);
            emitPairValues(out, n.init_fields);
            return;
        }

        case NodeKind::LambdaExpression: {
            auto& n = static_cast<LambdaExpression&>(node);
            emitAll(out, n.params);
            emit(out, n.return_type.get());
            emit(out, n.body.get());
            emit(out, n.expression_body.get());
            return;
        }

        case NodeKind::QuoteExpression:
            emit(out, static_cast<QuoteExpression&>(node).block.get());
            return;

        // --- TypeNode ------------------------------------------------------
        case NodeKind::TypeNode:
            emitTypeNodeBase(out, static_cast<TypeNode&>(node));
            return;

        case NodeKind::FunctionTypeNode: {
            auto& n = static_cast<FunctionTypeNode&>(node);
            emitTypeNodeBase(out, n);
            emitAll(out, n.param_types);
            emit(out, n.return_type.get());
            return;
        }

        case NodeKind::PointerTypeNode: {
            auto& n = static_cast<PointerTypeNode&>(node);
            emitTypeNodeBase(out, n);
            emit(out, n.pointee.get());
            return;
        }

        case NodeKind::ArrayTypeNode: {
            auto& n = static_cast<ArrayTypeNode&>(node);
            emitTypeNodeBase(out, n);
            emit(out, n.element_type.get());
            emit(out, n.size.get());
            return;
        }
    }

    // Unreachable: the switch above is exhaustive and every case returns.
    throw UnregisteredNodeError(node);
}

std::vector<ASTNode*> childrenOf(ASTNode& node) {
    std::vector<ASTNode*> children;
    forEachChild(node, [&children](ASTNode& child) { children.push_back(&child); });
    return children;
}

void StructuralWalk::onUnregisteredNode(ASTNode& node) {
    throw UnregisteredNodeError(node);
}

void StructuralWalk::walkChildren(ASTNode& node) {
    forEachChild(node, [this](ASTNode& child) { walk(child); });
}

void StructuralWalk::walk(ASTNode& node) {
    if (node.kind() == NodeKind::Unknown) {
        onUnregisteredNode(node);
        return;
    }
    const bool descend = enter(node);
    if (descend) {
        walkChildren(node);
    }
    leave(node);
}

void StructuralWalk::walk(ASTNode* node) {
    if (node != nullptr) {
        walk(*node);
    }
}

} // namespace fin
