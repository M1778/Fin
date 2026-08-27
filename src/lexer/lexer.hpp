#pragma once
#include "parser.hpp" 

struct yy_buffer_state;
typedef yy_buffer_state* YY_BUFFER_STATE;

// Flex functions (Global)
fin::parser::symbol_type yylex();
YY_BUFFER_STATE yy_scan_string(const char *str);
// Byte-counted, so source holding an embedded NUL is scanned whole. `yy_scan_string`
// is `yy_scan_bytes(s, strlen(s))`, which ends the input at the first NUL.
YY_BUFFER_STATE yy_scan_bytes(const char *bytes, int len);
void yy_delete_buffer(YY_BUFFER_STATE buffer);

// Helper functions (Namespaced)
namespace fin {
    void reset_lexer_location();
}