#pragma once

#include "../nodes/ASTNode.hpp"
#include <string>

namespace fin {

class Attribute : public ASTNode {
public:
    std::string name;
    std::string value_str; 
    bool is_flag = true;
    Attribute(std::string n, bool flag);
    Attribute(std::string n, std::string v);
    void accept(Visitor& v) override;
};

} // namespace fin
