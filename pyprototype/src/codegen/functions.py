from .essentials import *

# <Method name=compile_function_call args=[<Compiler>, <FunctionCall>]>
# <Description>
# Compiles a function call.
# Handles Compiler Intrinsics (compiler.xxx) and standard calls.
# </Description>
def compile_function_call(compiler: Compiler, ast: FunctionCall) -> ir.Value:
    # --- Case 0: Compiler Intrinsics ---
    if isinstance(ast.call_name, str) and ast.call_name.startswith("compiler."):
        arg_vals = [compiler.compile(arg) for arg in ast.params]
        if hasattr(compiler, 'intrinsics_lib'):
            return compiler.intrinsics_lib.dispatch_call(ast.call_name, arg_vals)
        else:
            raise Exception("Compiler intrinsics engine not initialized.")

    func_to_call_llvm = None
    
    # --- Case 1: Super Constructor Call ---
    if isinstance(ast.call_name, SuperNode):
        if not compiler.current_struct_name:
            compiler.errors.error(ast, "'super' call used outside of a struct.")
            return ir.Constant(ir.IntType(32), 0)
        
        current_ast_name = getattr(compiler, 'current_struct_ast_name', None)
        parent_name = None
        if current_ast_name and current_ast_name in compiler.struct_parents_registry:
            parents = compiler.struct_parents_registry[current_ast_name]
            if parents:
                parent_node = parents[0]
                parent_name = parent_node if isinstance(parent_node, str) else getattr(parent_node, 'base_name', str(parent_node))
        
        if not parent_name:
            compiler.errors.error(ast, "Cannot call 'super()': No parent struct found.")
            return ir.Constant(ir.IntType(32), 0)

        mangled_parent = compiler.get_mangled_name(parent_name)
        ctor_name = f"{mangled_parent}__init"
        try:
            func_to_call_llvm = compiler.module.get_global(ctor_name)
        except KeyError:
            compiler.errors.error(ast, f"Parent struct '{parent_name}' has no constructor.")
            return ir.Constant(ir.IntType(32), 0)

        arg_llvm_values = [compiler.compile(arg) for arg in ast.params]
        temp_parent_ptr = compiler.builder.call(func_to_call_llvm, arg_llvm_values, name="super_temp_obj")

        self_ptr_addr = compiler.current_scope.resolve("self")
        if not self_ptr_addr:
            compiler.errors.error(ast, "'self' not found in scope for super call.")
            return ir.Constant(ir.IntType(32), 0)
        
        # In constructor, self is Struct* (no load needed)
        parent_ty = compiler.struct_types[mangled_parent]
        self_as_parent_ptr = compiler.builder.bitcast(self_ptr_addr, parent_ty.as_pointer(), name="super_self_cast")
        
        temp_val = compiler.builder.load(temp_parent_ptr, name="super_temp_val")
        compiler.builder.store(temp_val, self_as_parent_ptr)
        
        return self_as_parent_ptr

    # --- Case 2: String Name ---
    elif isinstance(ast.call_name, str):
        call_name_str = ast.call_name
        
        # A. Check for Struct Constructor
        mangled_struct_name = compiler.get_mangled_name(call_name_str)
        struct_exists = (mangled_struct_name in compiler.struct_types) or \
                        (call_name_str in compiler.struct_types)

        if struct_exists:
            ctor_name = f"{mangled_struct_name}__init"
            try:
                func_to_call_llvm = compiler.module.get_global(ctor_name)
            except KeyError:
                try:
                    func_to_call_llvm = compiler.module.get_global(f"{call_name_str}__init")
                except KeyError:
                    compiler.errors.error(ast, f"Struct '{call_name_str}' does not have a constructor defined.")
                    return ir.Constant(ir.IntType(32), 0)
            
            arg_llvm_values = [compiler.compile(arg) for arg in ast.params]
            return compiler.builder.call(func_to_call_llvm, arg_llvm_values, name=f"new_{call_name_str}")

        # B. Check for Monomorphization Template
        if call_name_str in compiler.function_templates:
            template_ast = compiler.function_templates[call_name_str]
            
            arg_llvm_values = [compiler.compile(arg) for arg in ast.params]
            
            bindings = {}
            generic_params = template_ast.type_parameters
            gen_names = [p.name for p in generic_params]
            
            if ast.generic_args:
                if len(ast.generic_args) != len(gen_names):
                    compiler.errors.error(ast, f"Function '{call_name_str}' expects {len(gen_names)} generic arguments, got {len(ast.generic_args)}.")
                    return ir.Constant(ir.IntType(32), 0)
                
                for i, param_obj in enumerate(generic_params):
                    tp_name = param_obj.name
                    explicit_type = compiler.ast_to_fin_type(ast.generic_args[i])
                    bindings[tp_name] = explicit_type
            else:
                for i, param_def in enumerate(template_ast.params):
                    if i >= len(arg_llvm_values): break
                    concrete_fin_type = compiler.get_arg_fin_type(ast.params[i], arg_llvm_values[i])
                    pattern_fin_type = compiler.ast_to_fin_type_pattern(param_def.var_type, generic_params)
                    compiler.match_generic_types(concrete_fin_type, pattern_fin_type, bindings)
                
                for name in gen_names:
                    if name not in bindings:
                        compiler.errors.error(ast, f"Could not infer generic type '{name}' for function '{call_name_str}'.")
                        return ir.Constant(ir.IntType(32), 0)

            for name in list(bindings.keys()):
                val = bindings[name]
                if isinstance(val, FinType):
                    bindings[name] = compiler.fin_type_to_ast(val)

            type_args_str = [str(bindings[name]) for name in gen_names]
            inst_name = compiler.get_mono_mangled_name(call_name_str, type_args_str)
            
            if inst_name in compiler.mono_function_cache:
                func_to_call_llvm = compiler.mono_function_cache[inst_name]
            else:
                concrete_func_ast = deepcopy(template_ast)
                concrete_func_ast.name = inst_name
                concrete_func_ast.type_parameters = []
                compiler._substitute_ast_types(concrete_func_ast, bindings)
                func_to_call_llvm = compiler.compile(concrete_func_ast)
                compiler.mono_function_cache[inst_name] = func_to_call_llvm
            
            return compiler.builder.call(func_to_call_llvm, arg_llvm_values)

        # C. Standard Function Resolution
        resolved_symbol = compiler.current_scope.resolve(call_name_str)

        if resolved_symbol is None:
            try:
                resolved_symbol = compiler.module.get_global(call_name_str)
            except KeyError:
                pass

        if isinstance(resolved_symbol, ir.Function):
            func_to_call_llvm = resolved_symbol
        
        elif isinstance(resolved_symbol, dict) and resolved_symbol.get("_is_generic_template"):
            pass 
        
        elif isinstance(resolved_symbol, (ir.AllocaInstr, ir.GlobalVariable, ir.Argument)):
                loaded_val = resolved_symbol
                if isinstance(resolved_symbol, (ir.AllocaInstr, ir.GlobalVariable)):
                    loaded_val = compiler.builder.load(resolved_symbol)
                
                if isinstance(loaded_val.type, ir.PointerType) and isinstance(loaded_val.type.pointee, ir.FunctionType):
                    func_to_call_llvm = loaded_val
                else:
                    compiler.errors.error(ast, f"Symbol '{call_name_str}' is not callable.")
                    return ir.Constant(ir.IntType(32), 0)

    # --- Case 3: Module Access ---
    elif isinstance(ast.call_name, ModuleAccess):
        resolved_ma_symbol = compiler.compile(ast.call_name)
        if isinstance(resolved_ma_symbol, ir.Function):
            func_to_call_llvm = resolved_ma_symbol
        else:
            compiler.errors.error(ast, f"Module access call '{ast.call_name}' did not resolve to a function.")
            return ir.Constant(ir.IntType(32), 0)

    # --- Case 4: Other Expressions ---
    else:
        func_to_call_llvm = compiler.compile(ast.call_name)
        if not (isinstance(func_to_call_llvm.type, ir.PointerType) and 
                isinstance(func_to_call_llvm.type.pointee, ir.FunctionType)):
            compiler.errors.error(ast, f"Expression '{ast.call_name}' is not a function pointer.")
            return ir.Constant(ir.IntType(32), 0)

    # --- EXECUTE CALL ---
    if func_to_call_llvm is None:
        compiler.errors.error(ast, f"Function '{ast.call_name}' not found or is not callable.")
        return ir.Constant(ir.IntType(32), 0)

    # [FIX] Resolve Function Type (Signature)
    fn_ty = func_to_call_llvm.function_type
    if isinstance(func_to_call_llvm.type, ir.PointerType):
        fn_ty = func_to_call_llvm.type.pointee

    # [FIX] Handle Default Arguments AND Varargs
    func_def_ast = None
    if isinstance(ast.call_name, str):
        func_def_ast = compiler.function_registry.get(ast.call_name)
    
    arg_llvm_values = []
    expected_params = func_def_ast.params if func_def_ast else []
    
    # Check if the last param is a Rest Parameter (...args)
    has_rest_param = False
    if expected_params and expected_params[-1].is_vararg:
        has_rest_param = True
    
    # Determine fixed arg count
    num_fixed = len(expected_params)
    if has_rest_param:
        num_fixed -= 1 # The last one is dynamic
    
    # 1. Process Fixed Arguments
    for i in range(num_fixed):
        if i < len(ast.params):
            val = compiler.compile(ast.params[i])
            arg_llvm_values.append(val)
        else:
            # Check default
            param = expected_params[i]
            if param.default_value is not None:
                val = compiler.compile(param.default_value)
                arg_llvm_values.append(val)
            else:
                compiler.errors.error(ast, f"Missing argument '{param.identifier}' for function '{ast.call_name}'")
                return ir.Constant(ir.IntType(32), 0)

    # 2. Process Rest Argument
    if has_rest_param:
        rest_param = expected_params[-1]
        rest_type_ast = rest_param.var_type # Should be ArrayTypeNode
        
        # Collect remaining args
        remaining_args_ast = ast.params[num_fixed:]
        
        # Compile them
        compiled_rest_args = [compiler.compile(a) for a in remaining_args_ast]
        
        # Determine Element Type
        rest_llvm_type = compiler.convert_type(rest_type_ast)
        element_llvm_type = None
        if isinstance(rest_llvm_type, ir.LiteralStructType) and len(rest_llvm_type.elements) == 2:
             element_llvm_type = rest_llvm_type.elements[0].pointee
        elif isinstance(rest_llvm_type, ir.ArrayType):
             element_llvm_type = rest_llvm_type.element
        else:
             compiler.errors.error(ast, f"Rest parameter '{rest_param.identifier}' must be an array type.")
             return ir.Constant(ir.IntType(32), 0)

        # Create Collection from Values
        count = len(compiled_rest_args)
        
        # Calculate Size
        if compiler.data_layout_obj:
            elem_size_int = element_llvm_type.get_abi_size(compiler.data_layout_obj)
            elem_size = ir.Constant(ir.IntType(64), elem_size_int)
        else:
            null_ptr = ir.Constant(element_llvm_type.as_pointer(), None)
            one = ir.Constant(ir.IntType(32), 1)
            gep = compiler.builder.gep(null_ptr, [one])
            elem_size = compiler.builder.ptrtoint(gep, ir.IntType(64))
            
        count_val = ir.Constant(ir.IntType(64), count)
        total_size = compiler.builder.mul(count_val, elem_size)
        
        # Malloc
        try:
            malloc_fn = compiler.module.get_global("malloc")
        except KeyError:
            malloc_ty = ir.FunctionType(ir.IntType(8).as_pointer(), [ir.IntType(64)])
            malloc_fn = ir.Function(compiler.module, malloc_ty, name="malloc")
            
        raw_ptr = compiler.builder.call(malloc_fn, [total_size], name="vararg_malloc")
        data_ptr = compiler.builder.bitcast(raw_ptr, element_llvm_type.as_pointer())
        
        # Store
        for idx, val in enumerate(compiled_rest_args):
            # Coercion check
            if val.type != element_llvm_type:
                if isinstance(element_llvm_type, ir.FloatType) and isinstance(val.type, ir.IntType):
                    val = compiler.builder.sitofp(val, element_llvm_type)
                elif isinstance(element_llvm_type, ir.PointerType) and isinstance(val.type, ir.PointerType):
                    val = compiler.builder.bitcast(val, element_llvm_type)
            
            dest = compiler.builder.gep(data_ptr, [ir.Constant(ir.IntType(32), idx)])
            compiler.builder.store(val, dest)
            
        # Create Collection Struct
        coll_val = ir.Constant(rest_llvm_type, ir.Undefined)
        coll_val = compiler.builder.insert_value(coll_val, data_ptr, 0)
        coll_val = compiler.builder.insert_value(coll_val, ir.Constant(ir.IntType(32), count), 1)
        
        arg_llvm_values.append(coll_val)

    else:
        # No rest param, check for extra args (C-style varargs)
        if len(ast.params) > num_fixed:
             if not fn_ty.var_arg:
                 compiler.errors.error(ast, f"Too many arguments for '{ast.call_name}'")
                 return ir.Constant(ir.IntType(32), 0)
             
             # It IS C-style varargs, compile remaining args normally
             for i in range(num_fixed, len(ast.params)):
                 arg_llvm_values.append(compiler.compile(ast.params[i]))

    # --- Coercion & Interface Packing ---
    final_args = []
    for i, arg_val in enumerate(arg_llvm_values):
        if i >= len(fn_ty.args):
            if fn_ty.var_arg:
                if isinstance(arg_val.type, ir.FloatType):
                    arg_val = compiler.builder.fpext(arg_val, ir.DoubleType())
                final_args.append(arg_val)
                continue
            else:
                    compiler.errors.error(ast, f"Too many arguments for '{ast.call_name}'")
                    return ir.Constant(ir.IntType(32), 0)
        
        expected_type = fn_ty.args[i]
        
        # 'any' Packing (Concrete -> {i8*, i64})
        is_any_expected = (isinstance(expected_type, ir.LiteralStructType) and 
                           len(expected_type.elements) == 2 and 
                           expected_type.elements[1] == ir.IntType(64))
        
        if is_any_expected and arg_val.type != expected_type:
            # Infer type and pack
            fin_type = compiler.infer_fin_type_from_llvm(arg_val.type)
            arg_val = compiler.pack_any(arg_val, fin_type)

        # Interface Packing
        is_interface_expected = (isinstance(expected_type, ir.LiteralStructType) and 
                                    len(expected_type.elements) == 2 and 
                                    isinstance(expected_type.elements[0], ir.PointerType) and 
                                    isinstance(expected_type.elements[1], ir.PointerType))
        
        if is_interface_expected:
            is_struct_arg = False
            if isinstance(arg_val.type, ir.IdentifiedStructType): is_struct_arg = True
            if isinstance(arg_val.type, ir.PointerType) and isinstance(arg_val.type.pointee, ir.IdentifiedStructType): is_struct_arg = True
            
            if is_struct_arg:
                arg_val = compiler.pack_interface(arg_val, arg_val.type, expected_type)

        # Standard Coercion
        if arg_val.type != expected_type:
            if isinstance(expected_type, ir.FloatType) and isinstance(arg_val.type, ir.IntType):
                arg_val = compiler.builder.sitofp(arg_val, expected_type)
            elif isinstance(expected_type, ir.PointerType) and isinstance(arg_val.type, ir.PointerType):
                arg_val = compiler.builder.bitcast(arg_val, expected_type)
            elif isinstance(expected_type, ir.IntType) and isinstance(arg_val.type, ir.IntType):
                if arg_val.type.width < expected_type.width:
                    arg_val = compiler.builder.sext(arg_val, expected_type)
                elif arg_val.type.width > expected_type.width:
                    arg_val = compiler.builder.trunc(arg_val, expected_type)
        
        final_args.append(arg_val)

    return compiler.builder.call(func_to_call_llvm, final_args)
# ---------------------------------------------------------------------------
# <Method name=_instantiate_and_compile_generic args=[...]>
# <Description>
# Helper to monomorphize (instantiate) a generic function template.
# 1. Generates a unique mangled name based on concrete types.
# 2. Checks if already compiled.
# 3. Clones the AST and substitutes 'T' with concrete types.
# 4. Compiles the new concrete function.
# </Description>
def instantiate_and_compile_generic(
    compiler: Compiler,
    func_name_str: str,
    generic_func_ast: FunctionDeclaration,
    inferred_bindings: Dict[str, Any], # Map T -> FinType/LLVMType
    concrete_types_tuple: Tuple[Any, ...]
) -> ir.Function:
    
    # 1. Generate Mangled Name
    # e.g. "add" + (int, int) -> "add_int_int"
    type_names = []
    for t in concrete_types_tuple:
        # Convert LLVM types or FinTypes to string representation for mangling
        t_str = str(t).replace(" ", "").replace("*", "p").replace("[", "arr").replace("]", "").replace("%", "")
        type_names.append(t_str)

    type_suffix = "_".join(type_names)
    # Sanitize
    type_suffix = re.sub(r"[^a-zA-Z0-9_p]", "", type_suffix)

    mangled_name = f"{func_name_str}__{type_suffix}"
    
    # Safety limit for name length
    if len(mangled_name) > 200:
        mangled_name = f"{func_name_str}__{uuid.uuid4().hex[:8]}"

    # 2. Check Cache / Global Module
    try:
        return compiler.module.get_global(mangled_name)
    except KeyError:
        pass

    # 3. AST Substitution (The Robust Way)
    # We deepcopy the template so we don't modify the original generic definition
    concrete_ast = deepcopy(generic_func_ast)
    concrete_ast.name = mangled_name
    concrete_ast.type_parameters = [] # It is no longer generic
    
    # Convert inferred_bindings values to strings/AST nodes for substitution
    # inferred_bindings might contain LLVM types, we need to map them back to AST-compatible types if possible,
    # or rely on the fact that _substitute_ast_types handles strings.
    ast_bindings = {}
    for k, v in inferred_bindings.items():
        ast_bindings[k] = str(v) # Simple string substitution for now

    compiler._substitute_ast_types(concrete_ast, ast_bindings)

    # 4. Compile
    # This recursively calls compiler.compile, which handles the new function declaration
    instantiated_llvm_func = compiler.compile(concrete_ast)

    if not isinstance(instantiated_llvm_func, ir.Function):
        compiler.errors.error(generic_func_ast, 
            f"Instantiation of '{func_name_str}' to '{mangled_name}' failed to produce an LLVM function.")
        return None

    # 5. Cache
    compiler.instantiated_functions[(func_name_str, concrete_types_tuple)] = instantiated_llvm_func
    
    return instantiated_llvm_func

# ---------------------------------------------------------------------------
# <Method name=create_function args=[<Compiler>, <str>, <Any>, <List[Any]>]>
# <Description>
# Helper to create a basic LLVM function, entry block, and builder.
# Used internally for creating intrinsics or synthetic functions.
# 1. Converts types to LLVM.
# 2. Creates ir.Function.
# 3. Sets up Entry Block and Builder.
# 4. Registers arguments in the current scope with inferred FinTypes.
# </Description>
def create_function(compiler: Compiler, name: str, ret_type: Any, arg_types: List[Any]) -> ir.Function:
    # 1. Convert Types
    conv_ret_type = compiler.convert_type(ret_type)
    llvm_arg_types = [compiler.convert_type(arg) for arg in arg_types]
    
    func_type = ir.FunctionType(conv_ret_type, llvm_arg_types)

    # 2. Create Function
    function = ir.Function(compiler.module, func_type, name)
    compiler.function = function

    # 3. Setup Entry Block
    entry_block = function.append_basic_block(name + "_entry")
    compiler.builder = ir.IRBuilder(entry_block)

    # 4. Register Arguments in Scope
    # We name them arg0, arg1, etc. since we don't have AST names here.
    for i, arg in enumerate(function.args):
        arg_name = f"arg{i}"
        arg.name = arg_name
        
        # Allocate stack space for mutability (standard Fin convention)
        arg_ptr = compiler.builder.alloca(arg.type, name=f"{arg_name}_ptr")
        compiler.builder.store(arg, arg_ptr)
        
        # Infer FinType so these args work with box/unbox logic
        fin_type = compiler.infer_fin_type_from_llvm(arg.type)
        
        compiler.current_scope.define(arg_name, arg_ptr, fin_type)

    return function



