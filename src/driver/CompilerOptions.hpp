#pragma once
#include <string>
#include <vector>

namespace fin {

struct CompilerOptions {
    std::string inputFile;
    std::string outputPath = "a.out";
    
    std::vector<std::string> includePaths;
    
    bool debugLexer = false;
    bool debugParser = false;
    bool debugSema = false;
    bool debugCodegen = false;
    
    bool skipSemantics = false;
    bool skipCodegen = false;
    
    int optLevel = 0;
};

}