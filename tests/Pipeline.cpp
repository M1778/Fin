#include "Pipeline.hpp"

#include "lexer/lexer.hpp"
#include "parser.hpp"
#include "preprocessor/Preprocessor.hpp"

namespace fin {
extern std::unique_ptr<Program> root;
}

namespace fin::testing {

ParseResult parseSource(const std::string& code, fin::DiagnosticEngine& diag) {
    ParseResult r;

    fin::Preprocessor pp;
    std::string processed = pp.process(code);
    diag.setSource(processed, "<test>");

    fin::setLexerDiagnostics(&diag);
    fin::reset_lexer_location();
    fin::root = nullptr;

    YY_BUFFER_STATE buffer = yy_scan_string(processed.c_str());
    fin::parser parser(diag);
    int res = parser.parse();
    yy_delete_buffer(buffer);

    fin::setLexerDiagnostics(nullptr);

    if (res == 0 && fin::root) {
        r.ast = std::move(fin::root);
        r.parsed = true;
    }
    fin::root = nullptr;
    return r;
}

}
