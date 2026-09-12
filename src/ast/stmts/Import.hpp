#pragma once
#include "../nodes/ASTNode.hpp"
#include "../types/Attribute.hpp"
#include <string>
#include <vector>

namespace fin {

class ImportModule : public Statement {
public:
    std::string source;
    bool is_package;
    std::string alias;
    std::vector<std::string> targets;
    // The `::`-separated tail of a module path: `import { Error } from error::std`
    // names the module `error` and the namespace `std` inside it
    // (tests/samples/importing.fin:14 states this). Empty when the path was
    // written without `::`.
    std::string namespace_path;
    // `#[stdimport]` above an import. An attribute attaches to an import the
    // same way it attaches to a declaration; four stdlib samples open with one.
    std::vector<std::unique_ptr<Attribute>> attributes;
    // Set once every name this import brings in has been bound into the importing
    // scope. An import is a compile-time name-binding directive and nothing else,
    // so once it has bound its names it has no further meaning -- which is why the
    // backend refuses one that reaches it (`an import (the module loader did not
    // consume it)`). The front end is what takes it out of the tree, and it does so
    // only on this flag: an import that named a module or a symbol that does not
    // exist stays in the tree behind its own diagnostic, because a construct the
    // compiler could not handle is refused and never quietly dropped.
    bool consumed = false;
    ImportModule(std::string src, bool pkg, std::string al, std::vector<std::string> tgts);
    void accept(Visitor& v) override;
};

}
