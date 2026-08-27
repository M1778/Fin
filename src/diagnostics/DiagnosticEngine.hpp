#pragma once
#include <string>
#include <vector>
#include <iostream>
#include "location.hh"

namespace fin {

enum class DiagnosticSeverity { Error, Warning, Note };

// How diagnostics are rendered. `Human` is the terminal renderer and carries no
// compatibility promise. `Json` is JSONL on stderr and does: keys may be added,
// never removed or retyped (ADR 0009).
enum class DiagnosticFormat { Human, Json };

enum class ColorMode { Auto, Always, Never };

// Reserved now, populated in wave 4. A library may inject code at an event
// point, so a diagnostic can name a source location the user never wrote. When
// that happens the diagnostic also names the handler responsible and the event
// point that fired it. Empty for every diagnostic the compiler raises on its
// own, which today is all of them.
struct DiagnosticAttribution {
    std::string handler;  // the @special function that injected the code
    std::string event;    // the event point that fired the handler
    bool empty() const { return handler.empty() && event.empty(); }
};

struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string message;
    std::string file;
    // 0 means "no source location" — a diagnostic about the invocation rather
    // than about a place in a file.
    int line = 0;
    int column = 0;
    int endLine = 0;
    int endColumn = 0;
    std::string help;
    DiagnosticAttribution attribution;
};

class DiagnosticEngine {
public:
    DiagnosticEngine(std::string sourceCode, std::string filename = "<input>");

    void reportError(const fin::location& loc, const std::string& msg);
    void reportError(const fin::location& loc, const std::string& msg,
                     const DiagnosticAttribution& attribution);
    // A diagnostic with no source location: a file that could not be read, a
    // module that could not be found, or a mistake in the command line itself.
    // `file` is emitted as null and `line` as 0 for these, which is what the
    // consumer reads as "this is about the invocation, not about a place".
    void reportError(const std::string& msg);
    void reportError(const std::string& msg, const std::string& help);
    void reportWarning(const fin::location& loc, const std::string& msg);

    // Takes over another engine's counts. A module is diagnosed by an engine of its
    // own so its diagnostics can point into its own source, but it prints to the same
    // stream, so what it counted has to end up here -- otherwise the trailing summary
    // contradicts the diagnostics a consumer just read off that stream.
    void absorbCountsOf(const DiagnosticEngine& other) {
        errorCount += other.errorCount;
        warningCount += other.warningCount;
    }

    bool hasErrors() const { return errorCount > 0; }
    int getErrorCount() const { return errorCount; }
    int getWarningCount() const { return warningCount; }
    const std::vector<Diagnostic>& getDiagnostics() const { return records; }

    void setFormat(DiagnosticFormat f) { format = f; }
    DiagnosticFormat getFormat() const { return format; }

    // `Auto` means colour when stderr is a tty and NO_COLOR is unset.
    void setColorMode(ColorMode m);
    bool usesColor() const { return color; }

    void setSource(std::string src, std::string fname);

    // Closes the stream. In JSON mode this writes the trailing summary object;
    // in human mode it writes nothing. `exitCode` is the process exit code the
    // driver is about to return, so a consumer can tell "finished clean" from
    // "died before reporting".
    void emitSummary(int exitCode);

    // Progress chatter. Goes to stderr because stdout is reserved, and is
    // suppressed entirely in JSON mode because no non-JSON byte may reach
    // stderr on that path.
    void note(const std::string& text);
    void success(const std::string& text);

    static bool colorAvailable();

private:
    std::string sourceCode;
    std::string filename;
    int errorCount = 0;
    int warningCount = 0;
    std::vector<std::string> lines;
    std::vector<std::string> keywords;
    std::vector<std::string> types;
    std::vector<Diagnostic> records;
    DiagnosticFormat format = DiagnosticFormat::Human;
    ColorMode colorMode = ColorMode::Auto;
    bool color = false;

    void splitLines();

    // Makes one source line safe to write to a terminal, in place and without changing
    // its length. See the definition for why the length matters.
    static void sanitiseForDisplay(std::string& line);
    std::string getLine(int lineNum);

    std::string extractTokenText(const fin::location& loc);
    std::string getPreviousWord(const fin::location& loc);
    fin::location getPreviousWordLoc(const fin::location& loc);

    std::string checkTypo(const std::string& word);

    void emit(const Diagnostic& d);
    void emitHuman(const Diagnostic& d);
    void emitJson(const Diagnostic& d);

    void printContext(int line, int beginCol, int endCol);
    void printHighlightedLine(const std::string& line);
};

// The lexer's catch-all rule reports through here rather than writing to
// std::cerr itself. `finc` installs the engine for the translation unit it is
// about to lex; when none is installed the character is still reported, so a
// unit test that lexes without a driver does not lose it.
void setLexerDiagnostics(DiagnosticEngine* engine);
DiagnosticEngine* getLexerDiagnostics();
void reportLexerError(const fin::location& loc, const char* text);

}
