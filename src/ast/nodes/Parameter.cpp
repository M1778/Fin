#include "../nodes/Parameter.hpp"
#include "../Visitor.hpp"

namespace fin {

Parameter::Parameter(std::string n, std::unique_ptr<TypeNode> t, std::unique_ptr<Expression> d, bool v)
    : name(std::move(n)), type(std::move(t)), default_value(std::move(d)), is_vararg(v) {}
void Parameter::accept(Visitor& v) { v.visit(*this); } 

}
