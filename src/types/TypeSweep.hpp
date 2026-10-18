// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdlib>
#include <unordered_set>
#include <vector>

namespace fin {

class Type;

// Issue #101. The semantic type graph is mutually recursive: a struct owns the
// FunctionTypes of its methods, constructors and enumerators, and those signatures
// name the struct straight back (return types, parameters, `Self`), so with
// shared_ptr on every edge each preloaded module leaves a reference island no
// ordinary destructor can reclaim. Of the 373648 bytes `finc -c` leaked, every
// leaked record was indirect -- each chunk referenced only by another leaked chunk,
// never by anything alive.
//
// Each Type registers itself here on construction and erases itself in its
// destructor, so the registry never keeps a node alive and never holds a dangling
// pointer. At process exit -- before the leak check, which runs its own, earlier
// atexit hook -- the sweep walks the surviving nodes and asks each one, through
// Type::releaseTypeEdges, to drop the type-valued edges it holds. With nothing in
// the graph pointing back into it, the refcounts collapse and the islands free
// through the ordinary mechanism instead of surviving until exit.
//
// A node freed during the cascade erases itself mid-sweep and the loop picks up the
// current entry by pointer at every step, so a node whose inbound edges were cut by
// an earlier sweep is never visited after it died.
void typeGraphSweep();

inline std::unordered_set<const Type*>& typeGraphNodes() {
    static auto* nodes = new std::unordered_set<const Type*>();
    return *nodes;
}

inline bool typeGraphSweepArmed() {
    static const bool armed = (std::atexit(&typeGraphSweep), true);
    return armed;
}

inline void typeGraphRegister(const Type* node) {
    typeGraphSweepArmed();
    typeGraphNodes().insert(node);
}

inline void typeGraphUnregister(const Type* node) { typeGraphNodes().erase(node); }

}
