#include "Preprocessor.hpp"
#include <sstream>
#include <regex>
#include <iostream>
#include <cctype>

namespace fin {

std::string Preprocessor::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t");
    if (std::string::npos == first) return "";
    size_t last = str.find_last_not_of(" \t");
    return str.substr(first, (last - first + 1));
}

bool Preprocessor::shouldProcess() {
    for (bool b : ifStack) {
        if (!b) return false;
    }
    return true;
}

std::string Preprocessor::process(const std::string& source) {
    std::stringstream ss(source);
    std::string line;
    std::string result;
    std::string currentMultiLine;
    bool inMultiLine = false;

    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\\') {
            currentMultiLine += line.substr(0, line.size() - 1);
            inMultiLine = true;
            result += "\n"; 
            continue;
        }
        
        if (inMultiLine) {
            currentMultiLine += line;
            line = currentMultiLine;
            currentMultiLine = "";
            inMultiLine = false;
        }

        std::string trimmed = trim(line);
        
        // Handle Directives
        if (trimmed.rfind("#c_", 0) == 0 || trimmed.rfind("#cdef", 0) == 0) {
            if (trimmed.rfind("#cdef", 0) == 0) {
                if (shouldProcess()) {
                    std::regex defRegex(R"(#cdef\s+(\w+)(\(([^)]*)\))?\s*(.*))");
                    std::smatch match;
                    if (std::regex_search(line, match, defRegex)) {
                        std::string name = match[1];
                        std::string paramsStr = match[3];
                        std::string body = match[4];
                        
                        MacroDef def;
                        def.body = trim(body);
                        
                        if (match[2].matched) {
                            def.isFunctionLike = true;
                            std::stringstream ps(paramsStr);
                            std::string p;
                            while(std::getline(ps, p, ',')) {
                                def.params.push_back(trim(p));
                            }
                        }
                        defines[name] = def;
                    }
                }
            }
            else if (trimmed.rfind("#c_ifdef", 0) == 0) {
                std::string name = trim(trimmed.substr(8));
                if (shouldProcess()) {
                    ifStack.push_back(defines.count(name));
                } else {
                    ifStack.push_back(false);
                }
            }
            else if (trimmed.rfind("#c_else", 0) == 0) {
                if (!ifStack.empty()) {
                    bool last = ifStack.back();
                    ifStack.pop_back();
                    if (shouldProcess()) {
                        ifStack.push_back(!last);
                    } else {
                        ifStack.push_back(false);
                    }
                }
            }
            else if (trimmed.rfind("#c_endif", 0) == 0) {
                if (!ifStack.empty()) ifStack.pop_back();
            }
            result += "\n"; 
        } 
        else {
            if (shouldProcess()) {
                result += expandMacros(line) + "\n";
            } else {
                result += "\n"; 
            }
        }
    }
    return result;
}

// Helpers for tokenization
bool isIdStart(char c) { return std::isalpha(c) || c == '_'; }
bool isIdPart(char c) { return std::isalnum(c) || c == '_'; }

std::string Preprocessor::expandMacros(std::string line) {
    int maxIterations = 100; // Prevent infinite recursion
    bool changed = true;
    
    while (changed && maxIterations-- > 0) {
        changed = false;
        std::string newLine;
        newLine.reserve(line.size() * 2);
        
        size_t i = 0;
        while (i < line.size()) {
            // 1. Skip Strings "..."
            if (line[i] == '"') {
                newLine += line[i++];
                while (i < line.size() && line[i] != '"') {
                    if (line[i] == '\\' && i + 1 < line.size()) newLine += line[i++];
                    newLine += line[i++];
                }
                if (i < line.size()) newLine += line[i++];
                continue;
            }
            
            // 2. Skip Chars '...'
            if (line[i] == '\'') {
                newLine += line[i++];
                while (i < line.size() && line[i] != '\'') {
                    if (line[i] == '\\' && i + 1 < line.size()) newLine += line[i++];
                    newLine += line[i++];
                }
                if (i < line.size()) newLine += line[i++];
                continue;
            }
            
            // 3. Check Identifier
            if (isIdStart(line[i])) {
                size_t start = i;
                while (i < line.size() && isIdPart(line[i])) i++;
                std::string word = line.substr(start, i - start);
                
                if (defines.count(word)) {
                    const auto& def = defines.at(word);
                    
                    if (def.isFunctionLike) {
                        // Look ahead for '('
                        size_t j = i;
                        while (j < line.size() && std::isspace(line[j])) j++;
                        
                        if (j < line.size() && line[j] == '(') {
                            // Parse Arguments
                            j++; // Skip '('
                            std::vector<std::string> args;
                            std::string currentArg;
                            int parenDepth = 1;
                            
                            while (j < line.size() && parenDepth > 0) {
                                if (line[j] == '(') parenDepth++;
                                else if (line[j] == ')') parenDepth--;
                                
                                if (line[j] == ',' && parenDepth == 1) {
                                    args.push_back(trim(currentArg));
                                    currentArg = "";
                                    j++;
                                    continue;
                                }
                                
                                if (parenDepth > 0) currentArg += line[j++];
                            }
                            
                            if (parenDepth == 0) {
                                args.push_back(trim(currentArg));
                                j++; // Skip closing ')'
                                
                                // Expand Function Macro
                                if (args.size() == def.params.size()) {
                                    std::string expansion = def.body;
                                    // Replace params in body (Simple regex for now)
                                    for(size_t k=0; k<def.params.size(); ++k) {
                                        std::regex pRegex("\\b" + def.params[k] + "\\b");
                                        expansion = std::regex_replace(expansion, pRegex, args[k]);
                                    }
                                    newLine += expansion;
                                    i = j; // Advance cursor past macro call
                                    changed = true;
                                    continue;
                                }
                            }
                        }
                    } else {
                        // Object-like Macro
                        newLine += def.body;
                        changed = true;
                        continue;
                    }
                }
                newLine += word;
            } else {
                newLine += line[i++];
            }
        }
        if (changed) line = newLine;
    }
    return line;
}

} // namespace fin