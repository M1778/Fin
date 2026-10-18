// SPDX-License-Identifier: GPL-3.0-or-later
#include "PrototypeExpr.hpp"
#include "../Visitor.hpp"

namespace fin {

void PrototypeLiteral::accept(Visitor& v) { v.visit(*this); }

}
