#include "DiagnosticEngine.hpp"
#include "../utils/Levenshtein.hpp"
#include <sstream>
#include <cctype>
#include <fmt/core.h>
#include <fmt/color.h>

namespace fin {

DiagnosticEngine::DiagnosticEngine(std::string source, std::string fname) 
    : sourceCode(std::move(source)), filename(std::move(fname)) {
    splitLines();
    
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

void DiagnosticEngine::splitLines() {
    std::stringstream ss(sourceCode);
    std::string line;
    while (std::getline(ss, line)) {
        lines.push_back(line);
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
    while (cursor >= 0 && std::isspace(line[cursor])) cursor--;
    if (cursor < 0) return ""; 
    
    int end = cursor + 1;
    while (cursor >= 0 && (std::isalnum(line[cursor]) || line[cursor] == '_')) cursor--;
    
    return line.substr(cursor + 1, end - (cursor + 1));
}

fin::location DiagnosticEngine::getPreviousWordLoc(const fin::location& loc) {
    std::string line = getLine(loc.begin.line);
    if (line.empty()) return loc;
    
    int cursor = loc.begin.column - 2;
    while (cursor >= 0 && std::isspace(line[cursor])) cursor--;
    if (cursor < 0) return loc;
    
    int endCol = cursor + 1;
    while (cursor >= 0 && (std::isalnum(line[cursor]) || line[cursor] == '_')) cursor--;
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
        if (std::isalnum(c) || c == '_') {
            word += c;
        } else {
            if (!word.empty()) {
                bool isKw = false;
                for(const auto& k : keywords) if(k == word) isKw = true;
                bool isType = false;
                for(const auto& t : types) if(t == word) isType = true;

                if (isKw) fmt::print(fg(fmt::color::magenta) | fmt::emphasis::bold, "{}", word);
                else if (isType) fmt::print(fg(fmt::color::yellow), "{}", word);
                else fmt::print("{}", word);
                word = "";
            }
            fmt::print("{}", c);
        }
    }
    if (!word.empty()) {
        fmt::print("{}", word);
    }
    fmt::print("\n");
}

void DiagnosticEngine::reportError(const fin::location& loc, const std::string& msg) {
    fmt::print(fg(fmt::color::red) | fmt::emphasis::bold, "error: ");
    fmt::print(fmt::emphasis::bold, "{}\n", msg);
    
    fmt::print(fg(fmt::color::cornflower_blue), "   --> {}:{}:{}\n", filename, loc.begin.line, loc.begin.column);
    
    printContext(loc);

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
                fmt::print(fg(fmt::color::cornflower_blue), "   --> {}:{}:{}\n", filename, prevLoc.begin.line, prevLoc.begin.column);
                printContext(prevLoc); 
                fmt::print(fg(fmt::color::cyan), "   = help: The word '{}' looks suspicious. Did you mean '{}'?\n", prevWord, prevSuggestion);
                return;
            }
        }
    }

    if (!suggestion.empty()) {
        fmt::print(fg(fmt::color::cyan), "   = help: Did you mean '{}'?\n", suggestion);
    }
}

void DiagnosticEngine::printContext(const fin::location& loc) {
    int lineNum = loc.begin.line;
    std::string lineContent = getLine(lineNum);
    std::string lineNumStr = std::to_string(lineNum);
    std::string padding(lineNumStr.length(), ' ');
    
    fmt::print(fg(fmt::color::cornflower_blue), " {} |\n", padding);
    fmt::print(fg(fmt::color::cornflower_blue), " {} | ", lineNumStr);
    printHighlightedLine(lineContent);
    fmt::print(fg(fmt::color::cornflower_blue), " {} | ", padding);
    
    int col = loc.begin.column;
    for(int i=1; i<col; i++) fmt::print(" ");
    
    int len = std::max(1, loc.end.column - loc.begin.column);
    for(int i=0; i<len; i++) fmt::print(fg(fmt::color::red) | fmt::emphasis::bold, "^");
    
    fmt::print(fg(fmt::color::red) | fmt::emphasis::bold, " here\n");
}

}