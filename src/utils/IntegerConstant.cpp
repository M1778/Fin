#include "IntegerConstant.hpp"

#include "../ast/ASTNode.hpp"

namespace fin {

bool integerConstant(const ASTNode& node, bool& negative) {
    if (auto* lit = dynamic_cast<const Literal*>(&node)) {
        negative = false;
        return lit->kind == ASTTokenKind::INTEGER;
    }
    if (auto* un = dynamic_cast<const UnaryOp*>(&node)) {
        if (un->op != ASTTokenKind::MINUS || !un->operand) return false;
        bool inner = false;
        if (!integerConstant(*un->operand, inner)) return false;
        negative = !inner;  // `--1` is non-negative; nesting costs nothing to allow
        return true;
    }
    return false;
}

ConstantRead readConstant(const ASTNode& node, uint64_t& out) {
    bool negative = false;
    if (!integerConstant(node, negative)) return ConstantRead::NotConstant;
    if (negative) return ConstantRead::Negative;

    // `--5` is what integerConstant calls non-negative, and it is a UnaryOp rather
    // than the Literal, so the wrapping is peeled rather than assumed away.
    const ASTNode* inner = &node;
    while (auto* un = dynamic_cast<const UnaryOp*>(inner)) inner = un->operand.get();
    auto* lit = dynamic_cast<const Literal*>(inner);
    if (!lit || lit->value.empty()) return ConstantRead::NotConstant;

    uint64_t value = 0;
    for (char c : lit->value) {
        if (c < '0' || c > '9') return ConstantRead::NotConstant;
        const uint64_t digit = static_cast<uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10) return ConstantRead::TooLarge;
        value = value * 10 + digit;
    }
    out = value;
    return ConstantRead::Ok;
}

}  // namespace fin
