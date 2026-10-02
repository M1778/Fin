#include "EventPayloads.hpp"

#include "../ast/NodeKind.hpp"
#include "../ast/StructuralWalk.hpp"
#include "../ast/decls/Program.hpp"
#include "../ast/decls/TypeDef.hpp"
#include "../ast/exprs/Identifier.hpp"
#include "../ast/nodes/Parameter.hpp"
#include "../ast/types/TypeNode.hpp"

namespace fin::events {

const std::vector<EventPayload>& w6Payloads() {
    // docs/compiler-api.md §3.2, the W6 floor. Entry args stay a follow-up:
    // identity first, per the owner decision, and the interpreter cannot
    // carry argument values yet.
    static const std::vector<EventPayload> table = {
        {"function_entry", {{"f", "function"}}, "quote"},
        {"function_exit", {{"f", "function"}, {"exit_kind", "int"}}, "quote"},
        {"assignment",
         {{"target", "quote"}, {"value", "quote"}, {"t", "$type"}},
         "quote"},
        {"allocation_site",
         {{"t", "$type"}, {"count", "quote"}, {"dest", "quote"}},
         "quote"},
        {"delete_site", {{"t", "$type"}, {"ptr", "quote"}}, "quote"},
    };
    return table;
}

const EventPayload* findW6Payload(const std::string& event) {
    for (const auto& p : w6Payloads())
        if (p.event == event) return &p;
    return nullptr;
}

bool isW6Event(const std::string& event) { return findW6Payload(event) != nullptr; }

namespace {

// A written type spelling is the payload's when it names the same base type
// with no structure of its own: no pointer, array, generic argument or
// nullability. `const` is exempt (see the header): it binds the parameter,
// it does not change what arrives.
bool spellsPayloadType(const TypeNode* written, const std::string& expected) {
    if (!written) return false;
    return written->name == expected && written->pointer_depth == 0 &&
           !written->is_array && written->generics.empty() && !written->is_nullable;
}

std::string writtenSignature(const SpecialDeclaration& decl) {
    std::string out = "(";
    for (std::size_t i = 0; i < decl.params.size(); ++i) {
        if (i != 0) out += ", ";
        const auto& param = decl.params[i];
        out += param->name + ": ";
        out += param->type ? param->type->name : "<untyped>";
    }
    out += ") <";
    out += decl.return_type ? decl.return_type->name : "void";
    out += ">";
    return out;
}

bool signatureMatches(const SpecialDeclaration& decl, const EventPayload& payload) {
    if (decl.params.size() != payload.params.size()) return false;
    for (std::size_t i = 0; i < decl.params.size(); ++i)
        if (!spellsPayloadType(decl.params[i]->type.get(), payload.params[i].type))
            return false;
    return spellsPayloadType(decl.return_type.get(), payload.returns);
}

// The interpretability line as a walker: the first control-flow form found,
// by its source spelling. Shared so W5's check calls this rather than
// restating which four forms are control flow.
class LineWalker : public StructuralWalk {
public:
    std::string found;
    bool enter(ASTNode& node) override {
        switch (node.kind()) {
            case NodeKind::IfStatement: found = "if"; return false;
            case NodeKind::WhileLoop: found = "while"; return false;
            case NodeKind::ForLoop: found = "for"; return false;
            case NodeKind::ForeachLoop: found = "foreach"; return false;
            default: return true;
        }
    }
};

} // namespace

void checkW6HandlerPayloads(Program& program, const EventRegistry& registry,
                            DiagReporter report) {
    for (const auto& record : registry.all()) {
        const EventPayload* payload = findW6Payload(record.event);
        if (!payload) continue;
        SpecialDeclaration* decl = nullptr;
        for (auto& stmt : program.statements) {
            auto* special = dynamic_cast<SpecialDeclaration*>(stmt.get());
            if (special && special->name == record.handler) {
                decl = special;
                break;
            }
        }
        if (!decl) continue;
        if (!signatureMatches(*decl, *payload)) {
            report(*decl, "Handler '" + record.handler + "' for event '" +
                              record.event + "' has the wrong payload: expected " +
                              expectedSignature(*payload) + ", wrote " +
                              writtenSignature(*decl));
            continue;
        }
        if (decl->body) {
            LineWalker walker;
            walker.walk(decl->body.get());
            if (!walker.found.empty())
                report(*decl, "Handler '" + record.handler + "' for event '" +
                                  record.event + "' uses control flow ('" +
                                  walker.found +
                                  "'): handlers hold no control flow "
                                  "(the interpretability line)");
        }
    }
}

std::string expectedSignature(const EventPayload& payload) {
    std::string out = "(";
    for (std::size_t i = 0; i < payload.params.size(); ++i) {
        if (i != 0) out += ", ";
        out += payload.params[i].name + ": " + payload.params[i].type;
    }
    out += ") <" + payload.returns + ">";
    return out;
}

std::string spellFireTarget(const Expression& target) {
    if (const auto* id = dynamic_cast<const Identifier*>(&target)) return id->name;
    return std::string("<") + nodeKindName(target.kind()) + ">";
}

} // namespace fin::events
