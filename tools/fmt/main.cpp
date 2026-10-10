// finfmt — canonical whitespace formatter for Fin sources (issue #38).
//
// Usage: finfmt [--check] <file.fin>...
//        --check: print a diff for each file that would change and exit 1;
//                 change nothing. Without it, rewrite drifted files in place.
//
// Canonical rules (MVP, deliberately narrow — see below for what is skipped):
//   1. Parse gate: each file is run through the real preprocessor + bison
//      parser first (the same front end `finc` uses, no new grammar). A file
//      that does not parse is refused with the parser's own diagnostic and
//      never rewritten.
//   2. 4-space indents from `{}` nesting. `namespace X {` scopes are
//      transparent (corpus convention: namespace contents sit at column 0).
//   3. A lone `pub:`/`priv:` line sits at its block indent; the members that
//      follow sit 2 deeper until the scope closes.
//   4. A line continued from the previous one (previous code ends with one of
//      `, ( [ { = .` or `=>` `&&` `||`, or with an operator char
//      `+ - * / % & | < > ! ^ ~ ? :`, or the previous raw line ends with the
//      preprocessor's `\` joiner) sits 2 deeper. Label and continuation never
//      stack: one extra level at most.
//   5. A line starting with `}` is dedented to the scope it closes.
//   6. No trailing whitespace, no tabs in indentation, LF endings, at most one
//      blank line in a row, no leading blank lines, exactly one trailing
//      newline (an all-blank file formats to empty).
//   Skipped on purpose: intra-line spacing (the corpus is already consistent
//   there, and comments/strings are full of counterexamples — reformatting
//   inside the line buys churn, not signal).
//
// Guarantees: whitespace-only by construction (no non-whitespace byte is ever
// touched — prove with `tr -d ' \t\r\n'` before/after), and idempotent
// (indentation is a pure function of the stripped line text, so a second run
// is a no-op — prove with `finfmt --check` right after formatting).
//
// Exits: 0 everything parsed (and, without --check, rewritten); 1 --check
// found drift; 2 usage, I/O, or a file that does not parse.

#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "ast/ASTNode.hpp"
#include "diagnostics/DiagnosticEngine.hpp"
#include "lexer/lexer.hpp"
#include "parser.hpp"
#include "preprocessor/Preprocessor.hpp"

namespace fin {
// Declared in the bison prologue (src/parser/parser.y); the driver refers to
// it the same way rather than through a header.
extern std::unique_ptr<Program> root;
}

namespace {

std::string readWholeFile(const std::string& path, bool& ok) {
    std::ifstream t(path, std::ios::binary);
    if (!t.is_open()) {
        ok = false;
        return "";
    }
    std::ostringstream buffer;
    buffer << t.rdbuf();
    ok = !t.bad();
    return buffer.str();
}

bool writeWholeFile(const std::string& path, const std::string& text) {
    std::ofstream o(path, std::ios::binary | std::ios::trunc);
    if (!o.is_open()) return false;
    o << text;
    o.flush();
    return static_cast<bool>(o);
}

// Mirror of Driver::compile steps 1-3 (read, preprocess, parse) without
// semantic analysis or imports: a file that parses is formattable, and a file
// that does not is refused before any byte is rewritten.
bool parsesCleanly(const std::string& raw, const std::string& path) {
    fin::Preprocessor pp;
    const std::string code = pp.process(raw);
    fin::DiagnosticEngine diag("", path);
    diag.setSource(code, path);
    fin::setLexerDiagnostics(&diag);
    fin::root = nullptr;
    fin::reset_lexer_location();
    YY_BUFFER_STATE buffer =
        yy_scan_bytes(code.data(), static_cast<int>(code.size()));
    fin::parser parser(diag);
    const int res = parser.parse();
    yy_delete_buffer(buffer);
    fin::setLexerDiagnostics(nullptr);
    const bool ok = (res == 0 && fin::root && !diag.hasErrors());
    fin::root.reset();
    return ok;
}

struct LineScan {
    int opens = 0;          // '{' outside strings/comments
    int closes = 0;         // '}' outside strings/comments
    int leadingCloses = 0;  // run of '}' starting the code part
    std::string code;       // line with comments removed, strings intact
};

// One pass over a line. Block comments nest (lexer.l) and can span lines, so
// the nesting depth is carried across lines; strings and char literals honor
// backslash escapes (lexer.l string/char rules).
LineScan scanLine(const std::string& line, int& blockDepth) {
    LineScan out;
    std::string code;
    code.reserve(line.size());
    bool inString = false;
    bool inChar = false;
    bool lineComment = false;
    bool sawCode = false;  // any non-space code char yet (for leadingCloses)
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (lineComment) break;
        if (inString) {
            code += c;
            if (c == '\\' && i + 1 < line.size()) code += line[++i];
            else if (c == '"') inString = false;
            continue;
        }
        if (inChar) {
            code += c;
            if (c == '\\' && i + 1 < line.size()) code += line[++i];
            else if (c == '\'') inChar = false;
            continue;
        }
        if (blockDepth > 0) {
            if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
                ++blockDepth;
                ++i;
            } else if (c == '*' && i + 1 < line.size() && line[i + 1] == '/') {
                --blockDepth;
                ++i;
            }
            continue;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            lineComment = true;
            ++i;
            continue;
        }
        if (c == '/' && i + 1 < line.size() && line[i + 1] == '*') {
            blockDepth = 1;
            ++i;
            continue;
        }
        if (c == '"') {
            inString = true;
            code += c;
            continue;
        }
        if (c == '\'') {
            inChar = true;
            code += c;
            continue;
        }
        if (c == '{') ++out.opens;
        if (c == '}') {
            ++out.closes;
            if (!sawCode) ++out.leadingCloses;
        }
        // `%` is not code for indentation purposes: an attribute-block close
        // `}%` must read as a leading close, the same as a bare `}`.
        if (c != ' ' && c != '\t' && c != '\r' && c != '%') sawCode = true;
        code += c;
    }
    out.code = code;
    return out;
}

std::string rtrim(const std::string& s) {
    size_t n = s.size();
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'))
        --n;
    return s.substr(0, n);
}

std::string stripped(const std::string& s) {
    size_t b = 0;
    while (b < s.size() && (s[b] == ' ' || s[b] == '\t')) ++b;
    return rtrim(s.substr(b));
}

bool isLabelLine(const std::string& code) {
    const std::string t = stripped(code);
    return t == "pub:" || t == "priv:";
}

bool startsNamespace(const std::string& code) {
    const std::string t = stripped(code);
    return t.rfind("namespace", 0) == 0 &&
           (t.size() == 9 || t[9] == ' ' || t[9] == '\t' || t[9] == '{');
}

// Previous code line continues onto the next when it ends with an opener
// (but not `{`, which opens a block scope instead), a comma, `=`, `.`, a
// two-char operator, or any other operator character that cannot end a
// statement. `;`, `)`, `]`, `}`, `{`, and closing quotes never continue;
// anything else (identifiers, literals, keywords) ends the line.
bool continuesNext(const std::string& code) {
    const std::string t = rtrim(code);
    if (t.empty()) return false;
    // An attribute-block close never continues, even though `%` alone (a
    // modulo split) does.
    if (t.size() >= 2 && t.substr(t.size() - 2) == "}%") return false;
    const char last = t.back();
    if (last == ',' || last == '(' || last == '[' || last == '=' ||
        last == '.')
        return true;
    if (t.size() >= 2) {
        const std::string tail = t.substr(t.size() - 2);
        if (tail == "=>" || tail == "&&" || tail == "||") return true;
    }
    return last == '+' || last == '-' || last == '*' || last == '/' ||
           last == '%' || last == '&' || last == '|' || last == '<' ||
           last == '>' || last == '!' || last == '^' || last == '~' ||
           last == '?' || last == ':';
}

struct Scope {
    bool transparent = false;  // a `namespace`/`%{` scope: adds no indent
    bool labeled = false;      // a `pub:`/`priv:` line was seen at this level
    int openerIndent = 0;      // emitted indent of the line that opened it
};

std::string formatText(const std::string& raw) {
    // Split into lines, normalising CRLF (the preprocessor drops `\r` too).
    std::vector<std::string> lines;
    {
        std::string cur;
        for (char c : raw) {
            if (c == '\n') {
                lines.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
        if (!cur.empty() || (!raw.empty() && raw.back() == '\n')) {
            // A trailing partial line, or nothing after a final newline.
            if (!cur.empty()) lines.push_back(cur);
        }
    }

    std::vector<std::string> out;
    out.reserve(lines.size());
    std::vector<Scope> stack;
    auto units = [&]() {
        int u = 0;
        for (const auto& s : stack)
            if (!s.transparent) ++u;
        return u;
    };
    int blockDepth = 0;
    bool cont = false;
    bool prevBackslash = false;
    int blanks = 0;
    bool seenContent = false;

    for (const std::string& rawLine : lines) {
        // Tabs in indentation become 4 spaces; anything else on the line is
        // untouched. Non-whitespace bytes are never modified anywhere here.
        std::string detabbed;
        detabbed.reserve(rawLine.size());
        size_t i = 0;
        while (i < rawLine.size() && (rawLine[i] == ' ' || rawLine[i] == '\t')) {
            detabbed += (rawLine[i] == '\t') ? "    " : " ";
            ++i;
        }
        detabbed += rawLine.substr(i);

        const LineScan scan = scanLine(detabbed, blockDepth);
        // Indentation uses the raw line's own stripped text, so comment-only
        // lines keep their exact bytes (a comment is never reworded).
        const std::string own = stripped(detabbed);
        if (own.empty()) {
            if (!seenContent) continue;  // no leading blank lines
            if (++blanks > 1) continue;  // at most one blank line in a row
            out.emplace_back("");
            continue;
        }
        blanks = 0;
        seenContent = true;

        int pops = scan.leadingCloses;
        if (pops > static_cast<int>(stack.size())) pops = static_cast<int>(stack.size());
        int lineUnits = units();
        for (int k = 0; k < pops; ++k)
            if (!stack[stack.size() - 1 - k].transparent) --lineUnits;
        if (lineUnits < 0) lineUnits = 0;

        int indent;
        if (scan.leadingCloses > 0) {
            // A closing line sits at its opener's indent: continuation and
            // label extras belong to members, never to the `}` itself — and
            // a scope opened on a labeled (`+2`) line closes at that line.
            indent = (!stack.empty()) ? stack.back().openerIndent : 0;
        } else if (isLabelLine(scan.code)) {
            indent = 4 * lineUnits;
            if (!stack.empty()) stack.back().labeled = true;
        } else {
            const bool extra =
                (!stack.empty() && stack.back().labeled) || cont || prevBackslash;
            indent = 4 * lineUnits + (extra ? 2 : 0);
        }
        out.push_back(std::string(static_cast<size_t>(indent), ' ') + own);

        for (int k = 0; k < pops; ++k) stack.pop_back();
        const int pushes = scan.opens - scan.closes + pops;
        // A `{` opened by `namespace` or by an attribute block `%{` does not
        // indent its contents (corpus convention); any further `{` on the
        // same line opens an ordinary scope.
        const bool clear = startsNamespace(scan.code) ||
                           scan.code.find("%{") != std::string::npos;
        for (int k = 0; k < pushes; ++k)
            stack.push_back(Scope{clear && k == 0, false, indent});

        // Comment-only lines leave the continuation state alone, so a comment
        // between a split statement's lines does not break the continuation.
        if (!stripped(scan.code).empty()) cont = continuesNext(scan.code);
        prevBackslash = !rawLine.empty() && rawLine.back() == '\\';
    }

    if (!seenContent) return "";
    std::string result;
    for (const auto& l : out) {
        result += l;
        result += '\n';
    }
    return result;
}

// Minimal unified diff (3 lines of context), enough for `--check` output.
std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> v;
    std::string cur;
    for (char c : s) {
        if (c == '\n') {
            v.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) v.push_back(cur);
    return v;
}

std::string unifiedDiff(const std::string& path, const std::string& oldText,
                        const std::string& newText) {
    const std::vector<std::string> a = splitLines(oldText);
    const std::vector<std::string> b = splitLines(newText);
    // One hunk from the first to the last differing line (3 lines of
    // context): formatter drift comes in indentation runs, and a single
    // window keeps --check output readable without an LCS table.
    size_t pre = 0;
    while (pre < a.size() && pre < b.size() && a[pre] == b[pre]) ++pre;
    size_t suf = 0;
    while (suf < a.size() - pre && suf < b.size() - pre &&
           a[a.size() - 1 - suf] == b[b.size() - 1 - suf])
        ++suf;
    const size_t a0 = pre, a1 = a.size() - suf;
    const size_t b0 = pre, b1 = b.size() - suf;
    std::ostringstream diff;
    diff << "--- " << path << "\n+++ " << path << " (formatted)\n";
    diff << "@@ -" << (a0 + 1) << "," << (a1 - a0) << " +" << (b0 + 1) << ","
         << (b1 - b0) << " @@\n";
    constexpr size_t kCtx = 3;
    const size_t c0 = (a0 > kCtx) ? a0 - kCtx : 0;
    for (size_t k = c0; k < a0; ++k) diff << " " << a[k] << "\n";
    for (size_t k = a0; k < a1; ++k) diff << "-" << a[k] << "\n";
    for (size_t k = b0; k < b1; ++k) diff << "+" << b[k] << "\n";
    for (size_t k = a1; k < a1 + kCtx && k < a.size(); ++k)
        diff << " " << a[k] << "\n";
    return diff.str();
}

}  // namespace

int main(int argc, char** argv) {
    bool check = false;
    std::vector<std::string> files;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--check") {
            check = true;
        } else if (a == "--help" || a == "-h") {
            std::cout << "Usage: finfmt [--check] <file.fin>...\n";
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "finfmt: unknown flag: " << a << "\n";
            return 2;
        } else {
            files.push_back(a);
        }
    }
    if (files.empty()) {
        std::cerr << "finfmt: no input files\n";
        return 2;
    }

    int refused = 0;
    int drifted = 0;
    for (const auto& path : files) {
        bool ok = false;
        const std::string raw = readWholeFile(path, ok);
        if (!ok) {
            std::cerr << "finfmt: cannot read: " << path << "\n";
            refused = 2;
            continue;
        }
        if (!parsesCleanly(raw, path)) {
            std::cerr << "finfmt: refusing " << path
                      << ": does not parse (diagnostic above)\n";
            refused = 2;
            continue;
        }
        const std::string formatted = formatText(raw);
        if (formatted == raw) continue;
        ++drifted;
        if (check) {
            std::cout << "would reformat: " << path << "\n";
            std::cout << unifiedDiff(path, raw, formatted);
        } else {
            if (!writeWholeFile(path, formatted)) {
                std::cerr << "finfmt: cannot write: " << path << "\n";
                refused = 2;
            } else {
                std::cout << "reformatted: " << path << "\n";
            }
        }
    }
    if (refused != 0) return refused;
    return (check && drifted > 0) ? 1 : 0;
}
