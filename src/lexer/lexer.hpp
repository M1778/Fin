#pragma once
#include "parser.hpp" 

struct yy_buffer_state;
typedef yy_buffer_state* YY_BUFFER_STATE;

// Flex functions (Global)
fin::parser::symbol_type yylex();
YY_BUFFER_STATE yy_scan_string(const char *str);
void yy_delete_buffer(YY_BUFFER_STATE buffer);

// Helper functions (Namespaced)
namespace fin {
    void reset_lexer_location();
}