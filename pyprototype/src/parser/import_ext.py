from src.ast2.nodes import *

# ==============================================================================
#                                 IMPORTS & EXTERN
# ==============================================================================

def p_import_statement(p):
    """import_statement : IMPORT STRING_LITERAL SEMICOLON
    | IMPORT STRING_LITERAL AS IDENTIFIER SEMICOLON"""
    if len(p) == 4:
        p[0] = ImportModule(path=p[2])
    else:
        p[0] = ImportModule(path=p[2], alias=p[4])


def p_import_c_statement(p):
    """import_c_statement : IMPORT_C STRING_LITERAL SEMICOLON"""
    p[0] = ImportC(p[2])


def p_extern_statement(p):
    """extern_statement : EXTERN FUN IDENTIFIER LPAREN extern_params_content RPAREN extern_return_type SEMICOLON"""

    param_list, has_va_args = p[5]

    processed_args = param_list
    if has_va_args and param_list and param_list[-1] == "...":
        pass
    elif has_va_args:
        processed_args = (param_list or []) + ["..."]

    p[0] = Extern(func_name=p[3], func_args=processed_args, func_return_type=p[7])


def p_extern_params_content(p):
    """extern_params_content : extern_param_list COMMA ELLIPSIS
    | extern_param_list
    | empty"""
    if len(p) == 4:
        p[0] = (p[1], True)
    elif len(p) == 2:
        if p[1] is None:
            p[0] = ([], False)
        else:
            p[0] = (p[1], False)
    else:
        p[0] = ([], False)


def p_extern_param_list_single(p):
    """extern_param_list : extern_param"""
    p[0] = [p[1]]


def p_extern_param_list_multiple(p):
    """extern_param_list : extern_param_list COMMA extern_param"""
    p[0] = p[1] + [p[3]]


def p_extern_param(p):
    """extern_param : IDENTIFIER COLON LT type GT"""
    p[0] = Parameter(identifier=p[1], var_type=p[4])


def p_extern_return_type(p):
    """extern_return_type : LT type GT
    | NORET"""
    if len(p) == 4:
        p[0] = p[2]
    else:
        p[0] = p[1]

