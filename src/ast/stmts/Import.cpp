#include "Import.hpp"
#include "../Visitor.hpp"

namespace fin {

ImportModule::ImportModule(std::string src, bool pkg, std::string al, std::vector<std::string> tgts)
    : source(std::move(src)), is_package(pkg), alias(std::move(al)), targets(std::move(tgts)) {}
void ImportModule::accept(Visitor& v) { v.visit(*this); }

}
