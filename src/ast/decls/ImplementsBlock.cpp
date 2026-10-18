// SPDX-License-Identifier: GPL-3.0-or-later
#include "ImplementsBlock.hpp"
#include "../Visitor.hpp"

namespace fin {

void ImplementsBlock::accept(Visitor& v) { v.visit(*this); }
    
}
