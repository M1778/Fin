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
#include "preprocessor/Preprocessor.hpp"

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
        
        // 3. Setup Lexer. The catch-all rule reports through whichever engine
        // is installed, so a unit test does not lose an unlexable byte.
        fin::setLexerDiagnostics(&diag);
        YY_BUFFER_STATE buffer = yy_scan_string(code.c_str());
        
        // 4. Setup Parser
        fin::parser parser(diag);
        
        // 5. Run
        int res = parser.parse();
        
        // 6. Cleanup
        yy_delete_buffer(buffer);
        fin::setLexerDiagnostics(nullptr);
        
        return res == 0 && !diag.hasErrors();
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
           
             fun print_point(self: Self) <noret> { 
                printf("x: %d", cast<int>(self.x)); 
             }
             
             fun set_x<U>(new_x: U) <noret> { 
                self.x = cast<T>(new_x); 
             }

        
           pub static fun default_point() <Self> { 
                 return new Self{x: 0}; 
           }
        }
    )";
    EXPECT_TRUE(parseString(code));
}

// --- Preprocessor passthrough -------------------------------------------------
//
// The preprocessor has a fast path: a file with no directives and no line
// continuations is returned unchanged (modulo a trailing newline). These pin
// the behaviour the fast path must preserve byte-for-byte.

TEST(PreprocessorPassthrough, PlainSourceIsUnchanged) {
    fin::Preprocessor pp;
    const std::string src = "fun main() <noret> {}\n";
    EXPECT_EQ(pp.process(src), src);
}

TEST(PreprocessorPassthrough, MissingTrailingNewlineIsAdded) {
    fin::Preprocessor pp;
    EXPECT_EQ(pp.process("fun main() <noret> {}"), "fun main() <noret> {}\n");
}

TEST(PreprocessorPassthrough, CommentsAndStringsAreUntouched) {
    fin::Preprocessor pp;
    const std::string src =
        "// #cdef NOT_A_DIRECTIVE\n"
        "/* #c_ifdef NOPE */\n"
        "fun main() <noret> { let s <string> = \"#cdef\"; }\n";
    EXPECT_EQ(pp.process(src), src);
}

TEST(PreprocessorPassthrough, DirectivesStillExpand) {
    fin::Preprocessor pp;
    const std::string out = pp.process("#cdef FOO 42\nfun main() <noret> { let x <int> = FOO; }\n");
    EXPECT_NE(out.find("42"), std::string::npos) << out;
    EXPECT_EQ(out.find("FOO"), std::string::npos) << out;
}

TEST(PreprocessorPassthrough, DisabledBranchesAreBlanked) {
    fin::Preprocessor pp;
    const std::string out =
        pp.process("#c_ifdef NEVER_DEFINED\nfun hidden() <noret> {}\n#c_endif\nfun main() <noret> {}\n");
    EXPECT_EQ(out.find("hidden"), std::string::npos) << out;
    EXPECT_NE(out.find("main"), std::string::npos) << out;
}

TEST(PreprocessorPassthrough, BackslashContinuationStillJoins) {
    fin::Preprocessor pp;
    const std::string out = pp.process("fun main() <noret> { \\\n let x <int> = 1; }\n");
    EXPECT_EQ(out.find("\\"), std::string::npos) << out;
    EXPECT_NE(out.find("let x"), std::string::npos) << out;
}

// FileParserTest and GetFinFiles() used to live here. Both are deleted:
//
//  * FileParserTest asserted that all fifty samples parse. Authority is
//    per-expectation (ADR 0008), so an aspirational sample failing is the
//    expected outcome and a suite that cannot say so is red for reasons that
//    are not bugs. tests/test_expectations.cpp replaces it.
//  * GetFinFiles() ran during static initialisation and probed "samples" then
//    "tests/samples" relative to the working directory, so a wrong cwd
//    registered zero file tests and the suite reported success.
//    tests/Corpus.cpp::sampleFiles() replaces it: an absolute compile-time
//    path, and zero-sample discovery is a hard failure.
