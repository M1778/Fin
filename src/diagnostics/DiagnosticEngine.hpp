#pragma once
#include <string>
#include <vector>
#include <iostream>
#include "location.hh"

namespace fin {

class DiagnosticEngine {
public:
    DiagnosticEngine(std::string sourceCode, std::string filename = "<input>");
    void reportError(const fin::location& loc, const std::string& msg);

private:
    std::string sourceCode;
    std::string filename;
    std::vector<std::string> lines;
    std::vector<std::string> keywords;
    std::vector<std::string> types;

    void splitLines();
    std::string getLine(int lineNum);
    
    std::string extractTokenText(const fin::location& loc);
    // New: Extract the word immediately preceding the error location
    std::string getPreviousWord(const fin::location& loc);
    fin::location getPreviousWordLoc(const fin::location& loc);
    
    std::string checkTypo(const std::string& word);
    
    void printContext(const fin::location& loc);
    // New: Print line with syntax highlighting
    void printHighlightedLine(const std::string& line);
};

}