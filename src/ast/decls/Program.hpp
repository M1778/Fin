// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../nodes/ASTNode.hpp"
#include "../stmts/Statement.hpp"
#include <vector>
#include <memory>
#include <string>

namespace fin {

class Program : public ASTNode {
public:
    std::vector<std::unique_ptr<Statement>> statements;
    // Every `namespace X { ... }` block this file opened, in source order.
    // Collected by the parser (see noteNamespaceName): the blocks splice and
    // discard their names, so this is the only record of which namespaces the
    // file declares. The analyzer publishes it into the file's scope, where
    // the #90 strict rule reads it -- imports check the module's record, an
    // extern qualifier checks the importing file's own.
    std::vector<std::string> declared_namespaces;
    Program(std::vector<std::unique_ptr<Statement>> s);
    void accept(Visitor& v) override;
};

}
