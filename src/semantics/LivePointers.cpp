#include "LivePointers.hpp"

namespace fin::scopes {

bool isPointerContaining(const TypePtr& type, LayoutEngine& engine) {
    if (!type) return false;
    uint64_t count = 0;
    if (!engine.pointerCount(type, count).empty()) return false;
    return count > 0;
}

std::vector<LiveBinding> livePointers(const std::vector<LiveBinding>& bindings,
                                      LayoutEngine& engine) {
    std::vector<LiveBinding> out;
    for (const auto& b : bindings) {
        if (b.moved == events::MovedState::Moved) continue;
        if (!isPointerContaining(b.type, engine)) continue;
        out.push_back(b);
    }
    return out;
}

LiveAnswer livePointersQuote(const LivePoint& point,
                             const std::vector<LiveBinding>& bindings,
                             LayoutEngine& engine) {
    LiveAnswer answer{point, {}, "quote {}"};
    for (const auto& b : livePointers(bindings, engine)) answer.names.push_back(b.name);
    if (answer.names.empty()) return answer;
    std::string quote = "quote { [";
    for (std::size_t i = 0; i < answer.names.size(); ++i) {
        if (i != 0) quote += ", ";
        quote += "&" + answer.names[i];
    }
    quote += "]; }";
    answer.quote = std::move(quote);
    return answer;
}

}  // namespace fin::scopes
