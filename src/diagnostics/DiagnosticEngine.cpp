#include "DiagnosticEngine.hpp"
#include "../utils/Levenshtein.hpp"
#include <sstream>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fmt/core.h>
#include <fmt/color.h>

#ifdef _WIN32
#include <io.h>
#define FIN_ISATTY _isatty
#define FIN_FILENO _fileno
#else
#include <unistd.h>
#define FIN_ISATTY isatty
#define FIN_FILENO fileno
#endif

namespace fin {

namespace {

// Every byte a diagnostic produces goes to stderr; stdout is reserved.
void put(bool color, const fmt::text_style& style, const std::string& text) {
    if (color) fmt::print(stderr, style, "{}", text);
    else fmt::print(stderr, "{}", text);
}

void putPlain(const std::string& text) {
    fmt::print(stderr, "{}", text);
}

// A JSON string has to be valid UTF-8, and a diagnostic message can carry an
// arbitrary byte out of the source — the lexer's catch-all matches one byte at a
// time, so a multi-byte character arrives split. Any byte that is not part of a
// well-formed UTF-8 sequence becomes U+FFFD, so no diagnostic can produce a line
// the consumer cannot parse.
int utf8SequenceLength(const std::string& in, size_t i) {
    unsigned char c = (unsigned char)in[i];
    int len = 0;
    unsigned int cp = 0;
    if (c < 0x80) return 1;
    else if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1Fu; }
    else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0Fu; }
    else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07u; }
    else return 0;

    if (i + (size_t)len > in.size()) return 0;
    for (int k = 1; k < len; ++k) {
        unsigned char cc = (unsigned char)in[i + k];
        if ((cc & 0xC0) != 0x80) return 0;
        cp = (cp << 6) | (cc & 0x3Fu);
    }
    // Reject overlong encodings, surrogates and out-of-range code points.
    if (len == 2 && cp < 0x80) return 0;
    if (len == 3 && cp < 0x800) return 0;
    if (len == 4 && cp < 0x10000) return 0;
    if (cp > 0x10FFFF) return 0;
    if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
    return len;
}

std::string jsonEscape(const std::string& in) {
    std::string out;
    out.reserve(in.size() + 8);
    for (size_t i = 0; i < in.size();) {
        unsigned char c = (unsigned char)in[i];
        if (c < 0x80) {
            switch (c) {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                case '\b': out += "\\b";  break;
                case '\f': out += "\\f";  break;
                default:
                    if (c < 0x20) out += fmt::format("\\u{:04x}", (int)c);
                    else out += (char)c;
            }
            ++i;
            continue;
        }
        int len = utf8SequenceLength(in, i);
        if (len == 0) {
            out += "\\ufffd";   // U+FFFD REPLACEMENT CHARACTER
            ++i;
            continue;
        }
        out.append(in, i, (size_t)len);
        i += (size_t)len;
    }
    return out;
}

std::string jsonString(const std::string& s) {
    return "\"" + jsonEscape(s) + "\"";
}

// A key whose value is absent ships as JSON null rather than being omitted, so
// a consumer never has to distinguish "missing key" from "no value".
std::string jsonOptional(const std::string& s) {
    return s.empty() ? std::string("null") : jsonString(s);
}

const char* severityName(DiagnosticSeverity s) {
    switch (s) {
        case DiagnosticSeverity::Error:   return "error";
        case DiagnosticSeverity::Warning: return "warning";
        case DiagnosticSeverity::Note:    return "note";
    }
    return "error";
}

DiagnosticEngine* g_lexerDiagnostics = nullptr;

} // namespace

void setLexerDiagnostics(DiagnosticEngine* engine) { g_lexerDiagnostics = engine; }
DiagnosticEngine* getLexerDiagnostics() { return g_lexerDiagnostics; }

void reportLexerError(const fin::location& loc, const char* text) {
    std::string what = text ? text : "";
    // The catch-all rule matches exactly one byte, so a non-null `text` that reads as the
    // empty string held the one byte a `const char*` cannot carry: a NUL. Naming it as ''
    // hid the byte in the very message that rejected it.
    if (text && what.empty()) what = std::string(1, '\0');
    // The byte is rendered, not embedded: the catch-all rule matches one byte at
    // a time, so a multi-byte character arrives here split and pasting it into a
    // message produces a diagnostic that is not valid UTF-8.
    std::string shown;
    for (unsigned char c : what) {
        if (c >= 0x20 && c < 0x7F) shown += (char)c;
        else shown += fmt::format("\\x{:02x}", (int)c);
    }
    std::string msg = fmt::format("unrecognised byte in source: '{}'", shown);
    if (g_lexerDiagnostics) {
        g_lexerDiagnostics->reportError(loc, msg);
    } else {
        // No engine installed. Still stderr, still one line, so the byte is
        // never silently dropped the way lexer.l used to drop it.
        fmt::print(stderr, "error: {}\n", msg);
    }
}

// Colour precedence, most specific first:
//   1. an explicit --color=always|never on the command line
//   2. NO_COLOR in the environment, whatever its value including empty
//   3. isatty(stderr)
// Only 2 and 3 are decided here; 1 is decided by setColorMode. A machine
// consumer never passes --color=always, so the flag winning cannot surprise one,
// and a human whose profile sets NO_COLOR can still ask for colour once.
bool DiagnosticEngine::colorAvailable() {
    if (std::getenv("NO_COLOR") != nullptr) return false;
    return FIN_ISATTY(FIN_FILENO(stderr)) != 0;
}

DiagnosticEngine::DiagnosticEngine(std::string source, std::string fname)
    : sourceCode(std::move(source)), filename(std::move(fname)) {
    splitLines();
    color = colorAvailable();

    keywords = {
        "fun", "struct", "enum", "let", "const", "bez", "beton",
        "if", "else", "elseif", "while", "for", "foreach", "return", "break",
        "continue", "import", "sizeof", "typeof", "new", "delete",
        "cast", "interface", "pub", "priv", "static", "macro", "operator",
        "from", "as", "true", "false", "null", "self", "super"
    };

    types = {
        "int", "float", "char", "void", "bool", "string", "noret", "auto", "Self",
        "long", "double", "short", "uint", "ulong", "ushort"
    };
}

void DiagnosticEngine::setColorMode(ColorMode m) {
    colorMode = m;
    switch (m) {
        case ColorMode::Always: color = true; break;
        case ColorMode::Never:  color = false; break;
        case ColorMode::Auto:   color = colorAvailable(); break;
    }
}

void DiagnosticEngine::setSource(std::string src, std::string fname) {
    sourceCode = std::move(src);
    filename = std::move(fname);
    lines.clear();
    splitLines();
}

void DiagnosticEngine::splitLines() {
    std::stringstream ss(sourceCode);
    std::string line;
    while (std::getline(ss, line)) {
        // Tolerate a stray CR so a column never counts one.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        sanitiseForDisplay(line);
        lines.push_back(line);
    }
}

// A rendered snippet is the source line itself, so every byte of the source is a byte the
// compiler writes to a terminal -- and a control byte is not inert there. An interior CR
// returns the cursor to column 0, so whatever the source wrote after it overwrites the
// diagnostic that was just printed; an ESC begins a sequence the terminal executes. Both
// let a .fin file decide what its own diagnostics appear to say. Doing this here rather
// than at each print site covers `help` text too, which quotes words lifted out of these
// same lines.
//
// One byte in, one byte out: the caret is placed by counting bytes along the line
// (`printContext`), so escaping a byte into `\x1b` would point the caret at the wrong
// column. Rendering the byte properly *and* keeping the caret on it means teaching the
// caret about display width, which tabs already need and neither has yet.
//
// Bytes at or above 0x80 are left alone: they are UTF-8 continuation bytes in string
// literals and comments, and replacing them would corrupt text that is perfectly valid.
void DiagnosticEngine::sanitiseForDisplay(std::string& line) {
    for (char& c : line) {
        const unsigned char b = (unsigned char)c;
        if (b == '\t') continue;               // real indentation; replacing it mangles code
        if (b < 0x20 || b == 0x7F) c = '?';
    }
}

std::string DiagnosticEngine::getLine(int lineNum) {
    if (lineNum > 0 && lineNum <= (int)lines.size()) {
        return lines[lineNum - 1];
    }
    return "";
}

std::string DiagnosticEngine::extractTokenText(const fin::location& loc) {
    std::string line = getLine(loc.begin.line);
    if (line.empty()) return "";
    int start = loc.begin.column - 1;
    int end = loc.end.column - 1;
    if (start >= 0 && end <= (int)line.length() && start < end) {
        return line.substr(start, end - start);
    }
    return "";
}

std::string DiagnosticEngine::getPreviousWord(const fin::location& loc) {
    std::string line = getLine(loc.begin.line);
    if (line.empty()) return "";

    int cursor = loc.begin.column - 2;
    while (cursor >= 0 && std::isspace((unsigned char)line[cursor])) cursor--;
    if (cursor < 0) return "";

    int end = cursor + 1;
    while (cursor >= 0 && (std::isalnum((unsigned char)line[cursor]) || line[cursor] == '_')) cursor--;

    return line.substr(cursor + 1, end - (cursor + 1));
}

fin::location DiagnosticEngine::getPreviousWordLoc(const fin::location& loc) {
    std::string line = getLine(loc.begin.line);
    if (line.empty()) return loc;

    int cursor = loc.begin.column - 2;
    while (cursor >= 0 && std::isspace((unsigned char)line[cursor])) cursor--;
    if (cursor < 0) return loc;

    int endCol = cursor + 1;
    while (cursor >= 0 && (std::isalnum((unsigned char)line[cursor]) || line[cursor] == '_')) cursor--;
    int startCol = cursor + 1;

    fin::location newLoc = loc;
    newLoc.begin.column = startCol + 1;
    newLoc.end.column = endCol + 1;
    return newLoc;
}

std::string DiagnosticEngine::checkTypo(const std::string& word) {
    if (word.empty()) return "";
    std::string bestMatch;
    int bestDist = 100;

    for (const auto& kw : keywords) {
        int dist = utils::levenshtein_distance(word, kw);
        if (dist < bestDist) {
            bestDist = dist;
            bestMatch = kw;
        }
    }

    int threshold = (word.length() < 4) ? 1 : 2;
    if (bestDist <= threshold && bestDist < (int)word.length()) {
        return bestMatch;
    }
    return "";
}

void DiagnosticEngine::printHighlightedLine(const std::string& line) {
    std::string word;
    for (size_t i = 0; i < line.length(); ++i) {
        char c = line[i];
        if (std::isalnum((unsigned char)c) || c == '_') {
            word += c;
        } else {
            if (!word.empty()) {
                bool isKw = false;
                for (const auto& k : keywords) if (k == word) isKw = true;
                bool isType = false;
                for (const auto& t : types) if (t == word) isType = true;

                if (isKw)        put(color, fg(fmt::color::magenta) | fmt::emphasis::bold, word);
                else if (isType) put(color, fg(fmt::color::yellow), word);
                else             putPlain(word);
                word = "";
            }
            putPlain(std::string(1, c));
        }
    }
    if (!word.empty()) putPlain(word);
    putPlain("\n");
}

void DiagnosticEngine::reportError(const fin::location& loc, const std::string& msg) {
    reportError(loc, msg, DiagnosticAttribution{});
}

void DiagnosticEngine::reportError(const fin::location& loc, const std::string& msg,
                                   const DiagnosticAttribution& attribution) {
    Diagnostic d;
    d.severity = DiagnosticSeverity::Error;
    d.message = msg;
    d.file = filename;
    d.line = loc.begin.line;
    d.column = loc.begin.column;
    d.endLine = loc.end.line;
    d.endColumn = loc.end.column;
    d.attribution = attribution;

    std::string badToken = extractTokenText(loc);
    std::string suggestion = checkTypo(badToken);
    if (suggestion == badToken) suggestion = "";
    if (suggestion.empty()) {
        fin::location prevLoc = getPreviousWordLoc(loc);
        if (prevLoc.begin.column != loc.begin.column) {
            std::string prevWord = extractTokenText(prevLoc);
            std::string prevSuggestion = checkTypo(prevWord);
            if (prevSuggestion == prevWord) prevSuggestion = "";
            if (!prevSuggestion.empty()) {
                d.help = fmt::format("the word '{}' looks suspicious. Did you mean '{}'?",
                                     prevWord, prevSuggestion);
            }
        }
    } else {
        d.help = fmt::format("did you mean '{}'?", suggestion);
    }

    errorCount++;
    emit(d);
}

void DiagnosticEngine::reportError(const std::string& msg) {
    Diagnostic d;
    d.severity = DiagnosticSeverity::Error;
    d.message = msg;
    d.file = filename;
    errorCount++;
    emit(d);
}

void DiagnosticEngine::reportError(const std::string& msg, const std::string& help) {
    Diagnostic d;
    d.severity = DiagnosticSeverity::Error;
    d.message = msg;
    d.file = filename;
    d.help = help;
    errorCount++;
    emit(d);
}

void DiagnosticEngine::reportWarning(const fin::location& loc, const std::string& msg) {
    Diagnostic d;
    d.severity = DiagnosticSeverity::Warning;
    d.message = msg;
    d.file = filename;
    d.line = loc.begin.line;
    d.column = loc.begin.column;
    d.endLine = loc.end.line;
    d.endColumn = loc.end.column;
    warningCount++;
    emit(d);
}

void DiagnosticEngine::emit(const Diagnostic& d) {
    records.push_back(d);
    if (format == DiagnosticFormat::Json) emitJson(d);
    else emitHuman(d);
}

void DiagnosticEngine::emitHuman(const Diagnostic& d) {
    fmt::text_style head = d.severity == DiagnosticSeverity::Error
        ? (fg(fmt::color::red) | fmt::emphasis::bold)
        : (fg(fmt::color::yellow) | fmt::emphasis::bold);

    put(color, head, fmt::format("{}: ", severityName(d.severity)));
    put(color, fmt::text_style(fmt::emphasis::bold), d.message + "\n");

    if (d.line > 0) {
        put(color, fg(fmt::color::cornflower_blue),
            fmt::format("   --> {}:{}:{}\n", d.file, d.line, d.column));
        printContext(d.line, d.column, d.endColumn);
    } else if (!d.file.empty()) {
        put(color, fg(fmt::color::cornflower_blue), fmt::format("   --> {}\n", d.file));
    }

    if (!d.help.empty()) {
        put(color, fg(fmt::color::cyan), fmt::format("   = help: {}\n", d.help));
    }
}

void DiagnosticEngine::emitJson(const Diagnostic& d) {
    // One object per line, written as it is reported, so a compiler that dies
    // mid-run still leaves a parseable prefix (ADR 0009).
    std::string attribution = "null";
    if (!d.attribution.empty()) {
        attribution = fmt::format("{{\"handler\":{},\"event\":{}}}",
                                  jsonOptional(d.attribution.handler),
                                  jsonOptional(d.attribution.event));
    }
    fmt::print(stderr,
        "{{\"kind\":\"diagnostic\",\"severity\":\"{}\",\"code\":null,\"message\":{},"
        "\"file\":{},\"line\":{},\"column\":{},\"endLine\":{},\"endColumn\":{},"
        "\"help\":{},\"attribution\":{}}}\n",
        severityName(d.severity),
        jsonString(d.message),
        jsonOptional(d.file),
        d.line, d.column, d.endLine, d.endColumn,
        jsonOptional(d.help),
        attribution);
    std::fflush(stderr);
}

void DiagnosticEngine::emitSummary(int exitCode) {
    if (format != DiagnosticFormat::Json) return;
    fmt::print(stderr,
        "{{\"kind\":\"summary\",\"errors\":{},\"warnings\":{},\"exitCode\":{},\"status\":\"{}\"}}\n",
        errorCount, warningCount, exitCode, exitCode == 0 ? "ok" : "failed");
    std::fflush(stderr);
}

void DiagnosticEngine::note(const std::string& text) {
    if (format == DiagnosticFormat::Json) return;
    putPlain(text + "\n");
}

void DiagnosticEngine::success(const std::string& text) {
    if (format == DiagnosticFormat::Json) return;
    put(color, fg(fmt::color::green) | fmt::emphasis::bold, text + "\n");
}

void DiagnosticEngine::printContext(int lineNum, int beginCol, int endCol) {
    std::string lineContent = getLine(lineNum);
    std::string lineNumStr = std::to_string(lineNum);
    std::string padding(lineNumStr.length(), ' ');

    put(color, fg(fmt::color::cornflower_blue), fmt::format(" {} |\n", padding));
    put(color, fg(fmt::color::cornflower_blue), fmt::format(" {} | ", lineNumStr));
    printHighlightedLine(lineContent);
    put(color, fg(fmt::color::cornflower_blue), fmt::format(" {} | ", padding));

    for (int i = 1; i < beginCol; i++) putPlain(" ");

    int len = std::max(1, endCol - beginCol);
    put(color, fg(fmt::color::red) | fmt::emphasis::bold, std::string(len, '^'));
    put(color, fg(fmt::color::red) | fmt::emphasis::bold, " here\n");
}

}
