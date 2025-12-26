from .essentials import *

# <Method name=create_variable_mut args=[<Compiler>, <str>, <Union[Node, ir.Type]>, <Optional[ir.Value]>]>
# <Description>
# Helper to declare and optionally initialize a mutable local variable.
# Handles:
# 1. Type Conversion (AST -> LLVM)
# 2. Type Erasure (FinType registration)
# 3. Auto-Boxing (Concrete -> Generic i8*)
# 4. Type Coercion (Int->Float, Array->Collection, etc.)
# </Description>
def create_variable_mut(
    compiler: Compiler,
    name: str,
    var_type_ast_or_llvm: Union[Node, ir.Type],
    initial_value_llvm: Optional[ir.Value] = None,
    explicit_value_fin_type: FinType = None
) -> ir.AllocaInstr:
    """
    Helper to declare and optionally initialize a mutable local variable.
    Handles:
    1. Type Conversion (AST -> LLVM)
    2. Type Erasure (FinType registration)
    3. Auto-Boxing (Concrete -> Generic i8*)
    4. Type Coercion (Int->Float, Array->Collection, etc.)
    """
    if compiler.function is None or compiler.builder is None:
        raise CompilerException("create_variable_mut can only be called within a function compilation context.")

    # 1. Determine LLVM Type and FinType
    llvm_type = None
    fin_type = None
    ast_node = None # Used for error reporting

    if isinstance(var_type_ast_or_llvm, ir.Type):
        llvm_type = var_type_ast_or_llvm
        fin_type = compiler._infer_fin_type_from_llvm(llvm_type)
    else:
        ast_node = var_type_ast_or_llvm
        
        # Handle Array Size Inference: let x <[int]> = [1, 2]
        if isinstance(ast_node, ArrayTypeNode) and ast_node.size_expr is None:
            if initial_value_llvm and isinstance(initial_value_llvm.type, ir.ArrayType):
                # Infer size from initializer
                inferred_size = initial_value_llvm.type.count
                elem_type = compiler.convert_type(ast_node.element_type)
                llvm_type = ir.ArrayType(elem_type, inferred_size)
                fin_type = compiler.ast_to_fin_type(ast_node) # Note: FinType might need update to store size
            else:
                compiler.errors.error(ast_node, f"Cannot infer size for array '{name}'. Initializer missing or not an array.")
                return None
        else:
            llvm_type = compiler.convert_type(var_type_ast_or_llvm)
            fin_type = compiler.ast_to_fin_type(var_type_ast_or_llvm)
    
    if llvm_type is None:
        raise compiler.errors.error(ast_node, f"Could not determine type for '{name}'")

    # 2. Allocate Stack Memory
    var_ptr = compiler.builder.alloca(llvm_type, name=f"{name}_ptr")

    # 3. Define in Scope (With High-Level Type Info)
    try:
        compiler.current_scope.define(name, var_ptr, fin_type)
    except Exception as e:
        # Use the error handler if we have a node location
        if ast_node:
            compiler.errors.error(ast_node, f"Variable '{name}' definition failed.", hint=str(e))
        raise CompilerException(e.args[0])

    # 4. Initialize with Coercion
    if initial_value_llvm is not None:
        val_to_store = initial_value_llvm

        # --- COERCION LOGIC ---
        if val_to_store.type != llvm_type:
            
            if compiler.is_any_type(llvm_type):
                rhs_fin_type = explicit_value_fin_type if explicit_value_fin_type else compiler.infer_fin_type_from_llvm(val_to_store.type)
                val_to_store = compiler.pack_any(val_to_store, rhs_fin_type)
            
            # Auto-Boxing for Type Erasure
            # If variable is Generic (i8*) but value is Concrete (e.g. i32)
            elif llvm_type == ir.IntType(8).as_pointer() and val_to_store.type != llvm_type:
                val_fin_type = explicit_value_fin_type if explicit_value_fin_type else compiler.infer_fin_type_from_llvm(val_to_store.type)
                val_to_store = compiler.box_value(val_to_store, val_fin_type)
            # A. Int -> Float
            elif isinstance(llvm_type, ir.FloatType) and isinstance(val_to_store.type, ir.IntType):
                val_to_store = compiler.builder.sitofp(
                    val_to_store, llvm_type, name=f"{name}_init_sitofp"
                )
            
            # B. Float -> Int
            elif isinstance(llvm_type, ir.IntType) and isinstance(val_to_store.type, ir.FloatType):
                val_to_store = compiler.builder.fptosi(
                    val_to_store, llvm_type, name=f"{name}_init_fptosi"
                )

            # C. Int Width Mismatch (Truncate or Extend)
            elif isinstance(llvm_type, ir.IntType) and isinstance(val_to_store.type, ir.IntType):
                target_w = llvm_type.width
                source_w = val_to_store.type.width
                if target_w < source_w:
                    val_to_store = compiler.builder.trunc(val_to_store, llvm_type, name=f"{name}_init_trunc")
                elif target_w > source_w:
                    val_to_store = compiler.builder.sext(val_to_store, llvm_type, name=f"{name}_init_sext")
            
            # D. Pointer Bitcast (e.g. void* -> int* or Child* -> Parent*)
            elif isinstance(llvm_type, ir.PointerType) and isinstance(val_to_store.type, ir.PointerType):
                if llvm_type != val_to_store.type:
                    val_to_store = compiler.builder.bitcast(val_to_store, llvm_type, name=f"{name}_init_bitcast")

            # E. String Literal (i8*) -> Char (i8)
            elif (isinstance(llvm_type, ir.IntType) and llvm_type.width == 8 and 
                    isinstance(val_to_store.type, ir.PointerType) and 
                    isinstance(val_to_store.type.pointee, ir.IntType) and 
                    val_to_store.type.pointee.width == 8):
                val_to_store = compiler.builder.load(val_to_store, name=f"{name}_init_char_load")

            # F. Struct Pointer -> Struct Value (Dereference)
            elif isinstance(llvm_type, ir.IdentifiedStructType) and \
                    isinstance(val_to_store.type, ir.PointerType) and \
                    val_to_store.type.pointee == llvm_type:
                val_to_store = compiler.builder.load(val_to_store, name=f"{name}_init_struct_load")

            # G. Array Literal ([N x T]) -> Collection ({T*, len})
            elif isinstance(llvm_type, ir.LiteralStructType) and \
                    len(llvm_type.elements) == 2 and \
                    isinstance(val_to_store.type, ir.ArrayType):
                    
                    target_elem_type = llvm_type.elements[0].pointee
                    val_to_store = compiler.create_collection_from_array_literal(val_to_store, target_elem_type)

            # Final Check
            if val_to_store.type != llvm_type:
                msg = f"Type mismatch for variable '{name}'. Expected {llvm_type}, got {val_to_store.type}."
                if ast_node: compiler.errors.error(ast_node, msg)
                else: raise CompilerException(msg)

        compiler.builder.store(val_to_store, var_ptr)

    return var_ptr

# ---------------------------------------------------------------------------
# <Method name=create_variable_immut args=[<Compiler>, <str>, <Union[Node, ir.Type]>, <Union[Node, ir.Constant]>]>
# <Description>
# Declare an immutable (constant) global variable in the module.
# The initial_value_ast_or_llvm_const MUST evaluate to a compile-time constant.
# </Description>
def create_variable_immut(
    compiler: Compiler,
    name: str,
    var_type_ast_or_llvm: Union[Node, ir.Type],
    initial_value_ast_or_llvm_const: Union[Node, ir.Constant],
) -> ir.GlobalVariable:
    """
    Declare an immutable (constant) global variable in the module.
    The initial_value_ast_or_llvm_const MUST evaluate to a compile-time constant.
    """
    
    # 1. Determine LLVM Type and FinType
    llvm_type = None
    fin_type = None
    ast_node = None # Used for error reporting

    if isinstance(var_type_ast_or_llvm, ir.Type):
        # Case A: Raw LLVM Type passed
        llvm_type = var_type_ast_or_llvm
        fin_type = compiler.infer_fin_type_from_llvm(llvm_type)
    else:
        # Case B: AST Node passed
        ast_node = var_type_ast_or_llvm
        llvm_type = compiler.convert_type(var_type_ast_or_llvm)
        fin_type = compiler.ast_to_fin_type(var_type_ast_or_llvm)

    if llvm_type is None:
        msg = f"Could not determine LLVM type for global constant '{name}'."
        if ast_node: compiler.errors.error(ast_node, msg)
        else: raise CompilerException(msg)

    # 2. Determine Initializer
    llvm_initializer_const = None
    if initial_value_ast_or_llvm_const is None:
        msg = f"Global immutable variable '{name}' must be initialized."
        if ast_node: compiler.errors.error(ast_node, msg)
        else: raise CompilerException(msg)

    if isinstance(initial_value_ast_or_llvm_const, ir.Constant):
        llvm_initializer_const = initial_value_ast_or_llvm_const
    else:
        # Compile the AST node to get the constant
        # We temporarily disable the builder to ensure no instructions are emitted
        # (Constants shouldn't emit instructions anyway)
        current_builder = compiler.builder
        compiler.builder = None

        try:
            compiled_init_val = compiler.compile(initial_value_ast_or_llvm_const)
        except Exception as e:
            compiler.builder = current_builder
            raise e

        compiler.builder = current_builder

        if not isinstance(compiled_init_val, ir.Constant):
            msg = f"Initializer for global constant '{name}' must be a compile-time constant expression. Got {type(compiled_init_val)}."
            if isinstance(initial_value_ast_or_llvm_const, Node):
                compiler.errors.error(initial_value_ast_or_llvm_const, msg)
            else:
                raise CompilerException(msg)
        
        llvm_initializer_const = compiled_init_val

    # 3. Type Checking
    # Note: We cannot emit instructions (sext/trunc/bitcast) for globals easily.
    # The types must match or be compatible constants.

    if llvm_initializer_const.type != llvm_type:
        # Check for Array Literal -> Array Type mismatch (e.g. size inference)
        if isinstance(llvm_type, ir.ArrayType) and isinstance(llvm_initializer_const.type, ir.ArrayType):
            if llvm_type.element == llvm_initializer_const.type.element:
                if llvm_type.count != llvm_initializer_const.type.count:
                     msg = f"Global array '{name}' size mismatch. Declared {llvm_type.count}, got {llvm_initializer_const.type.count}."
                     if ast_node: compiler.errors.error(ast_node, msg)
                     else: raise CompilerException(msg)
        
        # Strict check for others
        elif llvm_initializer_const.type != llvm_type:
             msg = f"Type mismatch for global constant '{name}'. Expected {llvm_type}, got {llvm_initializer_const.type}."
             if ast_node: compiler.errors.error(ast_node, msg)
             else: raise CompilerException(msg)

    # 4. Create or Get Global Variable
    try:
        gvar = compiler.module.get_global(name)
        # Check for redefinition with different type
        if gvar.type.pointee != llvm_type:
            msg = f"Global variable '{name}' already exists with a different type."
            if ast_node: compiler.errors.error(ast_node, msg)
            else: raise CompilerException(msg)
        if not gvar.global_constant:
            msg = f"Global variable '{name}' already exists but is not constant."
            if ast_node: compiler.errors.error(ast_node, msg)
            else: raise CompilerException(msg)

    except KeyError:
        gvar = ir.GlobalVariable(compiler.module, llvm_type, name=name)

    gvar.linkage = "internal"
    gvar.global_constant = True
    gvar.initializer = llvm_initializer_const

    # 5. Register in Global Scope
    existing_in_scope = compiler.global_scope.resolve(name)
    if existing_in_scope is None:
        # [FIX] Pass fin_type to scope so it can be resolved correctly later
        compiler.global_scope.define(name, gvar, fin_type)
    elif existing_in_scope is not gvar:
        msg = f"Symbol '{name}' already defined in global scope with a different value."
        if ast_node: compiler.errors.error(ast_node, msg)
        else: raise CompilerException(msg)

    return gvar

# ---------------------------------------------------------------------------
# <Method name=guess_type args=[<Compiler>, <Any>]>
# <Description>
# Infers the LLVM Type corresponding to a Python literal value.
# Used primarily when compiling Literal nodes to determine the constant type.
# </Description>
def guess_type(self, value):
    if isinstance(value, int):

        if -(2**31) <= value < 2**31:
            return ir.IntType(32)
        elif -(2**63) <= value < 2**63:
            return ir.IntType(64)

        elif -(2**127) <= value < 2**127:
            return ir.IntType(128)
        else:

            raise Exception(
                f"Integer literal {value} is too large for supported integer types (e.g., i128)."
            )

    elif isinstance(value, float):
        return ir.FloatType()

    elif isinstance(value, str):

        return ir.IntType(8).as_pointer()

    elif isinstance(value, bool):
        return ir.IntType(1)

    else:
        raise Exception(
            f"Cannot guess LLVM type for Python value '{value}' of type {type(value)}."
        )

# ---------------------------------------------------------------------------
# <Method name=create_global_string args=[<Compiler>, <str>]>
# <Description>
# Creates a global string constant in the LLVM module.
# Caches strings to avoid duplicates.
# Returns a pointer to the first character of the string.
# </Description>
def create_global_string(self, val: str) -> ir.Value:
    if val in self.global_strings:
        return self.global_strings[val]

    bytes_ = bytearray(val.encode("utf8")) + b"\00"
    str_ty = ir.ArrayType(ir.IntType(8), len(bytes_))

    uniq = uuid.uuid4().hex[:8]
    name = f".str_{uniq}"

    gvar = ir.GlobalVariable(self.module, str_ty, name=name)
    gvar.linkage = "internal"
    gvar.global_constant = True
    gvar.initializer = ir.Constant(str_ty, bytes_)

    zero = ir.Constant(ir.IntType(32), 0)
    
    # --- FIX START ---
    # Use Constant Expression GEP. This works everywhere (even in __init__).
    # It does not require self.builder.
    str_ptr = gvar.gep([zero, zero]) 
    # --- FIX END ---

    self.global_strings[val] = str_ptr
    return str_ptr
# ---------------------------------------------------------------------------
# <Method name=set_variable args=[<Compiler>, <str>, <ir.Value>, <Node>]>
# <Description>
# Stores a value into an existing variable.
# </Description>
def set_variable(compiler: Compiler, name: str, value_llvm: ir.Value, node: Node = None) -> ir.Value:
    if compiler.builder is None:
        raise Exception("set_variable called outside of a function context.")

    # 1. Resolve Variable
    var_ptr = compiler.current_scope.resolve(name)

    if var_ptr is None:
        msg = f"Variable '{name}' not declared before assignment."
        if node: compiler.errors.error(node, msg)
        else: raise Exception(msg)

    if not isinstance(var_ptr, (ir.AllocaInstr, ir.GlobalVariable)):
        msg = f"Cannot assign to '{name}'. It is not a mutable variable."
        if node: compiler.errors.error(node, msg)
        else: raise Exception(msg)

    if isinstance(var_ptr, ir.GlobalVariable) and var_ptr.global_constant:
        msg = f"Cannot assign to global constant '{name}'."
        if node: compiler.errors.error(node, msg)
        else: raise Exception(msg)

    # 2. Determine Types
    expected_type = var_ptr.type.pointee
    val_to_store = value_llvm

    # 3. Coercion & Boxing Logic
    if val_to_store.type != expected_type:
        is_any_target = (isinstance(expected_type, ir.LiteralStructType) and 
                         len(expected_type.elements) == 2 and 
                         expected_type.elements[1] == ir.IntType(64))
        
        
        if is_any_target:
            # Special Case: Array Literal -> Collection
            if isinstance(val_to_store.type, ir.ArrayType):
                elem_type = val_to_store.type.element
                val_to_store = compiler.create_collection_from_array_literal(val_to_store, elem_type)
            
            # Infer type and pack
            fin_type = compiler.infer_fin_type_from_llvm(val_to_store.type)
            val_to_store = compiler.pack_any(val_to_store, fin_type)
        # Auto-Boxing for Type Erasure
        elif expected_type == ir.IntType(8).as_pointer() and val_to_store.type != expected_type:
            fin_type = compiler.infer_fin_type_from_llvm(val_to_store.type)
            val_to_store = compiler.box_value(val_to_store, fin_type)

        # Int -> Float
        elif isinstance(expected_type, ir.FloatType) and isinstance(val_to_store.type, ir.IntType):
            val_to_store = compiler.builder.sitofp(val_to_store, expected_type, name=f"{name}_set_sitofp")
        
        # Float -> Int
        elif isinstance(expected_type, ir.IntType) and isinstance(val_to_store.type, ir.FloatType):
            val_to_store = compiler.builder.fptosi(val_to_store, expected_type, name=f"{name}_set_fptosi")

        # Int Width
        elif isinstance(expected_type, ir.IntType) and isinstance(val_to_store.type, ir.IntType):
            if val_to_store.type.width < expected_type.width:
                val_to_store = compiler.builder.sext(val_to_store, expected_type, name=f"{name}_set_sext")
            elif val_to_store.type.width > expected_type.width:
                val_to_store = compiler.builder.trunc(val_to_store, expected_type, name=f"{name}_set_trunc")

        # Pointer Bitcast
        elif isinstance(expected_type, ir.PointerType) and isinstance(val_to_store.type, ir.PointerType):
            val_to_store = compiler.builder.bitcast(val_to_store, expected_type, name=f"{name}_set_bitcast")

        # Array Literal -> Collection
        elif isinstance(expected_type, ir.LiteralStructType) and \
             len(expected_type.elements) == 2 and \
             isinstance(val_to_store.type, ir.ArrayType):
             
             target_elem_type = expected_type.elements[0].pointee
             val_to_store = compiler.create_collection_from_array_literal(val_to_store, target_elem_type)

        # Final Check
        if val_to_store.type != expected_type:
            msg = f"Type mismatch in assignment to '{name}'. Expected {expected_type}, got {val_to_store.type}."
            if node: compiler.errors.error(node, msg)
            else: raise Exception(msg)

    # 4. Store
    compiler.builder.store(val_to_store, var_ptr)
    return var_ptr
# ---------------------------------------------------------------------------
# <Method name=get_variable args=[<Compiler>, <Union[str, Node]>]>
# <Description>
# Retrieves the LLVM value of a symbol.
# 1. Checks Scope for Variables/Functions.
# 2. Checks Global Module for Symbols.
# 3. Checks if it's a Type Name (returns Type ID).
# 4. Handles Literals and other Nodes.
# </Description>
def get_variable(compiler: Compiler, name_or_node_ast: Union[str, Node]) -> ir.Value:
    # Case 1: Identifier (String)
    if isinstance(name_or_node_ast, str):
        identifier_name = name_or_node_ast
        
        # 1. Resolve Variable in Scope
        resolved_symbol = compiler.current_scope.resolve(identifier_name)

        # 2. Resolve Global Variable/Function
        if resolved_symbol is None:
            try:
                resolved_symbol = compiler.module.get_global(identifier_name)
            except KeyError:
                pass


        # 4. Handle Resolved Symbol
        if isinstance(resolved_symbol, (ir.AllocaInstr, ir.GlobalVariable)):
            if compiler.builder is None:
                raise Exception("get_variable trying to load from memory outside function context.")
            return compiler.builder.load(resolved_symbol, name=identifier_name + "_val")
        
        elif isinstance(resolved_symbol, (ir.Function, ir.Constant, ir.Argument)):
            return resolved_symbol
        
        elif resolved_symbol is None:
            compiler.errors.error(None, f"Variable, symbol, or type '{identifier_name}' not declared.")
            return ir.Constant(ir.IntType(32), 0)
        
        else:
            raise Exception(f"Resolved symbol '{identifier_name}' is of an unexpected type: {type(resolved_symbol)}")

    # Case 2: Literal Node
    elif isinstance(name_or_node_ast, Literal):
        py_value = name_or_node_ast.value
        if py_value is None:
            return ir.Constant(ir.IntType(8).as_pointer(), None)
        elif isinstance(py_value, str):
            return compiler.create_global_string(py_value)
        else:
            llvm_type = compiler.guess_type(py_value)
            return ir.Constant(llvm_type, py_value)

    # Case 3: Complex AST Node
    else:
        return compiler.compile(name_or_node_ast) 
    
