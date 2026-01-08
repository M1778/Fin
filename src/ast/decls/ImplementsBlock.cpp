#include "ImplementsBlock.hpp"
#include "../Visitor.hpp"

namespace fin {

void ImplementsBlock::accept(Visitor& v) { v.visit(*this); }

}
