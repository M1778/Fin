from ..parser.packages.errors import Colors
from ..ast2.nodes import *

class TypeChecker:
    def __init__(self, diagnostic_engine):
        self.diag = diagnostic_engine
        self.symbol_table = {} # Simple scope for types: name -> type_str
        self.current_return_type = None

    def check(self, node):
        self.visit(node)
        # If fatal errors occurred, the diagnostic engine handles the exit

    def visit(self, node):
        if hasattr(node, "lineno"):
            self.current_node = node # Track for error reporting
            
        method = f'visit_{type(node).__name__}'
        visitor = getattr(self, method, self.generic_visit)
        return visitor(node)

    def generic_visit(self, node):
        if hasattr(node, '__dict__'):
            for _, value in node.__dict__.items():
                if isinstance(value, list):
                    for item in value:
                        if isinstance(item, Node): self.visit(item)
                elif isinstance(value, Node):
                    self.visit(value)

    def error(self, msg, hint=None):
        # Uses YOUR existing engine
        self.diag.add_error(self.current_node, msg, hint, fatal=False)

    # --- RULES ---

    def visit_VariableDeclaration(self, node):
        # 1. Check shadowing/redefinition
        if node.identifier in self.symbol_table:
            self.error(f"Variable '{node.identifier}' is already defined.", 
                       hint="Shadowing is not allowed in the same block.")
        
        # 2. Resolve types
        declared_type = node.type
        inferred_type = self.visit(node.value) if node.value else None

        if declared_type == "auto":
            if not inferred_type:
                self.error(f"Cannot infer type for '{node.identifier}'.", hint="Auto variables must be initialized.")
                declared_type = "unknown"
            else:
                declared_type = inferred_type

        # 3. Type Mismatch
        if inferred_type and declared_type != inferred_type:
            # Allow int->float coercion if your language supports it
            if not (declared_type == "float" and inferred_type == "int"):
                self.error(
                    f"Type mismatch: expected '{declared_type}', got '{inferred_type}'",
                    hint=f"Variable '{node.identifier}' was declared as {declared_type}."
                )

        self.symbol_table[node.identifier] = declared_type

    def visit_AdditiveOperator(self, node):
        left = self.visit(node.left)
        right = self.visit(node.right)

        if left != right:
            self.error(f"Invalid operation between '{left}' and '{right}'", 
                       hint=f"Operator '{node.operator}' requires operands of the same type.")
            return "unknown"
        
        return left

    def visit_Literal(self, node):
        if isinstance(node.value, int): return "int"
        if isinstance(node.value, float): return "float"
        if isinstance(node.value, str): return "string"
        return "unknown"

    def visit_str(self, node):
        # Identifier lookup
        return self.symbol_table.get(node, "unknown")