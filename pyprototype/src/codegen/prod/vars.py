from .essentials import *

# ---------------------------------------------------------------------------
# <Method name=compile_variable_declaration args=[<Compiler>, <VariableDeclaration>]>
# <Description>
# Main entry point for compiling 'let', 'const', 'bez', 'beton'.
# Dispatches to Global or Local compilation logic.
# </Description>
def compile_variable_declaration(compiler: Compiler, ast: VariableDeclaration):
    if compiler.function is None:
        _compile_global_variable(compiler, ast)
    else:
        _compile_local_variable(compiler, ast)

# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# <Method name=_compile_local_variable args=[<Compiler>, <VariableDeclaration>]>
# <Description>
# Compiles a local variable (Stack Allocation).
# Relies on 'create_variable_mut' to handle:
# - Type Conversion
# - Auto-Boxing (Type Erasure)
# - Coercion
# - Scope Registration
# </Description>
def _compile_local_variable(compiler: Compiler, ast: VariableDeclaration):
    name = ast.identifier
    declared_type = ast.type
    init_expr = ast.value

    # 1. Compile Initializer (if exists)
    initial_val_llvm = None
    init_fin_type = None
    
    if init_expr:
        initial_val_llvm = compiler.compile(init_expr)
        # Resolve precise type from AST
        init_fin_type = compiler.get_arg_fin_type(init_expr, initial_val_llvm)

    # 2. Handle 'auto' Type Inference
    # If type is 'auto', we pass the LLVM Type of the initializer to create_variable_mut
    # If type is explicit, we pass the AST Node.
    type_arg = declared_type
    
    if declared_type == "auto":
        if initial_val_llvm is None:
            compiler.errors.error(ast, f"Local 'auto' variable '{name}' requires an initializer.")
            return
        # Pass the inferred LLVM type
        type_arg = initial_val_llvm.type

    # 3. Create Variable
    # This helper (defined in variables.py/helpers.py) handles ALL the heavy lifting
    # including Boxing, Coercion, and Scope.
    compiler.create_variable_mut(name, type_arg, initial_val_llvm, init_fin_type)

# ---------------------------------------------------------------------------
# <Method name=_compile_global_variable args=[<Compiler>, <VariableDeclaration>]>
# <Description>
# Compiles a global variable (Global Data Section).
# Requires compile-time constants.
# </Description>
def _compile_global_variable(compiler: Compiler, ast: VariableDeclaration):
    name = ast.identifier
    declared_type = ast.type
    init_expr = ast.value

    # 1. Resolve Type and Initializer
    llvm_type = None
    init_const = None

    # Case A: 'auto' Global
    if declared_type == "auto":
        if init_expr is None:
            compiler.errors.error(ast, f"Global 'auto' variable '{name}' requires an initializer.")
            return
        
        # Compile expression (Must result in Constant)
        # We temporarily disable builder to ensure no instructions are emitted
        with _const_context(compiler):
            init_const = compiler.compile(init_expr)
            
        if not isinstance(init_const, ir.Constant):
            compiler.errors.error(ast, f"Global 'auto' initializer for '{name}' must be a constant.")
            return
            
        llvm_type = init_const.type

    # Case B: Static Array Global [T, N]
    elif isinstance(declared_type, ArrayTypeNode):
        # Resolve Element Type
        elem_type = compiler.convert_type(declared_type.element_type)
        
        # Resolve Size
        size = 0
        if declared_type.size_expr:
            if isinstance(declared_type.size_expr, Literal) and isinstance(declared_type.size_expr.value, int):
                size = declared_type.size_expr.value
            else:
                compiler.errors.error(ast, f"Global array '{name}' size must be a constant integer.")
                return
        
        # Resolve Initializer
        if init_expr:
            if not isinstance(init_expr, ArrayLiteralNode):
                compiler.errors.error(ast, f"Global array '{name}' initializer must be an array literal.")
                return
            
            # Infer size from init if not explicit
            if size == 0:
                size = len(init_expr.elements)
            elif len(init_expr.elements) != size:
                compiler.errors.error(ast, f"Global array '{name}' size mismatch. Declared {size}, got {len(init_expr.elements)}.")
                return
            
            llvm_type = ir.ArrayType(elem_type, size)
            
            # Compile Array Literal to Constant
            # We use the helper, ensuring it runs in Global Mode (builder=None)
            with _const_context(compiler):
                init_const = compiler.compile_array_literal(init_expr, llvm_type)
        else:
            # No init
            if not ast.is_mutable:
                compiler.errors.error(ast, f"Global constant array '{name}' must be initialized.")
                return
            if size == 0:
                compiler.errors.error(ast, f"Global array '{name}' needs explicit size or initializer.")
                return
            
            llvm_type = ir.ArrayType(elem_type, size)
            init_const = ir.Constant(llvm_type, None) # Zero init

    # Case C: Standard Global
    else:
        llvm_type = compiler.convert_type(declared_type)
        
        if init_expr:
            with _const_context(compiler):
                init_const = compiler.compile(init_expr)
            
            if not isinstance(init_const, ir.Constant):
                compiler.errors.error(ast, f"Global initializer for '{name}' must be a constant.")
                return
        else:
            if not ast.is_mutable:
                compiler.errors.error(ast, f"Global constant '{name}' must be initialized.")
                return
            init_const = ir.Constant(llvm_type, None)

    # 2. Create Global Variable
    # We delegate to the helper which handles registration and type checking
    # Note: We pass the resolved LLVM type and Constant to avoid re-compilation
    compiler.create_variable_immut(name, llvm_type, init_const)

# ---------------------------------------------------------------------------
# <Method name=compile_parameter args=[<Compiler>, <Parameter>]>
# <Description>
# Compiles a parameter definition.
# Allocates stack space and registers the variable in the current scope.
# Note: This is usually called inside function compilation, but if it appears
# standalone, it acts like a mutable variable declaration.
# </Description>
def compile_parameter(compiler: Compiler, ast: Parameter) -> ir.AllocaInstr:
    param_name = ast.identifier
    param_type = ast.var_type
    
    # Use create_variable_mut to handle allocation and scope registration
    return compiler.create_variable_mut(param_name, param_type)

# --- Context Manager for Global Compilation ---
class _const_context:
    """Temporarily disables the builder to ensure we only generate Constants."""
    def __init__(self, compiler):
        self.compiler = compiler
        self.saved_builder = None
    
    def __enter__(self):
        self.saved_builder = self.compiler.builder
        self.compiler.builder = None # Force constant generation mode
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        self.compiler.builder = self.saved_builder