// SPDX-License-Identifier: GPL-3.0-or-later
#include "Statement.hpp"
#include "../Visitor.hpp"

namespace fin {

Block::Block(std::vector<std::unique_ptr<Statement>> s) : statements(std::move(s)) {}
void Block::accept(Visitor& v) { v.visit(*this); }

}
