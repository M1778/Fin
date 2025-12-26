import os
import platform
from ctypes.util import find_library
from ..lexer import lexer
from ..parser import parser
from ..preprocessor.macros import preprocess_macros
def parse_code(code, filename="<stdin>"):
    # Set the filename on the lexer so DiagnosticEngine can read it
    lexer.filename = filename 
    
    code = preprocess_macros(code)
    lexer.input(code)
    ast = parser.parse(code)
    return ast

def parse_file(path):
    with open(path, "r") as f:
        code = f.read()
    # Pass the path to parse_code
    return parse_code(code, filename=path)
def resolve_c_library(name):
    """
    Map a bare import like "stdio" to the right runtime libc name for this platform.
    """
    if os.path.isabs(name) or name.endswith((".so", ".dll", ".dylib")):
        return name

    plat = platform.system().lower()
    if name.lower() in ("c", "stdio"):
        if plat == "windows":
            return find_library("msvcrt") or "msvcrt.dll"
        elif plat == "darwin":
            return find_library("c") or "libc.dylib"
        else:
            return find_library("c") or "libc.so.6"

    return find_library(name) or name

def run_experimental_mode(compiler):
    text = ""
    save = ""
    while True:
        inp = input("Finterpreter>")
        if inp == "!reset":
            text = ""
            continue
        elif inp == "!run":
            print("Compiling...\n```\n",text,"\n```\n\n")
            compiler.compile(parse_code(text, "<stdin>"))
            compiler.runwithjit("main")
        elif '\\' in inp:
            save += inp.rstrip('\\') + '\n'
        else:
            if save:
                text+=save
                save = ""
            else:
                text+=inp
        print(text)