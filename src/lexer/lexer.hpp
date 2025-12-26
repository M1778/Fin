#pragma once
#include "tokens.hpp"


// Forward declare the Flex buffer struct
struct yy_buffer_state;
typedef yy_buffer_state* YY_BUFFER_STATE;


// C++ Function Declarations (No extern "C")
int yylex(fin::Token* yylval);
YY_BUFFER_STATE yy_scan_string(const char *str);
void yy_delete_buffer(YY_BUFFER_STATE buffer);
