#include "Attribute.hpp"
#include "../Visitor.hpp"

namespace fin {

Attribute::Attribute(std::string n, bool flag) : name(std::move(n)), is_flag(flag) {}
Attribute::Attribute(std::string n, std::string v) : name(std::move(n)), value_str(std::move(v)), is_flag(false) {}
void Attribute::accept(Visitor& v) { v.visit(*this); }

} // namespace fin
