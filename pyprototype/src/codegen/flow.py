from .essentials import *

# ---------------------------------------------------------------------------
# <Method name=create_block args=[<Compiler>, <str>]>
# <Description>
# Helper to create a new Basic Block in the current function and position the builder at its end.
# 
# Note: This does NOT terminate the previous block. The caller must ensure
# the previous block has a terminator (br, ret, switch) before flow reaches here,
# unless this is the start of a new disjoint path.
# </Description>
def create_block(compiler: Compiler, name: str) -> ir.Block:
    if compiler.function is None:
        raise Exception("Cannot create block: No function is currently being compiled.")
    
    block = compiler.function.append_basic_block(name)
    
    if compiler.builder is None:
        # Should not happen if function is set, but safety first
        compiler.builder = ir.IRBuilder(block)
    else:
        compiler.builder.position_at_end(block)
        
    return block

# ---------------------------------------------------------------------------
# <Method name=compile_if args=[<Compiler>, <IfStatement>]>
# <Description>
# Compiles if/elseif/else constructs.
# Handles nested blocks and merging control flow.
# </Description>
# ---------------------------------------------------------------------------
# <Method name=compile_if args=[<Compiler>, <IfStatement>]>
# <Description>
# Compiles if/elseif/else constructs.
# Handles:
# 1. Control Flow (Branching/Merging).
# 2. Scoping (Each block gets a scope).
# 3. Smart Casting (Flow-Sensitive Typing for 'any').
# </Description>
def compile_if(compiler: Compiler, ast: IfStatement):
    func = compiler.function
    if_id_suffix = f".{compiler.block_count}"
    compiler.block_count += 1

    # --- Helper: Smart Cast Detection ---
    def _detect_smart_cast(condition_node):
        """
        Checks if condition is 'typeof(var) == Type' OR 'typeof(var) == typeof(Type)'.
        Returns (var_name, fin_type) or (None, None).
        """
        if isinstance(condition_node, ComparisonOperator) and condition_node.operator == "==":
            left, right = condition_node.left, condition_node.right
            
            # Check Left: typeof(x)
            if isinstance(left, TypeOf) and isinstance(left.expr, str):
                var_name = left.expr
                
                # Check Right: Type Name OR typeof(Type Name)
                type_name = None
                
                # Case 1: Direct Type Name (Legacy/Non-Standard: typeof(x) == int)
                if isinstance(right, str): 
                    type_name = right
                elif hasattr(right, 'name'): # GenericTypeNode, etc.
                    type_name = right.name
                
                # Case 2: Wrapped Type (Standard: typeof(x) == typeof(int))
                elif isinstance(right, TypeOf):
                    # right.expr is the type inside typeof(...)
                    type_node = right.expr
                    
                    # It could be ANY type node (Array, Pointer, Generic, etc.)
                    # We should just try to convert it.
                    try:
                        target_type = compiler.ast_to_fin_type(type_node)
                        if not (isinstance(target_type, PrimitiveType) and target_type.name == "unknown"):
                            return var_name, target_type
                    except:
                        pass
                
                if type_name:
                    try:
                        # Resolve the type
                        target_type = compiler.ast_to_fin_type(type_name)
                        # Ensure it's a valid type (not "unknown")
                        if not (isinstance(target_type, PrimitiveType) and target_type.name == "unknown"):
                            return var_name, target_type
                    except:
                        pass
        return None, None
    # --- Helper: Compile Body with Scope & Smart Cast ---
    def _compile_guarded_body(condition_node, body_nodes):
        compiler.enter_scope()
        
        # 1. Check for Smart Cast
        smart_var, smart_type = _detect_smart_cast(condition_node)
        # print(smart_var, smart_type, type(smart_var), type(smart_type))
        # if smart_var and isinstance(smart_type, ir.Type):
        #     resolve_ = compiler.current_scope.resolve(smart_var)
        #     print(resolve_)
        if smart_var:
            # Try to resolve the variable in the PARENT scope (before we shadowed it)
            # We need the 'any' pointer.
            # Since we just entered a scope, resolve() checks parent.
            any_ptr = compiler.current_scope.resolve(smart_var)
            
            # Verify it is actually an 'any' type
            # (We check the FinType stored in the scope)
            any_fin_type = compiler.current_scope.resolve_type(smart_var)
            
            # if any_ptr and isinstance(any_fin_type, AnyType):
            #     # Perform Unboxing
            #     # 'any' struct is { i8* data, i64 type_id }
            #     # We need to load 'data' (index 0)
                
            #     # Handle indirection (any* vs any**)
            #     val_ptr = any_ptr
            #     if isinstance(val_ptr.type, ir.PointerType) and isinstance(val_ptr.type.pointee, ir.PointerType):
            #         val_ptr = compiler.builder.load(val_ptr)

            #     zero = ir.Constant(ir.IntType(32), 0)
            #     data_ptr_ptr = compiler.builder.gep(val_ptr, [zero, zero], name="smart_cast_gep")
            #     data_ptr = compiler.builder.load(data_ptr_ptr, name="smart_cast_data")
                
            #     # Unbox/Cast to Concrete Type
            #     unboxed_val = compiler.unbox_value(data_ptr, smart_type)
                
            #     # Shadow the variable in the current scope
            #     # This effectively "changes the type" for the duration of this block
            #     compiler.create_variable_mut(smart_var, smart_type, unboxed_val)
            #     # print(f"[DEBUG] Smart cast applied: {smart_var} is now {smart_type}")
            if any_ptr and isinstance(any_fin_type, StructType):
                val_ptr = any_ptr
                print(val_ptr, val_ptr.type)
                if isinstance(val_ptr.type, ir.PointerType):...

        # 2. Compile Body
        compiler.compile(body_nodes)
        compiler.exit_scope()

    # =========================================================================
    
    # 1. Compile Main Condition
    cond_val = compiler.compile(ast.condition)
    if not (isinstance(cond_val.type, ir.IntType) and cond_val.type.width == 1):
        compiler.errors.error(ast.condition, f"If condition must be a boolean (i1), got {cond_val.type}")

    # 2. Create Blocks
    then_bb = func.append_basic_block(f"if_then{if_id_suffix}")
    merge_bb = func.append_basic_block(f"if_merge{if_id_suffix}")
    
    current_false_target_bb = merge_bb
    if ast.elifs or ast.else_body:
        current_false_target_bb = func.append_basic_block(f"if_cond_false{if_id_suffix}")

    compiler.builder.cbranch(cond_val, then_bb, current_false_target_bb)

    # 3. Compile THEN Block
    compiler.builder.position_at_end(then_bb)
    _compile_guarded_body(ast.condition, ast.body) # [FIX] Use Helper
    
    if not compiler.builder.block.is_terminated:
        compiler.builder.branch(merge_bb)

    # 4. Compile ELIF Blocks
    if ast.elifs:
        for i, (elif_cond_ast, elif_body_ast) in enumerate(ast.elifs):
            compiler.builder.position_at_end(current_false_target_bb)

            elif_then_bb = func.append_basic_block(f"elif{i}_then{if_id_suffix}")

            next_false_target_for_elif = merge_bb
            if i < len(ast.elifs) - 1 or ast.else_body:
                next_false_target_for_elif = func.append_basic_block(f"elif{i}_false_path{if_id_suffix}")

            # Compile Elif Condition
            elif_cond_val = compiler.compile(elif_cond_ast)
            compiler.builder.cbranch(elif_cond_val, elif_then_bb, next_false_target_for_elif)

            # Compile Elif Body
            compiler.builder.position_at_end(elif_then_bb)
            _compile_guarded_body(elif_cond_ast, elif_body_ast) # [FIX] Use Helper
            
            if not compiler.builder.block.is_terminated:
                compiler.builder.branch(merge_bb)

            current_false_target_bb = next_false_target_for_elif

    # 5. Compile ELSE Block
    compiler.builder.position_at_end(current_false_target_bb)
    if ast.else_body:
        # Else block doesn't get smart casting (condition is inverted/unknown)
        compiler.enter_scope()
        compiler.compile(ast.else_body)
        compiler.exit_scope()
        
        if not compiler.builder.block.is_terminated:
            compiler.builder.branch(merge_bb)
    elif current_false_target_bb != merge_bb:
        if not compiler.builder.block.is_terminated:
            compiler.builder.branch(merge_bb)

    # 6. Resume at Merge Block
    compiler.builder.position_at_end(merge_bb)
# ---------------------------------------------------------------------------
# <Method name=compile_while args=[<Compiler>, <WhileLoop>]>
# <Description>
# Compiles 'while' loops.
# Manages Loop Scope to allow 'break' and 'continue' to find targets.
# </Description>
def compile_while(compiler: Compiler, ast: WhileLoop):
    # 1. Create Blocks
    cond_block = compiler.function.append_basic_block("while_cond")
    body_block = compiler.function.append_basic_block("while_body")
    end_block = compiler.function.append_basic_block("while_end")

    # Jump to condition
    if not compiler.builder.block.is_terminated:
        compiler.builder.branch(cond_block)

    # 2. Compile Condition
    compiler.builder.position_at_end(cond_block)
    cond_val = compiler.compile(ast.condition)
    
    if not (isinstance(cond_val.type, ir.IntType) and cond_val.type.width == 1):
        compiler.errors.error(ast.condition, f"While condition must be boolean, got {cond_val.type}")
        
    compiler.builder.cbranch(cond_val, body_block, end_block)

    # 3. Compile Body
    compiler.builder.position_at_end(body_block)

    # Enter Loop Scope
    # This registers the blocks so 'break' and 'continue' know where to jump
    compiler.enter_scope(
        is_loop_scope=True,
        loop_cond_block=cond_block,
        loop_end_block=end_block,
    )

    compiler.compile(ast.body)

    if not compiler.builder.block.is_terminated:
        compiler.builder.branch(cond_block)

    compiler.exit_scope()

    # 4. Resume
    compiler.builder.position_at_end(end_block)

# ---------------------------------------------------------------------------
# <Method name=compile_for args=[<Compiler>, <ForLoop>]>
# <Description>
# Compiles C-style 'for' loops.
# Structure: Init -> Cond -> Body -> Increment -> Cond
# </Description>
def compile_for(compiler: Compiler, ast: ForLoop):
    # 1. Enter Scope (for the Init variable)
    compiler.enter_scope()

    # 2. Compile Init (e.g. let i = 0)
    if ast.init is not None:
        compiler.compile(ast.init)

    # 3. Create Blocks
    cond_block = compiler.function.append_basic_block("for_cond")
    body_block = compiler.function.append_basic_block("for_body")
    inc_block = compiler.function.append_basic_block("for_inc")
    end_block = compiler.function.append_basic_block("for_end")

    compiler.builder.branch(cond_block)

    # 4. Compile Condition
    compiler.builder.position_at_end(cond_block)
    cond_val = compiler.compile(ast.condition)
    
    if not (isinstance(cond_val.type, ir.IntType) and cond_val.type.width == 1):
        compiler.errors.error(ast.condition, f"For loop condition must be boolean.")
        
    compiler.builder.cbranch(cond_val, body_block, end_block)

    # 5. Compile Body
    compiler.builder.position_at_end(body_block)

    # Enter Inner Loop Scope
    # Note: 'continue' jumps to INC block, not COND block in a for-loop!
    compiler.enter_scope(
        is_loop_scope=True,
        loop_cond_block=inc_block, # Continue goes to increment
        loop_end_block=end_block,
    )

    compiler.compile(ast.body)

    if not compiler.builder.block.is_terminated:
        compiler.builder.branch(inc_block)

    compiler.exit_scope() # Exit inner scope

    # 6. Compile Increment
    compiler.builder.position_at_end(inc_block)
    if ast.increment is not None:
        compiler.compile(ast.increment)

    if not compiler.builder.block.is_terminated:
        compiler.builder.branch(cond_block)

    # 7. Resume
    compiler.builder.position_at_end(end_block)
    compiler.exit_scope() # Exit init scope

# ---------------------------------------------------------------------------
# <Method name=compile_control_statement args=[<Compiler>, <ControlStatement>]>
# <Description>
# Compiles 'break' and 'continue'.
# </Description>
def compile_control_statement(compiler: Compiler, ast: ControlStatement):
    control_type = ast.control_type

    # Find the nearest loop scope
    active_loop_scope = compiler.current_scope.find_loop_scope()
    
    if active_loop_scope is None:
        compiler.errors.error(ast, f"'{control_type}' statement found outside of any loop construct.")
        return

    if control_type == "break":
        if active_loop_scope.loop_end_block is None:
            compiler.errors.error(ast, "Internal Error: Loop scope missing end block.")
            return
        compiler.builder.branch(active_loop_scope.loop_end_block)
        
    elif control_type == "continue":
        if active_loop_scope.loop_cond_block is None:
            compiler.errors.error(ast, "Internal Error: Loop scope missing condition block.")
            return
        compiler.builder.branch(active_loop_scope.loop_cond_block)
        
    else:
        compiler.errors.error(ast, f"Unsupported control statement: '{control_type}'")
        
        
