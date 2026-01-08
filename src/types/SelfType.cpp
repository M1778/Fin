#include "SelfType.hpp"
#include "TypeImpl.hpp"

namespace fin {

TypePtr SelfType::substitute(const TypeMap& mapping, TypePtr selfReplacement) {
    if (selfReplacement) return selfReplacement;
    return std::make_shared<SelfType>(originalStruct);
}

}
