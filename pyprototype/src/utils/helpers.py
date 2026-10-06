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