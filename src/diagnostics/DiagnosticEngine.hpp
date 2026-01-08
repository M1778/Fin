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
    bool hasErrors() const { return errorCount > 0; }
    int getErrorCount() const { return errorCount; }

private:
    std::string sourceCode;
    std::string filename;
    int errorCount = 0;
    std::vector<std::string> lines;
    std::vector<std::string> keywords;
    std::vector<std::string> types;

    void splitLines();
    std::string getLine(int lineNum);
    
    std::string extractTokenText(const fin::location& loc);
    std::string getPreviousWord(const fin::location& loc);
    fin::location getPreviousWordLoc(const fin::location& loc);
    
    std::string checkTypo(const std::string& word);
    
    void printContext(const fin::location& loc);
    void printHighlightedLine(const std::string& line);
};

}