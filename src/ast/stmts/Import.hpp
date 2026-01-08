#pragma once
#include "../nodes/ASTNode.hpp"
#include <string>
#include <vector>

namespace fin {

class ImportModule : public Statement {
public:
    std::string source;
    bool is_package;
    std::string alias;
    std::vector<std::string> targets;
    ImportModule(std::string src, bool pkg, std::string al, std::vector<std::string> tgts);
    void accept(Visitor& v) override;
};

}
