#include "PrototypeExpr.hpp"
#include "../Visitor.hpp"

namespace fin {

void PrototypeLiteral::accept(Visitor& v) { v.visit(*this); }

}
