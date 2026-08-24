#include "IntegerConstant.hpp"

#include <cstdint>

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

ConstantRead readSignedConstant(const ASTNode& node, int64_t& out) {
    bool negative = false;
    if (!integerConstant(node, negative)) return ConstantRead::NotConstant;

    // The magnitude is read by peeling the sign off and asking the unsigned reader,
    // so there is exactly one digit loop and one overflow rule in this file.
    const ASTNode* inner = &node;
    while (auto* un = dynamic_cast<const UnaryOp*>(inner)) inner = un->operand.get();
    if (!inner) return ConstantRead::NotConstant;

    uint64_t magnitude = 0;
    const ConstantRead read = readConstant(*inner, magnitude);
    if (read != ConstantRead::Ok) return read;

    // The two bounds are not symmetric: -9223372036854775808 fits and its positive
    // twin does not.
    const uint64_t limit = negative ? (static_cast<uint64_t>(INT64_MAX) + 1)
                                    : static_cast<uint64_t>(INT64_MAX);
    if (magnitude > limit) return ConstantRead::TooLarge;

    // INT64_MIN spelled out rather than negated: negating the int64 form of its
    // magnitude is the one case of this that is undefined.
    if (negative && magnitude == limit) out = INT64_MIN;
    else out = negative ? -static_cast<int64_t>(magnitude) : static_cast<int64_t>(magnitude);
    return ConstantRead::Ok;
}

}  // namespace fin
