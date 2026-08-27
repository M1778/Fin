#pragma once

// StructuralWalk -- traversal with a default (ADR 0004).
//
// `Visitor` is 55 pure-virtual methods, so a consumer that cares about two node
// types still writes 55 overrides, and a consumer that cares about *arbitrary*
// nodes -- a compiler component rewriting code at a scope-exit event, say --
// cannot use it at all, because it is dispatching from outside any visitor.
//
// StructuralWalk is the other mechanism.  It knows every node's children, so a
// consumer overrides `enter`/`leave` and switches on `NodeKind` for the handful
// it cares about.  Traversal costs zero overrides.
//
// WHICH MECHANISM TO USE
//   - `Visitor`        : you must handle every node type and want the compiler
//                        to refuse to build until you do.
//   - `StructuralWalk` : you care about a few node types, or about a node whose
//                        type you learn at runtime.
//
// Choosing wrongly costs a silently-skipped subtree, which is the failure
// `Visitor`'s exhaustiveness was protecting against.  That is why an
// unregistered node type here is an error rather than a no-op.

#include "NodeKind.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace fin {

class ASTNode;

// Called once per child, in source order, skipping null children.  Null
// children are not an error: `IfStatement::else_stmt` is null for an `if` with
// no `else`.
using ChildCallback = std::function<void(ASTNode&)>;

// Enumerates `node`'s immediate children.  This is the one place in the tree
// that knows the shape of every node type; everything else composes on it.
//
// Throws UnregisteredNodeError if `node`'s dynamic type is not in
// FIN_NODE_LIST -- silently reporting "no children" would drop a subtree.
void forEachChild(ASTNode& node, const ChildCallback& callback);

// `forEachChild` collected into a vector, for callers that want to index or
// count children rather than stream them.
std::vector<ASTNode*> childrenOf(ASTNode& node);

class StructuralWalk {
public:
    virtual ~StructuralWalk() = default;

    // Depth-first pre-order.  For each node: `enter`, then the children unless
    // `enter` returned false, then `leave`.  `leave` runs even when the children
    // were skipped, so an override can pair them without a flag.
    void walk(ASTNode& node);

    // Null-tolerant, because most child pointers in the AST are optional.
    void walk(ASTNode* node);

    template <typename T>
    void walkAll(const std::vector<std::unique_ptr<T>>& nodes) {
        for (const std::unique_ptr<T>& node : nodes) {
            walk(node.get());
        }
    }

protected:
    // Return false to skip this node's children.
    virtual bool enter(ASTNode& node) {
        (void)node;
        return true;
    }

    virtual void leave(ASTNode& node) { (void)node; }

    // A node whose type is missing from FIN_NODE_LIST.  The default throws
    // UnregisteredNodeError.  Override to downgrade it -- but understand that
    // continuing means the node's entire subtree is not visited.
    virtual void onUnregisteredNode(ASTNode& node);

    // Walk the children of `node` without re-entering it.  Useful from an
    // override that wants to do work between `enter` and the children.
    void walkChildren(ASTNode& node);
};

} // namespace fin
