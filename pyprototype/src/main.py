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
from src.codegen.fin import FinCompiler
from src.utils.module_loader import ModuleLoader
from src.parser import parser
from src.lexer import lexer
import platform
from pathlib import Path
import sys
import argparse

arg = argparse.ArgumentParser()
arg.add_argument('file')
arg.add_argument('-r','--run', action="store_true")
arg.add_argument('-O', '--optimization-level', action="store_true")
arg.add_argument('-o', '--output', type=str, default="out.exe" if platform.system()=="Windows" else "out")
arg.add_argument('-i', '--ircode', action="store_true")

if __name__ == "__main__":
    lexer.parser_instance = parser
    args = arg.parse_args()
    if not args.file or not Path(args.file).exists():
        sys.exit(0)

    file = args.file

    OPT = 0

    if args.run:
        source =open(args.file,'r').read()
        program = parser.parse(source,lexer=lexer)
        if args.optimization_level:
            OPT = int(args.optimization_level)
        ML = ModuleLoader(args.file)
        compiler = FinCompiler(source,args.file, module_loader=ML, opt=OPT)

        compiler.compile(program)

        compiler.runwithjit()
    
        if args.ircode:
            f=open(f'{str(Path(args.file).stem)}.fin.ir','w')
            f.write(str(compiler.module))
            f.close()
