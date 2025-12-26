import sys
import difflib

class Colors:
    RESET = "\033[0m"
    BOLD = "\033[1m"
    RED = "\033[38;5;196m"
    ORANGE = "\033[38;5;208m"
    YELLOW = "\033[38;5;226m"
    BLUE = "\033[38;5;39m"
    GRAY = "\033[38;5;240m"
    CYAN = "\033[38;5;51m"
    GREEN = "\033[38;5;46m"

class DiagnosticEngine:
    def __init__(self, parser, lexer):
        self.parser = parser
        self.lexer = lexer
        self.errors = []
        self.recovering = False 
        self.TYPO_CUTOFF = 0.8

        # Tokens that reset the panic mode
        self.SAFE_TOKENS = ['LET', 'CONST', 'FUN', 'STRUCT', 'ENUM', 'IF', 'WHILE', 'FOR', 'RETURN', 'RBRACE', 'SEMICOLON', 'INTERFACE']

        self.TOKEN_MAP = {
            'LPAREN': "'('", 'RPAREN': "')'",
            'LBRACE': "'{'", 'RBRACE': "'}'",
            'LBRACKET': "'['", 'RBRACKET': "']'",
            'SEMICOLON': "';'", 'COLON': "':'", 'COMMA': "','",
            'DOT': "'.'", 
            'PLUS': "'+'", 'MINUS': "'-'", 'MULT': "'*'", 'DIV': "'/'",
            'EQUAL': "'='", 'EQEQ': "'=='", 'NOTEQ': "'!='",
            'LT': "'<'", 'GT': "'>'", 'LTEQ': "'<='", 'GTEQ': "'>='",
            'AMPERSAND': "'&'", 'DOLLAR': "'$'", 'AT': "'@'",
            'IDENTIFIER': "identifier", 'STRING_LITERAL': "string",
            'INTEGER': "integer", 'FLOAT': "float",
            'INCREMENT': "'++'", 'DECREMENT': "'--'",
            'EOF': "end of file"
        }

        self.KEYWORDS = [
            "fun", "struct", "enum", "let", "const", "bez", "beton", 
            "if", "else", "elseif", "while", "for", "foreach", "return", "break", 
            "continue", "import", "import_c", "extern", "sizeof", "typeof", "new", "delete",
            "noret", "auto", "int", "float", "char", "void", "printf"
        ]

    def get_friendly_name(self, token_type):
        return self.TOKEN_MAP.get(token_type, token_type)

    def is_literal(self, token):
        """Returns True if the token is a raw value (number, string)"""
        if not token: return False
        return token.type in ['INTEGER', 'FLOAT', 'STRING_LITERAL', 'CHAR_LITERAL', 'INT', 'FLOAT'] and not isinstance(token.value, str)

    def add_error(self, token, message, hint=None, fatal=False):
        # Smart Recovery: If we hit a safe token, we stop suppressing errors
        if self.recovering and not fatal:
            if token and token.type in self.SAFE_TOKENS:
                self.recovering = False
            else:
                return

        err = { "token": token, "msg": message, "hint": hint, "fatal": fatal }
        self.errors.append(err)
        self.render_error(err)
        
        if fatal:
            self.print_summary()
            sys.exit(1)

    def find_column(self, token):
        if token is None: return 0
        input_data = self.lexer.lexdata
        line_start = input_data.rfind('\n', 0, token.lexpos) + 1
        return (token.lexpos - line_start) + 1

    def get_line_content(self, lineno):
        input_data = self.lexer.lexdata
        lines = input_data.splitlines()
        if 0 <= lineno - 1 < len(lines):
            return lines[lineno - 1]
        return ""

    def check_typo(self, value):
        if value in self.KEYWORDS: return None
        matches = difflib.get_close_matches(value, self.KEYWORDS, n=1, cutoff=self.TYPO_CUTOFF)
        return matches[0] if matches else None

    def analyze_context(self):
        stack = self.parser.symstack
        for item in reversed(stack):
            if hasattr(item, 'type'):
                if item.type == 'STRUCT': return "struct definition"
                if item.type == 'ENUM': return "enum definition"
                if item.type == 'FUN': return "function definition"
                if item.type == 'IF': return "if statement"
                if item.type == 'ELSE': return "else block"
                if item.type == 'WHILE': return "while loop"
                if item.type == 'FOR': return "for loop"
            if isinstance(item, dict) and "members" in item: return "struct body"
        return "global scope"

    def analyze_unclosed_delimiters(self):
        stack = self.parser.symstack
        for sym in reversed(stack):
            if hasattr(sym, 'type'):
                if sym.type == 'LPAREN': return "Unclosed parenthesis '('"
                if sym.type == 'LBRACE': return "Unclosed brace '{'"
                if sym.type == 'LBRACKET': return "Unclosed bracket '['"
        return "Unexpected end of file"

    def render_error(self, err):
        token = err['token']
        msg = err['msg']
        hint = err['hint']
        
        if token is None:
            print(f"\n{Colors.RED}{Colors.BOLD}error:{Colors.RESET} {msg}")
            print(f"{Colors.BLUE}   -->{Colors.RESET} {self.lexer.filename if hasattr(self.lexer, 'filename') else 'input'}:EOF")
            if hint: print(f"{Colors.CYAN}   = help:{Colors.RESET} {hint}")
            return

        lineno = token.lineno
        col = self.find_column(token)
        line_content = self.get_line_content(lineno)
        
        print(f"\n{Colors.RED}{Colors.BOLD}error:{Colors.RESET} {msg}")
        print(f"{Colors.BLUE}   -->{Colors.RESET} {self.lexer.filename if hasattr(self.lexer, 'filename') else 'input'}:{lineno}:{col}")
        
        line_str = str(lineno)
        padding = " " * len(line_str)
        
        print(f"{Colors.BLUE} {padding} |{Colors.RESET}")
        print(f"{Colors.BLUE} {line_str} |{Colors.RESET} {line_content.replace(chr(9), ' ')}")
        
        pointer_pad = " " * (col - 1)
        pointer_len = len(str(token.value))
        pointer = "^" * max(1, pointer_len)
        print(f"{Colors.BLUE} {padding} |{Colors.RESET} {pointer_pad}{Colors.RED}{Colors.BOLD}{pointer} here{Colors.RESET}")
        
        context = self.analyze_context()
        print(f"{Colors.BLUE} {padding} |{Colors.RESET} {Colors.GRAY}inside {context}{Colors.RESET}")

        if hint:
            print(f"{Colors.CYAN}   = help:{Colors.RESET} {hint}")

    def print_summary(self):
        count = len(self.errors)
        if count > 0:
            print(f"\n{Colors.RED}{Colors.BOLD}Build failed.{Colors.RESET} {count} error(s) found.")

    def recover(self):
        print(f"{Colors.ORANGE}   -> Attempting to recover...{Colors.RESET}")
        self.recovering = True
        
        while True:
            tok = self.parser.token()
            if not tok: break
            
            # Stop at safe tokens to allow parser to restart
            if tok.type in self.SAFE_TOKENS:
                self.recovering = False
                return tok
            
            if tok.type == 'SEMICOLON':
                self.recovering = False
                return tok
            
            if tok.type == 'RBRACE':
                self.recovering = False
                return tok

        self.parser.errok()
        return tok