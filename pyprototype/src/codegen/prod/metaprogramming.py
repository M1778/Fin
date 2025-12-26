from .essentials import *


# ---------------------------------------------------------------------------
# <Method name=compile_special_declaration args=[<Compiler>, <SpecialDeclaration>]>
# <Description>
# Compiles a special function definition (starting with @).
# 1. Registers AST for Compile-Time Execution (Meta-Programming).
# 2. If #[llvm_name] attribute is present, compiles it to LLVM IR (Runtime Intrinsic).
#    Uses AttributeLibrary and create_function helper to avoid hardcoding.
# </Description>
def compile_special_declaration(compiler: Compiler, ast: SpecialDeclaration):
    # 1. Register AST (for compile-time execution via @name())
    if not hasattr(compiler, 'special_functions'):
        compiler.special_functions = {}
    compiler.special_functions[ast.name] = ast

    # 2. Check for Runtime Compilation (#[llvm_name])
    llvm_name_attr = ast.get_attr("llvm_name")
    
    if llvm_name_attr:
        # --- Runtime Compilation Path ---
        
        # A. Resolve Name
        llvm_name = compiler.attributes_lib.resolve_llvm_name(ast, ast.name)
        
        # B. Enter Scope
        compiler.enter_scope()
        
        # [FIX] Unpack Params correctly
        real_params = ast.params
        if isinstance(real_params, tuple):
            real_params = real_params[0] # [List, is_vararg]


        # C. Create Function
        # Return Type
        ret_type_ast = ast.return_type if ast.return_type else "void"
        
        # Param Types
        param_types_ast = [p.var_type for p in real_params]
        
        # Use helper
        llvm_function = compiler.create_function(llvm_name, ret_type_ast, param_types_ast)
        
        # D. Apply Attributes
        compiler.attributes_lib.apply(llvm_function, ast)
        
        # E. Compile Body
        # Re-bind arguments to correct names
        for i, param in enumerate(real_params):
            arg_val = llvm_function.args[i]
            arg_val.name = param.identifier
            
            # Allocate stack variable with correct name
            compiler.create_variable_mut(param.identifier, param.var_type, initial_value_llvm=arg_val)

        # Compile Statements
        for stmt in ast.body:
            compiler.compile(stmt)

        # F. Implicit Return
        if not compiler.builder.block.is_terminated:
            ret_type_llvm = llvm_function.function_type.return_type
            if isinstance(ret_type_llvm, ir.VoidType):
                compiler.builder.ret_void()
            else:
                compiler.builder.unreachable()

        # Restore State
        compiler.function = None
        compiler.builder = None
        compiler.exit_scope()
       
def compile_special_call(compiler: Compiler, ast: SpecialCallNode) -> ir.Value:
    # 1. Check Builtins
    if ast.name == "hasattr":
        if len(ast.args) != 2:
            compiler.errors.error(ast, "@hasattr expects 2 arguments.")
            return ir.Constant(ir.IntType(1), 0)
        
        obj_expr = ast.args[0]
        field_name_expr = ast.args[1]
        
        if not isinstance(field_name_expr, Literal) or not isinstance(field_name_expr.value, str):
            compiler.errors.error(ast, "Second argument to @hasattr must be a string literal.")
            return ir.Constant(ir.IntType(1), 0)
            
        fin_type = compiler._get_arg_fin_type(obj_expr, None)
        if isinstance(fin_type, PointerType): fin_type = fin_type.pointee
        
        if not isinstance(fin_type, StructType): return ir.Constant(ir.IntType(1), 0)
        
        struct_name = fin_type.name
        field_name = field_name_expr.value
        
        # Check registries
        indices = compiler.struct_field_indices.get(struct_name)
        if not indices:
            unmangled = struct_name.split("__")[-1]
            if unmangled in compiler.struct_field_indices:
                indices = compiler.struct_field_indices[unmangled]
            else:
                for path, reg in compiler.module_struct_fields.items():
                    if struct_name in reg: indices = reg[struct_name]; break
                    if unmangled in reg: indices = reg[unmangled]; break
        
        has_field = (indices is not None and field_name in indices)
        return ir.Constant(ir.IntType(1), 1 if has_field else 0)

    elif ast.name == "unsafe_unbox":
        if len(ast.args) != 2: return ir.Constant(ir.IntType(8).as_pointer(), None)
        ptr_val = compiler.compile(ast.args[0])
        target_type = compiler.convert_type(ast.args[1])
        return compiler.builder.bitcast(ptr_val, target_type)

    elif ast.name == "name":
        fin_type = compiler._get_arg_fin_type(ast.args[0], None)
        type_name = fin_type.get_signature().split("__")[-1]
        return compiler.create_global_string(type_name)
    # 2. Check Special Functions
    if hasattr(compiler, 'special_functions') and ast.name in compiler.special_functions:
        func_def = compiler.special_functions[ast.name]
        
        # EXECUTE THE MACRO
        # We compile the body statements *inline* into the current function.
        
        compiler.enter_scope()
        
        real_params = func_def.params
        if isinstance(real_params, tuple):
            real_params = real_params[0]
        
        # Bind Arguments
        # We compile the arguments passed to the call
        arg_vals = [compiler.compile(arg) for arg in ast.args]
        
        for i, param in enumerate(real_params):
            compiler.current_scope.define(param.identifier, arg_vals[i])

        # Compile Body
        ret_val = None
        for stmt in func_def.body:
            # We need to capture 'return' statements.
            # Since 'compile' usually emits 'ret', we need to intercept it?
            # OR, we assume special functions return an Expression that evaluates to a Value.
            
            # If the statement is a ReturnStatement, we capture the value and STOP.
            if isinstance(stmt, ReturnStatement):
                if stmt.value:
                    ret_val = compiler.compile(stmt.value)
                break
            else:
                compiler.compile(stmt)
        
        compiler.exit_scope()
        
        # Return the value produced by the macro
        if ret_val:
            return ret_val
        return ir.Constant(ir.VoidType(), None)

    compiler.errors.error(ast, f"Unknown special function: @{ast.name}")
    return ir.Constant(ir.IntType(32), 0)