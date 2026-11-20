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

#include "Attribute.hpp"
#include "../Visitor.hpp"
#include "../StructuralWalk.hpp"
#include "../stmts/Statement.hpp"

namespace fin {

Attribute::Attribute(std::string n, bool flag) : name(std::move(n)), is_flag(flag) {}
Attribute::Attribute(std::string n, std::string v) : name(std::move(n)), value_str(std::move(v)), is_flag(false) {}
void Attribute::accept(Visitor& v) { v.visit(*this); }

namespace {

// Sets `std_scoped` on every `#[global]` in a subtree.
//
// `unregisteredNode` is left at the default, which throws: a node type missing
// from FIN_NODE_LIST would mean a whole subtree went unmarked, and an unmarked
// `#[global]` is one the analyzer then refuses -- so swallowing it would turn a
// registration gap into a diagnostic about the user's program.
class StdScopeMarker : public StructuralWalk {
protected:
    bool enter(ASTNode& node) override {
        if (node.kind() == NodeKind::Attribute) {
            auto& attr = static_cast<Attribute&>(node);
            if (attr.name == kGlobalAttribute && attr.is_flag) attr.std_scoped = true;
        }
        return true;
    }
};

} // namespace

void markStdScopedGlobals(const std::vector<std::unique_ptr<Statement>>& body) {
    StdScopeMarker marker;
    marker.walkAll(body);
}

namespace {
std::vector<std::string>& namespaceNameSink() {
    static std::vector<std::string> names;
    return names;
}
} // namespace

void noteNamespaceName(const std::string& name) {
    namespaceNameSink().push_back(name);
}

std::vector<std::string> takeNamespaceNames() {
    std::vector<std::string> out;
    out.swap(namespaceNameSink());
    return out;
}

} // namespace fin
