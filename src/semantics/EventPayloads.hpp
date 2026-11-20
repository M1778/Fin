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

#pragma once

#include <string>
#include <vector>

#include "EventRegistry.hpp"

namespace fin {
class Expression;
class Program;
class SpecialDeclaration;

namespace events {

// Wave-4 step 17 (docs/compiler-api.md §3.1-§3.2), W6 floor:
// function_entry/function_exit, assignment, allocation_site/delete_site.
//
// The handler's signature IS the event's payload (§3.1): one row here is the
// parameter list every handler for that event must declare, and the quote it
// must return. Spellings are §3.2 transcribed, not invented: a payload change
// lands in this one table, and W5's match check answers from these rows, so
// the diagnostic follows without a second transcription to drift from it.
//
// Only W6's five rows live here. W5's events (variable_declared,
// variable_scope_exit, struct_layout_finalised, ...) answer nullptr from
// findW6Payload: two match checks owning one event would diagnose it twice.
struct EventParam {
    std::string name;
    std::string type;
};

struct EventPayload {
    std::string event;
    std::vector<EventParam> params;
    std::string returns;
};

const std::vector<EventPayload>& w6Payloads();
const EventPayload* findW6Payload(const std::string& event);
bool isW6Event(const std::string& event);

// "(f: function, exit_kind: int) <quote>": the shape a mismatch names, so the
// diagnostic carries the whole contract instead of one missing parameter.
std::string expectedSignature(const EventPayload& payload);

// The match check for W6's events (docs/compiler-api.md §3.8: caught before
// anything runs, naming expected and actual). Runs in visit(Program) right
// after Collect/Arm, over this module's handlers, W6 events only: W5's match
// check owns W5's events, and two checks owning one event diagnose it twice.
//
// A handler matches when its arity, its parameter type spellings and its
// return spelling are the payload's. Parameter *names* are the handler's own
// (they are local); structure beyond the base name is not (`f: function?`
// is a different type than the payload's `function`). `const` is the one
// decoration ignored: it constrains the binding, not the arriving value.
//
// The interpretability line is held in the same pass (owner decision): a
// handler whose body holds control flow (if/while/for/foreach) is refused by
// name, whatever its signature says. `blame` and `return` stay legal: §3.8
// needs the first and every `@special` needs the second.
void checkW6HandlerPayloads(Program& program, const EventRegistry& registry,
                            DiagReporter report);

// One docs/compiler-api.md §3.2 fire point, as analysed. `site` is the
// enclosing function ("<root>" outside one); `line` its source line; `detail`
// the payload's human half: the assignment target (`x:int`), the
// allocated/deleted type, or the exit path ("return", "fallthrough").
// Recorded with no diagnostic of their own, so a program that fires them
// compiles exactly as before; W5's firing pass reads them from the analyzer
// rather than re-deriving where the points are.
struct FirePoint {
    std::string event;
    std::string site;
    int line = -1;
    std::string detail;
};

// An assignment/delete target as spelled: an identifier by name, anything
// else by node kind in brackets (`p.f` is a MemberAccess, `p[k]` an
// ArrayAccess). Shared so W5's firing spells the same site the same way.
std::string spellFireTarget(const Expression& target);

} // namespace events
} // namespace fin
