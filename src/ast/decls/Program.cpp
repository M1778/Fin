// SPDX-License-Identifier: GPL-3.0-or-later
#include "Program.hpp"
#include "../Visitor.hpp"

namespace fin {

Program::Program(std::vector<std::unique_ptr<Statement>> s) : statements(std::move(s)) {}
void Program::accept(Visitor& v) { v.visit(*this); }

}
