#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include "parser.hpp"
#include "lexer/lexer.hpp"
#include "diagnostics/DiagnosticEngine.hpp"
#include "preprocessor/Preprocessor.hpp" // <--- ADDED

namespace fs = std::filesystem;

// --- Helper Functions ---

std::string readFile(const std::string& path) {
    std::ifstream t(path);
    if (!t.is_open()) return "";
    std::stringstream buffer;
    buffer << t.rdbuf();
    return buffer.str();
}

// --- Base Test Fixture ---

class ParserTest : public ::testing::Test {
protected:
    // Helper to parse a string and print errors if it fails
    bool parseString(std::string code, const std::string& filename = "<test>") {
        // 0. Reset Lexer State (Crucial for batch testing)
        fin::reset_lexer_location();

        // 1. Run Preprocessor
        // The parser expects clean code (no #cdef), so we must preprocess first.
        fin::Preprocessor pp;
        code = pp.process(code);

        // 2. Setup Diagnostic Engine
        fin::DiagnosticEngine diag(code, filename);
        
        // 3. Setup Lexer
        YY_BUFFER_STATE buffer = yy_scan_string(code.c_str());
        
        // 4. Setup Parser
        fin::parser parser(diag);
        
        // 5. Run
        int res = parser.parse();
        
        // 6. Cleanup
        yy_delete_buffer(buffer);
        
        return res == 0;
    }
};

// --- Existing Unit Tests ---

TEST_F(ParserTest, BasicVariableDecl) {
    const char* code = "fun main() <void> { let x <int> = 10; }";
    EXPECT_TRUE(parseString(code));
}

TEST_F(ParserTest, StructDefinition) {
    const char* code = "struct Point { x <int>, y <int> }";
    EXPECT_TRUE(parseString(code));
}

TEST_F(ParserTest, TurbofishSyntax) {
    const char* code = R"(
        fun main() <noret> {
            let x <int> = my_func::<int>(10);
            let y <auto> = factory::<Point<int>, float>();
        }
    )";
    EXPECT_TRUE(parseString(code));
}

TEST_F(ParserTest, StructMethods) {
    const char* code = R"(
        #[llvm_name="general_point"]
        struct Point<T> {
           x <T>,
           y <T> = 0,
           
           fun print_point(self: <Self>) <noret> { 
              printf("x: %d", cast<int>(self.x)); 
           }
           
           fun set_x<U>(new_x: <U>) <noret> { 
              self.x = cast<T>(new_x); 
           }
        
           pub static fun default_point() <Self> { 
                 return new Self{x: 0}; 
           }
        }
    )";
    EXPECT_TRUE(parseString(code));
}

// --- New: Dynamic File Tests ---

class FileParserTest : public ParserTest, public ::testing::WithParamInterface<std::string> {};

TEST_P(FileParserTest, ParsesSuccessfully) {
    std::string filePath = GetParam();
    std::string code = readFile(filePath);
    
    ASSERT_FALSE(code.empty()) << "Could not read file: " << filePath;
    
    bool success = parseString(code, filePath);
    
    EXPECT_TRUE(success) << "Failed to parse file: " << filePath;
}

std::vector<std::string> GetFinFiles() {
    std::vector<std::string> files;
    std::string path = "samples"; 
    
    if (!fs::exists(path)) {
        path = "tests/samples"; 
    }
    
    if (fs::exists(path)) {
        for (const auto& entry : fs::recursive_directory_iterator(path)) {
            if (entry.path().extension() == ".fin") {
                files.push_back(entry.path().string());
            }
        }
    }
    return files;
}

INSTANTIATE_TEST_SUITE_P(
    AutoDiscovered,
    FileParserTest,
    ::testing::ValuesIn(GetFinFiles()),
    [](const testing::TestParamInfo<std::string>& info) {
        std::string name = fs::path(info.param).stem().string();
        std::replace_if(name.begin(), name.end(), [](char c){ return !isalnum(c); }, '_');
        return name;
    }
);