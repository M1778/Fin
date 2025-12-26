from src.ast2.nodes import *

# ==============================================================================
#                                 FUNCTIONS
# ==============================================================================

def p_function_declaration(p):
    """function_declaration : FUN IDENTIFIER generic_param_list_decl_opt LPAREN params RPAREN return_type LBRACE statements RBRACE
    | STATIC FUN IDENTIFIER generic_param_list_decl_opt LPAREN params RPAREN return_type LBRACE statements RBRACE"""
    is_static_decl = p.slice[1].type == "STATIC"

    name_idx = 2
    generic_params_idx = 3
    params_lparen_idx = 4

    if is_static_decl:
        name_idx = 3
        generic_params_idx = 4
        params_lparen_idx = 5

    func_name = p[name_idx]
    type_params_list = p[generic_params_idx]

    actual_params = p[params_lparen_idx + 1]
    return_type = p[params_lparen_idx + 3]
    body = p[params_lparen_idx + 5]

    body_stmts = p[params_lparen_idx + 5]

    p[0] = FunctionDeclaration(
        name=func_name,
        params=actual_params,
        return_type=return_type,
        body=body_stmts,
        is_static=is_static_decl,
        type_parameters=type_params_list,
    )


def p_params(p):
    """params : param_list
    | empty"""
    p[0] = p[1] if p[1] is not None else []


def p_param_list_single(p):
    "param_list : param"
    p[0] = [p[1]]


def p_param_list_multiple(p):
    "param_list : param_list COMMA param"
    p[0] = p[1] + [p[3]]


def p_param(p):
    "param : IDENTIFIER COLON LT type GT"
    p[0] = Parameter(identifier=p[1], var_type=p[4])


def p_return_type(p):
    """return_type : LT type GT
    | NORET"""
    if len(p) == 4:
        p[0] = p[2]
    else:
        p[0] = p[1]
