// =============================================================================
// Fin Programming Language Compiler
//
// Made with ❤️ for someone special.
//
// This project is genuinely built on love and dedication, a way to express your love can be done through Fin.
// Fin is a semi-object-orientated programming language that specialized in control, giving so much control to the user while asking for the bare minimum.
// Fin is the name of the Angel from BlackNightmare-Stories not yet published.
// “Love is the reason this project exists”
// -----------------------------------------------------------------------------
// Author: M1778
// Repository: https://github.com/M1778/Fin
// Profile: https://github.com/M1778M/
// -----------------------------------------------------------------------------
// Copyright (C) 2027 M1778
//
// This file is part of the Fin Programming Language Compiler.
//
// Fin is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// Fin is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Fin.  If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// =============================================================================

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
