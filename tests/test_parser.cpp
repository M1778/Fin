#include <gtest/gtest.h>
#include <fstream>
#include <sstream>
#include <string>
#include "parser.hpp"
#include "lexer/lexer.hpp"
#include "diagnostics/DiagnosticEngine.hpp"

// Helper to read file content
std::string readFile(const std::string& path) {
    std::ifstream t(path);
    if (!t.is_open()) return "";
    std::stringstream buffer;
    buffer << t.rdbuf();
    return buffer.str();
}

class ParserTest : public ::testing::Test {
protected:
    // Helper to parse a string
    bool parseString(const std::string& code) {
        // 1. Create Diagnostic Engine
        // We pass the code so it can print context on error
        fin::DiagnosticEngine diag(code, "<test-input>");
        
        // 2. Setup Lexer
        YY_BUFFER_STATE buffer = yy_scan_string(code.c_str());
        
        // 3. Run Parser with Engine
        fin::parser parser(diag);
        int res = parser.parse();
        
        // 4. Cleanup
        yy_delete_buffer(buffer);
        
        // If parsing failed, DiagnosticEngine has already printed the error to stdout.
        // GTest captures stdout, so you will see it in the logs if the test fails.
        return res == 0;
    }
};

// --- Unit Tests ---

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

// --- File Integration Tests ---

TEST_F(ParserTest, File_Basic) {
    std::string code = readFile("samples/basic.fin");
    if(code.empty()) SUCCEED();
    else EXPECT_TRUE(parseString(code));
}

TEST_F(ParserTest, File_Structs) {
    std::string code = readFile("samples/structs.fin");
    if(code.empty()) SUCCEED();
    else EXPECT_TRUE(parseString(code));
}

TEST_F(ParserTest, File_Interfaces) {
    std::string code = readFile("samples/interfaces.fin");
    if(code.empty()) SUCCEED();
    else EXPECT_TRUE(parseString(code));
}

TEST_F(ParserTest, File_GenericsInterfaces) {
    std::string code = readFile("samples/generics_interfaces.fin");
    if(code.empty()) SUCCEED();
    else EXPECT_TRUE(parseString(code));
}