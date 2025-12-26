from .essentials import *

# ---------------------------------------------------------------------------
# <Method name=compile_try_catch args=[<Compiler>, <TryCatchNode>]>
# <Description>
# Compiles 'try { ... } catch(e) { ... }'.
# NOTE: Without LLVM 'invoke' and landingpads, this does NOT actually catch runtime exceptions.
# It compiles the 'try' block normally.
# It compiles the 'catch' block in a detached block to ensure semantic validity.
# </Description>
def compile_try_catch(compiler: Compiler, ast: TryCatchNode):
    # 1. Compile Try Block
    compiler.compile(ast.try_body)
    
    # If try block terminates (returns), we stop here.
    if compiler.builder.block.is_terminated:
        return

    # 2. Compile Catch Block (Placeholder)
    if ast.catch_body:
        # Create a block for catch, but DO NOT branch to it.
        # This prevents it from running, but allows us to compile it for type checking.
        # In a real implementation, 'invoke' would jump here on exception.
        catch_bb = compiler.function.append_basic_block("catch_block")
        
        # Save current block to resume after (if try didn't terminate)
        resume_bb = compiler.function.append_basic_block("try_continue")
        compiler.builder.branch(resume_bb)
        
        # Switch to Catch Block to compile it
        compiler.builder.position_at_end(catch_bb)
        compiler.enter_scope()
        
        if ast.catch_var:
            # Determine Error Type
            err_type = ir.IntType(8).as_pointer() # Default: i8*
            err_fin_type = None
            
            if hasattr(ast, 'catch_type') and ast.catch_type:
                err_type = compiler.convert_type(ast.catch_type)
                err_fin_type = compiler.ast_to_fin_type(ast.catch_type)
            
            # Create 'err' variable
            # Initialize with null since we don't have a real exception object
            init_val = ir.Constant(err_type, None)
            compiler.create_variable_mut(ast.catch_var, err_type, init_val)
        
        compiler.compile(ast.catch_body)
        
        # If catch doesn't return, branch to resume
        if not compiler.builder.block.is_terminated:
            compiler.builder.branch(resume_bb)
            
        compiler.exit_scope()
        
        # Resume normal flow
        compiler.builder.position_at_end(resume_bb)

# ---------------------------------------------------------------------------
# <Method name=compile_blame args=[<Compiler>, <BlameNode>]>
# <Description>
# Compiles 'blame expr'.
# 1. Compiles expression.
# 2. If Struct, extracts 'error_msg' field.
# 3. Calls '__panic' (from builtins).
# 4. Emits 'unreachable'.
# </Description>
def compile_blame(compiler: Compiler, ast: BlameNode):
    # 1. Compile Expression
    err_val = compiler.compile(ast.expression)
    msg_ptr = err_val

    # 2. Handle Structs (Extract error_msg)
    if isinstance(err_val.type, ir.PointerType) and \
       isinstance(err_val.type.pointee, ir.IdentifiedStructType):
        
        struct_name = err_val.type.pointee.name
        
        # Find 'error_msg' index
        indices = compiler.struct_field_indices.get(struct_name)
        if not indices:
            # Check imports
            for path, reg in compiler.module_struct_fields.items():
                if struct_name in reg:
                    indices = reg[struct_name]
                    break
        
        if indices and "error_msg" in indices:
            idx = indices["error_msg"]
            
            zero = ir.Constant(ir.IntType(32), 0)
            idx_val = ir.Constant(ir.IntType(32), idx)
            field_ptr = compiler.builder.gep(err_val, [zero, idx_val], inbounds=True)
            
            msg_ptr = compiler.builder.load(field_ptr, name="panic_msg_load")
        else:
            # Struct has no error_msg
            # We could try to cast to string, but likely unsafe.
            # Fallback to generic message
            msg_ptr = compiler.create_global_string(f"Panic object: {struct_name}")

    # 3. Ensure i8*
    if msg_ptr.type != ir.IntType(8).as_pointer():
        if isinstance(msg_ptr.type, ir.PointerType):
            msg_ptr = compiler.builder.bitcast(msg_ptr, ir.IntType(8).as_pointer())
        else:
            msg_ptr = compiler.create_global_string("Unknown Error Value")

    # 4. Call Panic
    try:
        panic_fn = compiler.module.get_global("__panic")
    except KeyError:
        # Fallback if builtins not loaded
        if hasattr(compiler, 'panic_func'):
            panic_fn = compiler.panic_func
        else:
            compiler.errors.error(ast, "Runtime Error: '__panic' function not found. Ensure builtins are loaded.")
            return

    compiler.builder.call(panic_fn, [msg_ptr])
    compiler.builder.unreachable()

# ---------------------------------------------------------------------------
# <Method name=compile_foreach args=[<Compiler>, <ForeachLoop>]>
# <Description>
# Compiles 'foreach var <Type> in collection { ... }'.
# Currently supports Arrays and Collections.
# Logic:
# 1. Compiles the collection expression.
# 2. Creates a hidden index variable (i = 0).
# 3. Generates loop blocks (Cond, Body, Inc, End).
# 4. In Body: Loads element at [i] and assigns to 'var'.
# 5. Compiles user body.
# </Description>
def compile_foreach(compiler: Compiler, ast: ForeachLoop):
    # 1. Compile Collection
    coll_val = compiler.compile(ast.collection)
    
    # Determine Length
    # We reuse the logic from compile_member_access/arrays to get length
    # But since we have the value, we can extract it directly if it's a Collection.
    
    length_val = None
    is_collection = False
    
    # Unwrap pointer
    check_type = coll_val.type
    if isinstance(check_type, ir.PointerType): check_type = check_type.pointee
    
    # Case A: Collection {T*, len}
    if isinstance(check_type, ir.LiteralStructType) and len(check_type.elements) == 2:
        is_collection = True
        zero = ir.Constant(ir.IntType(32), 0)
        one = ir.Constant(ir.IntType(32), 1)
        
        if isinstance(coll_val.type, ir.PointerType):
            len_ptr = compiler.builder.gep(coll_val, [zero, one], inbounds=True)
            length_val = compiler.builder.load(len_ptr, name="foreach_len")
        else:
            length_val = compiler.builder.extract_value(coll_val, 1, name="foreach_len")

    # Case B: Static Array [N x T]
    elif isinstance(check_type, ir.ArrayType):
        length_val = ir.Constant(ir.IntType(32), check_type.count)
        
    else:
        compiler.errors.error(ast.collection, f"Foreach expects an Array or Collection, got {check_type}")
        return

    # 2. Enter Scope
    compiler.enter_scope()

    # 3. Create Hidden Index Variable
    idx_name = f".foreach_idx_{compiler.block_count}"
    compiler.block_count += 1
    
    # i = 0
    init_val = ir.Constant(ir.IntType(32), 0)
    idx_ptr = compiler.create_variable_mut(idx_name, "int", init_val)

    # 4. Create Blocks
    cond_block = compiler.function.append_basic_block("foreach_cond")
    body_block = compiler.function.append_basic_block("foreach_body")
    inc_block = compiler.function.append_basic_block("foreach_inc")
    end_block = compiler.function.append_basic_block("foreach_end")

    compiler.builder.branch(cond_block)

    # 5. Condition: i < length
    compiler.builder.position_at_end(cond_block)
    curr_idx = compiler.builder.load(idx_ptr, name="idx_val")
    
    # Ensure length is i32
    if length_val.type != ir.IntType(32):
        length_val = compiler.builder.intcast(length_val, ir.IntType(32))
        
    cond = compiler.builder.icmp_unsigned("<", curr_idx, length_val, name="loop_cond")
    compiler.builder.cbranch(cond, body_block, end_block)

    # 6. Body
    compiler.builder.position_at_end(body_block)
    
    # Enter Inner Scope for user variable
    compiler.enter_scope(
        is_loop_scope=True,
        loop_cond_block=inc_block,
        loop_end_block=end_block
    )

    # Extract Element: var = coll[i]
    # We construct a synthetic ArrayIndexNode to reuse the robust logic in arrays.py
    # This handles bounds checking (redundant here but safe) and loading.
    
    # Problem: ArrayIndexNode expects AST nodes for array and index.
    # We have LLVM values.
    # Solution: Manually GEP and Load here since we know the types.
    
    elem_val = None
    if is_collection:
        # Collection: Load data ptr -> GEP
        zero = ir.Constant(ir.IntType(32), 0)
        if isinstance(coll_val.type, ir.PointerType):
            data_ptr_ptr = compiler.builder.gep(coll_val, [zero, zero], inbounds=True)
            data_ptr = compiler.builder.load(data_ptr_ptr)
        else:
            data_ptr = compiler.builder.extract_value(coll_val, 0)
            
        elem_ptr = compiler.builder.gep(data_ptr, [curr_idx])
        elem_val = compiler.builder.load(elem_ptr, name="elem_val")
        
    elif isinstance(check_type, ir.ArrayType):
        # Static Array
        zero = ir.Constant(ir.IntType(32), 0)
        # coll_val must be a pointer for GEP
        if not isinstance(coll_val.type, ir.PointerType):
            # Spill to stack if it's a value
            temp = compiler.builder.alloca(coll_val.type)
            compiler.builder.store(coll_val, temp)
            coll_val = temp
            
        elem_ptr = compiler.builder.gep(coll_val, [zero, curr_idx])
        elem_val = compiler.builder.load(elem_ptr, name="elem_val")

    # Create User Variable
    # ast.identifier is the name (e.g. "x")
    # ast.var_type is the type (e.g. "int" or "auto")
    
    var_type = ast.var_type
    if var_type == "auto":
        var_type = elem_val.type # Pass LLVM type for inference
        
    compiler.create_variable_mut(ast.identifier, var_type, initial_value_llvm=elem_val)

    # Compile User Body
    compiler.compile(ast.body)

    if not compiler.builder.block.is_terminated:
        compiler.builder.branch(inc_block)

    compiler.exit_scope() # Exit user scope

    # 7. Increment: i++
    compiler.builder.position_at_end(inc_block)
    curr_idx_2 = compiler.builder.load(idx_ptr)
    one = ir.Constant(ir.IntType(32), 1)
    next_idx = compiler.builder.add(curr_idx_2, one, name="idx_inc")
    compiler.builder.store(next_idx, idx_ptr)
    
    compiler.builder.branch(cond_block)

    # 8. End
    compiler.builder.position_at_end(end_block)
    compiler.exit_scope() # Exit loop scope