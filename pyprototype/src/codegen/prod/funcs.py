from .essentials import *

# ---------------------------------------------------------------------------
# <Method name=compile_function_declaration args=[<Compiler>, <FunctionDeclaration>]>
# <Description>
# Compiles a global function declaration.
# Handles:
# 1. Monomorphization (Templates): Saves AST, skips compilation.
# 2. Type Erasure: Registers generic params as i8*.
# 3. Attributes: Applies #[llvm_name], #[linkage], etc.
# 4. Interfaces: Converts Interface parameters to Fat Pointers.
# 5. Argument Binding: Allocates stack space for arguments.
# </Description>
def compile_function_declaration(compiler: Compiler, ast: FunctionDeclaration, prototype_only: bool = False) -> Optional[ir.Function]:
    func_name = ast.name
    
    # 1. Classify Mode
    mode = compiler.classify_mode(ast)
    compiler.modes[func_name] = mode
    
    # --- PATH A: Monomorphization ---
    if mode == 'MONO':
        # Templates are just saved, not compiled.
        # Pass 0 and Pass 1 both do this, which is fine (idempotent).
        compiler.function_templates[func_name] = ast
        return None

    # --- PATH B & C: Standard / Erased ---
    
    # Register Metadata
    compiler.function_registry[func_name] = ast
    compiler.function_origins[func_name] = compiler.current_file_path
    
    # 2. Resolve Name
    default_mangled = compiler.get_mangled_name(func_name)
    llvm_name = compiler.attributes_lib.resolve_llvm_name(ast, default_mangled)
    compiler.function_visibility[llvm_name] = ast.visibility

    # 3. Enter Scope (Generic Scope)
    compiler.enter_scope()
    
    if mode == 'ERASED' and ast.type_parameters:
        for param in ast.type_parameters:
            compiler.current_scope.define_type_parameter(param.name, param.constraint)

    # 4. Prepare Signature
    llvm_ret_type = compiler.convert_type(ast.return_type)
    
    llvm_param_types = []
    for p_ast in ast.params:
        fin_type = compiler.ast_to_fin_type(p_ast.var_type)
        
        if isinstance(fin_type, StructType):
            mangled_type_name = compiler.get_mangled_name(fin_type.name)
            if mangled_type_name in compiler.interfaces:
                llvm_param_types.append(compiler.get_interface_type(fin_type.name))
                continue
        
        llvm_param_types.append(compiler.convert_type(p_ast.var_type))

    # 5. Create or Retrieve LLVM Function
    llvm_func_type = ir.FunctionType(
        llvm_ret_type,
        llvm_param_types,
        var_arg=ast.is_vararg
    )

    try:
        llvm_function = compiler.module.get_global(llvm_name)
    except KeyError:
        llvm_function = ir.Function(compiler.module, llvm_func_type, name=llvm_name)
    print(llvm_function)
    compiler.attributes_lib.apply(llvm_function, ast)
    print(llvm_function)

    # 6. Register Symbol
    # We register in the PARENT scope (Global/Module)
    target_scope = compiler.current_scope.parent if compiler.current_scope.parent else compiler.current_scope
    
    # [FIX] Check if already defined (Pass 1 re-visiting Pass 0)
    if func_name in target_scope.symbols:
        existing_info = target_scope.symbols[func_name]
        # If it's the same LLVM function, it's fine (just Pass 1 visiting Pass 0's work)
        if existing_info.llvm_value != llvm_function:
             compiler.errors.error(ast, f"Symbol '{func_name}' already defined.")
    else:
        target_scope.define(func_name, llvm_function)

    # [FIX] Return early for Scouting Pass
    if prototype_only:
        compiler.exit_scope()
        return llvm_function

    # 7. Compile Body
    if ast.body:
        prev_function = compiler.function
        prev_builder = compiler.builder
        
        compiler.function = llvm_function
        entry_block = llvm_function.append_basic_block(name="entry")
        compiler.builder = ir.IRBuilder(entry_block)

        for i, param_ast in enumerate(ast.params):
            llvm_arg = llvm_function.args[i]
            llvm_arg.name = param_ast.identifier
            
            compiler.create_variable_mut(
                param_ast.identifier, 
                param_ast.var_type, 
                initial_value_llvm=llvm_arg
            )

        for stmt_node in ast.body:
            compiler.compile(stmt_node)

        if not compiler.builder.block.is_terminated:
            if isinstance(llvm_ret_type, ir.VoidType):
                compiler.builder.ret_void()
            elif func_name == "main" and isinstance(llvm_ret_type, ir.IntType):
                 compiler.builder.ret(ir.Constant(llvm_ret_type, 0))
            else:
                compiler.errors.error(ast, f"Function '{func_name}' is missing a return statement.")
                compiler.builder.unreachable()

        compiler.function = prev_function
        compiler.builder = prev_builder
        
        if func_name == "main":
            compiler.main_function = llvm_function
    
    compiler.exit_scope()
    return llvm_function
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# <Method name=compile_return args=[<Compiler>, <ReturnStatement>]>
# <Description>
# Compiles 'return expr;' or 'return;'.
# Handles:
# 1. Dead Code Guard (skips if block terminated).
# 2. Void returns.
# 3. Struct Pointer -> Value conversion (if function returns Value).
# 4. Type Coercion (Int->Float, etc.).
# </Description>
def compile_return(compiler: Compiler, ast: ReturnStatement):
    # 1. Dead Code Guard
    if compiler.builder.block.is_terminated:
        return

    # 2. Void Return
    if ast.value is None:
        # Check if function expects void
        if not isinstance(compiler.function.function_type.return_type, ir.VoidType):
            compiler.errors.error(ast, "Function expects a return value, but got 'return;'.")
        compiler.builder.ret_void()
        return

    # 3. Compile Return Value
    ret_val = compiler.compile(ast.value)
    func_ret_type = compiler.function.function_type.return_type

    # 4. Handle Struct Pointer -> Struct Value
    # If the function returns a Struct Value (e.g. %MyStruct), but we have a Pointer (%MyStruct*),
    # we must load it.
    if isinstance(func_ret_type, ir.IdentifiedStructType) and \
       isinstance(ret_val.type, ir.PointerType) and \
       ret_val.type.pointee == func_ret_type:
        ret_val = compiler.builder.load(ret_val, name="ret_struct_load")

    # 5. Coercion (Basic)
    if ret_val.type != func_ret_type:
        if isinstance(func_ret_type, ir.FloatType) and isinstance(ret_val.type, ir.IntType):
            ret_val = compiler.builder.sitofp(ret_val, func_ret_type)
        elif isinstance(func_ret_type, ir.PointerType) and isinstance(ret_val.type, ir.PointerType):
            ret_val = compiler.builder.bitcast(ret_val, func_ret_type)
        
        # Check again
        if ret_val.type != func_ret_type:
             compiler.errors.error(ast, f"Return type mismatch. Expected {func_ret_type}, got {ret_val.type}")

    compiler.builder.ret(ret_val)

# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# <Method name=compile_lambda args=[<Compiler>, <LambdaNode>]>
# <Description>
# Compiles a lambda expression `(args) => { body }`.
# 1. Generates a unique global function name.
# 2. Compiles the function body in a new scope.
# 3. Returns the Function Pointer.
# Note: Currently implements Stateless Lambdas (no closure capture).
# </Description>
def compile_lambda(compiler: Compiler, ast: LambdaNode) -> ir.Value:
    # 1. Generate Unique Name
    name = f"__lambda_{compiler.block_count}_{uuid.uuid4().hex[:4]}"
    
    # 2. Save Compiler State (we are interrupting current function compilation)
    prev_function = compiler.function
    prev_builder = compiler.builder
    prev_scope = compiler.current_scope
    
    # 3. Prepare Signature
    llvm_ret_type = compiler.convert_type(ast.return_type)
    llvm_param_types = [compiler.convert_type(p.var_type) for p in ast.params]
    
    func_ty = ir.FunctionType(llvm_ret_type, llvm_param_types)
    
    # Create the function in the module
    lambda_func = ir.Function(compiler.module, func_ty, name=name)
    
    # 4. Compile Body
    compiler.function = lambda_func
    entry_block = lambda_func.append_basic_block(name="entry")
    compiler.builder = ir.IRBuilder(entry_block)
    
    # Create Scope
    # IMPORTANT: Parent is global_scope. 
    # We do NOT support closures (capturing locals) yet, so we prevent access to them
    # to avoid segfaults.
    lambda_scope = Scope(parent=compiler.global_scope)
    compiler.current_scope = lambda_scope
    
    # Bind Arguments
    for i, param in enumerate(ast.params):
        arg_val = lambda_func.args[i]
        arg_val.name = param.identifier
        
        # Use standard variable creation logic
        compiler.create_variable_mut(
            param.identifier, 
            param.var_type, 
            initial_value_llvm=arg_val
        )
        
    # Compile Statements
    # ast.body is a list of statements (from the block rule)
    for stmt in ast.body:
        compiler.compile(stmt)
        
    # Handle Implicit Return
    if not compiler.builder.block.is_terminated:
        if isinstance(llvm_ret_type, ir.VoidType):
            compiler.builder.ret_void()
        else:
            # If non-void lambda doesn't return, it's an error or UB.
            # For now, unreachable.
            compiler.builder.unreachable()
            
    # 5. Restore Compiler State
    compiler.function = prev_function
    compiler.builder = prev_builder
    compiler.current_scope = prev_scope
    
    # 6. Return the Function Pointer
    return lambda_func

# ---------------------------------------------------------------------------
# <Method name=compile_define args=[<Compiler>, <DefineDeclaration>]>
# <Description>
# Compiles '@define'. Registers an external C function.
# Handles:
# 1. Type Conversion (Fin Types -> LLVM Types).
# 2. Varargs (...).
# 3. Global Registration.
# </Description>
def compile_define(compiler: Compiler, ast: DefineDeclaration):
    func_name = ast.name
    
    # 1. Convert Parameters
    llvm_param_types = []
    for p_ast in ast.params:
        # p_ast is a Parameter node
        llvm_param_types.append(compiler.convert_type(p_ast.var_type))

    # 2. Convert Return Type
    llvm_ret_type = compiler.convert_type(ast.return_type)

    # 3. Create Function Type
    fn_ty = ir.FunctionType(
        llvm_ret_type,
        llvm_param_types,
        var_arg=ast.is_vararg
    )

    # 4. Check Attributes for Custom Name
    # Default to the name provided (e.g. "printf")
    # But allow #[llvm_name="alias"] override
    llvm_name = compiler.attributes_lib.resolve_llvm_name(ast, func_name)

    # 5. Declare in Module
    if llvm_name in compiler.module.globals:
        # It already exists (e.g. from builtins.fin)
        fn = compiler.module.globals[llvm_name]
        
        # Verify signature matches
        if fn.function_type != fn_ty:
             compiler.errors.error(ast, f"Redefinition of '@define {func_name}' with different signature.\nExisting: {fn.function_type}\nNew: {fn_ty}")
    else:
        # Create new declaration
        fn = ir.Function(compiler.module, fn_ty, name=llvm_name)
        

    # 6. Register in Global Scope
    # We define it in the global scope so it can be used anywhere.
    try:
        compiler.global_scope.define(func_name, fn)
    except Exception:
        # If already defined in scope (e.g. multiple imports of stdlib), ignore
        pass



