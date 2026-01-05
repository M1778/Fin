#pragma once
#include <string>
#include <vector>
#include <unordered_map>

namespace fin {

struct MacroDef {
    std::vector<std::string> params;
    std::string body;
    bool isFunctionLike = false;
};

class Preprocessor {
public:
    // Takes raw source, returns processed source (with #cdefs resolved)
    std::string process(const std::string& source);

private:
    std::unordered_map<std::string, MacroDef> defines;
    std::vector<bool> ifStack; // For #c_ifdef nesting

    std::string processLine(const std::string& line);
    std::string expandMacros(std::string line);
    
    // Helpers
    bool shouldProcess(); // Checks if we are inside a valid #c_ifdef block
    std::string trim(const std::string& str);
};

}
