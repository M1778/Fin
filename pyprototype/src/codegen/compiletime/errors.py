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

class Colors:
    RESET = "\033[0m"
    BOLD = "\033[1m"
    RED = "\033[38;5;196m"
    BLUE = "\033[38;5;39m"
    CYAN = "\033[38;5;51m"
    GRAY = "\033[38;5;240m"

class CompileError(Exception):
    """Custom exception to stop compilation gracefully."""
    pass

class ErrorHandler:
    def __init__(self, source_code: str, filename: str):
        self.source_code = source_code
        self.lines = source_code.splitlines()
        self.filename = filename
        self.had_error = False

    def error(self, node, message, hint=None):
        """
        Reports a compile-time error pointing to the AST node.
        """
        self.had_error = True
        
        lineno = getattr(node, 'lineno', 0)
        col = getattr(node, 'col_offset', 0)
        
        print(f"\n{Colors.RED}{Colors.BOLD}error:{Colors.RESET} {message}")
        
        if lineno > 0 and lineno <= len(self.lines):
            line_content = self.lines[lineno - 1]
            
            print(f"{Colors.BLUE}   -->{Colors.RESET} {self.filename}:{lineno}:{col}")
            
            line_str = str(lineno)
            padding = " " * len(line_str)
            
            print(f"{Colors.BLUE} {padding} |{Colors.RESET}")
            print(f"{Colors.BLUE} {line_str} |{Colors.RESET} {line_content.replace(chr(9), ' ')}")
            
            # Pointer
            pointer_pad = " " * (col - 1)
            print(f"{Colors.BLUE} {padding} |{Colors.RESET} {pointer_pad}{Colors.RED}{Colors.BOLD}^ here{Colors.RESET}")
        else:
            print(f"{Colors.BLUE}   -->{Colors.RESET} {self.filename}:[Unknown Location]")

        if hint:
            print(f"{Colors.CYAN}   = help:{Colors.RESET} {hint}")

        raise CompileError(message)