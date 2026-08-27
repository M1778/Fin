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
    ImportModule(std::string src, bool pkg, std::string al, std::vector<std::string> tgts);
    void accept(Visitor& v) override;
};

}
