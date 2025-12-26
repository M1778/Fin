# =============================================================================
# Fin Programming Language Compiler
#
# Made with ❤️
#
# This project is genuinely built on love, dedication, and care.
# Fin exists not only as a compiler, but as a labor of passion —
# created for a lover, inspired by curiosity, perseverance, and belief
# in building something meaningful from the ground up.
#
# “What is made with love is never made in vain.”
# “Love is the reason this code exists; logic is how it survives.”
#
# -----------------------------------------------------------------------------
# Author: M1778
# Repository: https://github.com/M1778M/Fin
# Profile: https://github.com/M1778M/
#
# Socials:
#   Telegram: https://t.me/your_username_here
#   Instagram: https://instagram.com/your_username_here
#   X (Twitter): https://x.com/your_username_here
#
# -----------------------------------------------------------------------------
# Copyright (C) 2025 M1778
#
# This file is part of the Fin Programming Language Compiler.
#
# Fin is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Fin is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Fin.  If not, see <https://www.gnu.org/licenses/>.
#
# -----------------------------------------------------------------------------
# “Code fades. Love leaves a signature.”
# =============================================================================
from src.lib import lex

tokens = ('LET', 'BEZ', 'CONST', 'BETON', 'AUTO',
    'FUN', 'NORET', 'RETURN',
    'PUB', 'PRIV',
    'STRUCT', 'ENUM', 'INTERFACE', # Added INTERFACE
    'MACRO', 'STATIC', 'AT', 'NULL',
    'WHILE', 'FOR', 'FOREACH', 'BREAK', 'CONTINUE',
    'IF', 'ELSE', 'ELSEIF', 'IN',
    'TRY', 'CATCH', 'BLAME', 'SUPER', 'SELF_TYPE', # Added Exception/OOP keywords
    
    'LT', 'GT', 'EQUAL', 'SEMICOLON', 'COLON', 'COMMA','DOUBLE_COLON',
    'LPAREN', 'RPAREN', 'LBRACE', 'RBRACE', 'DOT', 'ARROW',
    
    'OPERATOR',
    
    'SPECIAL','AT_RETURN','FN_TYPE','DEFINE',
    'TYPE_INT', 'TYPE_FLOAT', 'TYPE_DOUBLE','TYPE_BOOL', 'TYPE_STRING', 'TYPE_CHAR', 'TYPE_VOID','TYPE_LONG',
    'OR', 'AND', 'NOT',
    
    'PLUS', 'MINUS', 'MULT', 'DIV', 'MOD', 'EQEQ', 'NOTEQ', 'LTEQ', 'GTEQ',
    'INCREMENT', 'DECREMENT',
    'PLUSEQUAL', 'MINUSEQUAL', 'MULTEQUAL', 'DIVEQUAL',
    
    # SUPER SPECIAL TOKEN
    'M1778',
    
    'INTEGER', 'FLOAT', 'STRING_LITERAL', 'CHAR_LITERAL',
    'IDENTIFIER', 'ELLIPSIS',
    
    'STD_CONV', 'NEW', 'DELETE','SIZEOF',
    'AMPERSAND','LBRACKET', 'RBRACKET', 
    'TYPEOF','DOLLAR','HASH',
    'IMPORT', 'AS', 'AS_PTR', 'FROM')

t_ARROW = r'=>'
t_HASH = r'\#'
t_LT = r'<'
t_GT = r'>'
t_EQUAL = r'='
t_SEMICOLON = r';'
t_COLON = r':'
t_DOUBLE_COLON = r'::'
t_COMMA = r','
t_LPAREN = r'\('
t_RPAREN = r'\)'
t_LBRACE = r'\{'
t_RBRACE = r'\}'
t_DOT = r'\.'
t_PLUS = r'\+'
t_MINUS = r'-'
t_MULT = r'\*'
t_DIV = r'/'
t_MOD = r'%'
t_AT = r'@'
t_ELLIPSIS = r'\.\.\.'
t_DOLLAR = r'\$'
t_LBRACKET = r'\['
t_RBRACKET = r'\]'
t_AMPERSAND = r'&'




def t_OR(t):
    r'\|\|'
    return t

def t_AND(t):
    r'&&'
    return t

def t_EQEQ(t):
    r'=='
    return t

def t_NOTEQ(t):
    r'!='
    return t

def t_LTEQ(t):
    r'<='
    return t

def t_GTEQ(t):
    r'>='
    return t

def t_NOT(t):
    r'!'
    return t

def t_INCREMENT(t):
    r'\+\+'
    return t

def t_DECREMENT(t):
    r'--'
    return t

def t_PLUSEQUAL(t):
    r'\+='
    return t

def t_MINUSEQUAL(t):
    r'-='
    return t

def t_MULTEQUAL(t):
    r'\*='
    return t

def t_DIVEQUAL(t):
    r'/='
    return t

    
def t_BLOCK_COMMENT(t):
    r'/\*[\s\S]*?\*/'
    t.lexer.lineno += t.value.count('\n')
    pass # Explicitly ignore


keywords = {
    
    'let': 'LET',
    'bez': 'BEZ',
    'const': 'CONST',
    'beton': 'BETON',
    'auto': 'AUTO',
    
    'fun': 'FUN',
    '<noret>': 'NORET',
    'return': 'RETURN',
    
    'struct': 'STRUCT',
    'enum': 'ENUM',
    
    'macro': 'MACRO',
    'static': 'STATIC',
    'null': 'NULL',
    
    'while': 'WHILE',
    'for': 'FOR',
    'foreach': 'FOREACH',
    'break': 'BREAK',
    'continue': 'CONTINUE',
    'if': 'IF',
    'else': 'ELSE',
    'elseif': 'ELSEIF',
    'in': 'IN',
    'import': 'IMPORT',
    'as' : 'AS',
    'new': 'NEW',
    'delete': 'DELETE',
    'typeof': 'TYPEOF',
    'std_conv' : 'STD_CONV',
    'sizeof':'SIZEOF',
    'as_ptr':'AS_PTR',
    'from': 'FROM',
    'pub': 'PUB',
    'priv': 'PRIV',
    
    'operator': 'OPERATOR',
    
    'try': 'TRY',
    'catch': 'CATCH',
    'blame': 'BLAME',
    'super': 'SUPER',
    'Self': 'SELF_TYPE', # Capital 'Self' is a type keyword
    
    'special': 'SPECIAL',
    '@return': 'AT_RETURN',
    'fn': 'FN_TYPE',
    'define': 'DEFINE',
    
    'm1778': 'M1778',
    
    'interface': 'INTERFACE',
    
    
    'int': 'TYPE_INT',
    'long': 'TYPE_LONG',
    'float': 'TYPE_FLOAT',
    'double': 'TYPE_DOUBLE',
    'bool': 'TYPE_BOOL',
    'string': 'TYPE_STRING',
    'char': 'TYPE_CHAR',
    'void': 'TYPE_VOID',
    'noret': 'TYPE_VOID', # Map noret to void token for simplicity
    
}

def t_IDENTIFIER(t):
    r'[a-zA-Z_][a-zA-Z0-9_]*'
    t.type = keywords.get(t.value, 'IDENTIFIER')  
    return t


def t_FLOAT(t):
    r'\d+\.\d+'
    t.value = float(t.value)
    return t

def t_INTEGER(t):
    r'\d+'
    t.value = int(t.value)
    return t

def t_STRING_LITERAL(t):
    r'\"([^\\\"]|\\.)*\"'
    t.value = t.value[1:-1]  
    return t

def t_CHAR_LITERAL(t):
    r'\'([^\\\']|\\.)\''
    t.value = t.value[1:-1]  
    return t


t_ignore = ' \t\r'
t_ignore_LINECOMMENT = r'//.*' 


def t_newline(t):
    r'\n+'
    t.lexer.lineno += len(t.value)


def t_error(t):
    print(f"Illegal character '{t.value[0]}' at line {t.lineno}")
    t.lexer.skip(1)

def find_column(input_text, token_or_lexpos):
    """
    Calculates the 1-based column number.
    Accepts either a Token object or a raw integer lexpos.
    """
    # 1. Extract integer position
    lexpos = 0
    if isinstance(token_or_lexpos, int):
        lexpos = token_or_lexpos
    elif hasattr(token_or_lexpos, 'lexpos'):
        lexpos = token_or_lexpos.lexpos
    else:
        return 0

    # 2. Calculate column
    last_cr = input_text.rfind('\n', 0, lexpos)
    if last_cr < 0:
        last_cr = -1
        
    return (lexpos - last_cr)

lexer = lex.lex()