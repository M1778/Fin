from .essentials import *


# ---------------------------------------------------------------------------
# <Method name=compile_assignment args=[<Compiler>, <Assignment>]>
# <Description>
# Compiles assignment statements (x = y, x += y).
# Handles:
# 1. L-Value Resolution (Variable, Field, Array Element, Dereference).
# 2. Interface Packing (Struct -> Interface).
# 3. Type Erasure (Auto-Boxing).
# 4. Type Coercion (Int->Float, Ptr->Ptr).
# 5. Compound Assignment (Load -> Op -> Store).
# </Description>
def compile_assignment(compiler: Compiler, ast: Assignment):
    lhs_node = ast.identifier
    rhs_node = ast.value
    op = ast.operator

    # 1. Compile RHS (Value)
    rhs_val = compiler.compile(rhs_node)
    
    # 2. Resolve LHS (Pointer/Address)
    target_ptr = None
    
    # Case A: Variable
    if isinstance(lhs_node, str):
        target_ptr = compiler.current_scope.resolve(lhs_node)
        if not target_ptr:
            compiler.errors.error(ast, f"Cannot assign to unknown variable '{lhs_node}'")
            return
        
        if isinstance(target_ptr, ir.GlobalVariable) and target_ptr.global_constant:
             compiler.errors.error(ast, f"Cannot assign to global constant '{lhs_node}'")
             return

    # Case B: Member Access
    elif isinstance(lhs_node, MemberAccess):
        target_ptr = compiler.compile_member_access(lhs_node, want_pointer=True)

    # Case C: Array Index
    elif isinstance(lhs_node, ArrayIndexNode):
        target_ptr = compiler.compile_array_index(lhs_node, want_pointer=True)
    # Case D: Dereference
    elif isinstance(lhs_node, DereferenceNode):
        target_ptr = compiler.compile(lhs_node.expression)
        if not isinstance(target_ptr.type, ir.PointerType):
             compiler.errors.error(ast, "Cannot assign to dereference of non-pointer.")
             return

    else:
        compiler.errors.error(ast, f"Invalid assignment target: {type(lhs_node).__name__}")
        return

    # 3. Type Checking & Coercion
    if not isinstance(target_ptr.type, ir.PointerType):
         compiler.errors.error(ast, f"Internal Error: Assignment target is not a pointer: {target_ptr.type}")
         return

    expected_type = target_ptr.type.pointee
    val_to_store = rhs_val

    # 'any' Packing (Concrete -> {i8*, i64})
    # Robust check for {i8*, i64} structure
    is_any_target = False
    if isinstance(expected_type, ir.LiteralStructType) and len(expected_type.elements) == 2:
        # Check for {i8*, i64}
        t0 = expected_type.elements[0]
        t1 = expected_type.elements[1]
        if isinstance(t0, ir.PointerType) and isinstance(t0.pointee, ir.IntType) and t0.pointee.width == 8:
            if isinstance(t1, ir.IntType) and t1.width == 64:
                is_any_target = True
            
    if is_any_target and val_to_store.type != expected_type:
        # 1. Try to get precise type from AST (e.g. StructInstantiation -> StructType)
        fin_type = compiler.get_arg_fin_type(rhs_node, val_to_store)
        
        # 2. If AST gave us a StructType, but LLVM gave us a Pointer, 
        #    it means we have a pointer to the struct.
        #    pack_any -> box_value handles this by loading the value if needed.
        
        # However, if _get_arg_fin_type failed (returned Void/Unknown), try inferring from LLVM
        if isinstance(fin_type, (VoidType.__class__, type(None))): # Check if void/none
             fin_type = compiler.infer_fin_type_from_llvm(val_to_store.type)
             # If it's a pointer to struct, unwrap it to get the StructType
             if isinstance(fin_type, PointerType) and isinstance(fin_type.pointee, StructType):
                 fin_type = fin_type.pointee

        val_to_store = compiler.pack_any(val_to_store, fin_type)
    

    # [EXISTING] Interface Packing
    is_interface_target = False
    if isinstance(expected_type, ir.LiteralStructType) and len(expected_type.elements) == 2:
        e0 = expected_type.elements[0]
        e1 = expected_type.elements[1]
        # Check for {i8*, i8*}
        if isinstance(e0, ir.PointerType) and isinstance(e1, ir.PointerType):
             is_interface_target = True

    if is_interface_target:
        is_struct = False
        if isinstance(val_to_store.type, ir.IdentifiedStructType): is_struct = True
        if isinstance(val_to_store.type, ir.PointerType) and isinstance(val_to_store.type.pointee, ir.IdentifiedStructType): is_struct = True
        
        if is_struct:
            val_to_store = compiler.pack_interface(val_to_store, val_to_store.type, expected_type)

    # [EXISTING] Auto-Boxing for Type Erasure
    if expected_type == ir.IntType(8).as_pointer() and val_to_store.type != expected_type:
        fin_type = compiler.infer_fin_type_from_llvm(val_to_store.type)
        val_to_store = compiler.box_value(val_to_store, fin_type)

    # Standard Coercion
    if val_to_store.type != expected_type:
        if isinstance(expected_type, ir.FloatType) and isinstance(val_to_store.type, ir.IntType):
            val_to_store = compiler.builder.sitofp(val_to_store, expected_type)
        elif isinstance(expected_type, ir.PointerType) and isinstance(val_to_store.type, ir.PointerType):
            val_to_store = compiler.builder.bitcast(val_to_store, expected_type)
        elif isinstance(expected_type, ir.IntType) and isinstance(val_to_store.type, ir.IntType):
            if val_to_store.type.width < expected_type.width:
                val_to_store = compiler.builder.sext(val_to_store, expected_type)
            elif val_to_store.type.width > expected_type.width:
                val_to_store = compiler.builder.trunc(val_to_store, expected_type)
        
        # Null -> Ptr
        elif isinstance(expected_type, ir.PointerType) and \
             isinstance(val_to_store, ir.Constant) and \
             val_to_store.type == ir.IntType(8).as_pointer() and \
             val_to_store.constant is None:
            val_to_store = compiler.builder.bitcast(val_to_store, expected_type)

    if val_to_store.type != expected_type:
         compiler.errors.error(ast, f"Type mismatch in assignment. Expected {expected_type}, got {val_to_store.type}")
         return

    # 4. Store
    if op == "=":
        compiler.builder.store(val_to_store, target_ptr)
    else:
        # Compound Assignment
        current_val = compiler.builder.load(target_ptr, name="compound_load")
        new_val = None
        
        if isinstance(current_val.type, ir.IntType):
            if op == "+=": new_val = compiler.builder.add(current_val, val_to_store, "add")
            elif op == "-=": new_val = compiler.builder.sub(current_val, val_to_store, "sub")
            elif op == "*=": new_val = compiler.builder.mul(current_val, val_to_store, "mul")
            elif op == "/=": 
                compiler._emit_runtime_check_zero(val_to_store, "Division by zero", node=ast)
                new_val = compiler.builder.sdiv(current_val, val_to_store, "div")
        
        elif isinstance(current_val.type, ir.FloatType):
            if op == "+=": new_val = compiler.builder.fadd(current_val, val_to_store, "fadd")
            elif op == "-=": new_val = compiler.builder.fsub(current_val, val_to_store, "fsub")
            elif op == "*=": new_val = compiler.builder.fmul(current_val, val_to_store, "fmul")
            elif op == "/=": new_val = compiler.builder.fdiv(current_val, val_to_store, "fdiv")
            
        if new_val:
            compiler.builder.store(new_val, target_ptr)
        else:
            compiler.errors.error(ast, f"Unsupported compound operator '{op}' for type {current_val.type}")
# ---------------------------------------------------------------------------
# <Method name=compile_logical args=[<Compiler>, <LogicalOperator>]>
# <Description>
# Compiles '&&', '||', '!'.
# Handles:
# 1. Operator Overloading (for structs).
# 2. Short-Circuiting (&&, ||).
# 3. Boolean Logic.
# </Description>
def compile_logical(compiler: Compiler, ast: LogicalOperator) -> ir.Value:
    left = compiler.compile(ast.left)
    op = ast.operator
    
    # --- 1. OPERATOR OVERLOADING CHECK ---
    struct_name = None
    if isinstance(left.type, ir.PointerType) and \
       isinstance(left.type.pointee, ir.IdentifiedStructType):
        struct_name = left.type.pointee.name
    elif isinstance(left.type, ir.IdentifiedStructType):
        struct_name = left.type.name
        
    if struct_name and struct_name in compiler.struct_operators and \
       op in compiler.struct_operators[struct_name]:
        
        # Compile Right side IMMEDIATELY (No Short-Circuiting for Overloads)
        # Note: This deviates from standard short-circuit behavior but is typical for operator overloading
        right = compiler.compile(ast.right) if ast.right else None
        return compiler.emit_operator_call( struct_name, op, left, right)
    # ----------------------------------
    
    # --- 2. STANDARD SHORT-CIRCUIT LOGIC ---
    # Ensure left is boolean (i1)
    if not (isinstance(left.type, ir.IntType) and left.type.width == 1):
            compiler.errors.error(ast, f"Logical '{op}' requires boolean operands or operator overloading.")
            return ir.Constant(ir.IntType(1), 0)
    
    if op == "&&":
        entry = compiler.builder.block
        true_bb = compiler.function.append_basic_block("and_true")
        cont_bb = compiler.function.append_basic_block("and_cont")

        compiler.builder.cbranch(left, true_bb, cont_bb)

        compiler.builder.position_at_end(true_bb)
        right = compiler.compile(ast.right)
        
        # Ensure right is boolean
        if not (isinstance(right.type, ir.IntType) and right.type.width == 1):
             compiler.errors.error(ast.right, "Right operand of && must be boolean.")
             
        compiler.builder.branch(cont_bb)

        compiler.builder.position_at_end(cont_bb)
        phi = compiler.builder.phi(left.type, name="andtmp")
        phi.add_incoming(ir.Constant(left.type, 0), entry)
        phi.add_incoming(right, true_bb)
        return phi

    elif op == "||":
        entry = compiler.builder.block
        false_bb = compiler.function.append_basic_block("or_false")
        cont_bb = compiler.function.append_basic_block("or_cont")
        
        compiler.builder.cbranch(left, cont_bb, false_bb)

        compiler.builder.position_at_end(false_bb)
        right = compiler.compile(ast.right)
        
        if not (isinstance(right.type, ir.IntType) and right.type.width == 1):
             compiler.errors.error(ast.right, "Right operand of || must be boolean.")

        compiler.builder.branch(cont_bb)

        compiler.builder.position_at_end(cont_bb)
        phi = compiler.builder.phi(left.type, name="ortmp")
        phi.add_incoming(ir.Constant(left.type, 1), entry)
        phi.add_incoming(right, false_bb)
        return phi

    else:
        compiler.errors.error(ast, f"Unsupported logical operator: {op}")
        return ir.Constant(ir.IntType(1), 0)

# ---------------------------------------------------------------------------
# <Method name=compile_unary args=[<Compiler>, <UnaryOperator>]>
# <Description>
# Compiles '-', '!', '~'.
# Handles Operator Overloading.
# </Description>
def compile_unary(compiler: Compiler, ast: UnaryOperator) -> ir.Value:
    val = compiler.compile(ast.operand)
    op = ast.operator
    
    # Check for Overload
    struct_name = None
    if isinstance(val.type, ir.PointerType) and isinstance(val.type.pointee, ir.IdentifiedStructType):
        struct_name = val.type.pointee.name
    elif isinstance(val.type, ir.IdentifiedStructType):
        struct_name = val.type.name

    if struct_name and struct_name in compiler.struct_operators and op in compiler.struct_operators[struct_name]:
        return compiler.emit_operator_call( struct_name, op, val, None) # Right is None
    
    # Default Behavior
    if op == "-":
        if isinstance(val.type, ir.FloatType): return compiler.builder.fneg(val, name="fnegtmp")
        else: return compiler.builder.neg(val, name="negtmp")
    elif op == "!":
        return compiler.builder.icmp_unsigned("==", val, ir.Constant(val.type, 0), name="nottmp")
    elif op == "~":
        mask = ir.Constant(val.type, -1)
        return compiler.builder.xor(val, mask, name="xortmp")
    else:
        compiler.errors.error(ast, f"Unsupported unary operator: {op}")
        return val

# ---------------------------------------------------------------------------
# <Method name=compile_comparison args=[<Compiler>, <ComparisonOperator>]>
# <Description>
# Compiles '==', '!=', '<', '>', '<=', '>='.
# Handles:
# 1. Operator Overloading.
# 2. Null Checks.
# 3. Float vs Int comparisons.
# </Description>
def compile_comparison(compiler: Compiler, ast: ComparisonOperator) -> ir.Value:
    left = compiler.compile(ast.left)
    right = compiler.compile(ast.right)
    op = ast.operator
    
    # --- OPERATOR OVERLOADING CHECK ---
    struct_name = None
    if isinstance(left.type, ir.PointerType) and \
       isinstance(left.type.pointee, ir.IdentifiedStructType):
        struct_name = left.type.pointee.name
    elif isinstance(left.type, ir.IdentifiedStructType):
        struct_name = left.type.name

    if struct_name and struct_name in compiler.struct_operators and \
       op in compiler.struct_operators[struct_name]:
        return _emit_operator_call(compiler, struct_name, op, left, right)
    
    # --- STANDARD COMPARISONS ---

    # 1. Null Checks
    is_left_null = (isinstance(left, ir.Constant) and 
                    left.type == ir.IntType(8).as_pointer() and 
                    left.constant is None)
    is_right_null = (isinstance(right, ir.Constant) and 
                     right.type == ir.IntType(8).as_pointer() and 
                     right.constant is None)

    if is_left_null or is_right_null:
        void_ptr = ir.IntType(8).as_pointer()
        
        lhs_cmp = left
        rhs_cmp = right
        
        if left.type != void_ptr: lhs_cmp = compiler.builder.bitcast(left, void_ptr)
        if right.type != void_ptr: rhs_cmp = compiler.builder.bitcast(right, void_ptr)
            
        if op == "==":
            return compiler.builder.icmp_unsigned("==", lhs_cmp, rhs_cmp, name="is_null")
        elif op == "!=":
            return compiler.builder.icmp_unsigned("!=", lhs_cmp, rhs_cmp, name="not_null")
        else:
            compiler.errors.error(ast, "Only '==' and '!=' are supported for null comparisons.")
            return ir.Constant(ir.IntType(1), 0)

    # 2. Float Comparisons
    if isinstance(left.type, ir.FloatType) or isinstance(right.type, ir.FloatType):
        lhs_cmp = left
        rhs_cmp = right
        
        if isinstance(left.type, ir.IntType):
            lhs_cmp = compiler.builder.sitofp(left, right.type, name="cvt_fp_l")
        if isinstance(right.type, ir.IntType):
            rhs_cmp = compiler.builder.sitofp(right, left.type, name="cvt_fp_r")

        mapping = {
            "==": "oeq", "!=": "one", 
            "<": "olt", "<=": "ole", 
            ">": "ogt", ">=": "oge"
        }
        if op not in mapping:
             compiler.errors.error(ast, f"Unsupported float comparison operator: {op}")
             return ir.Constant(ir.IntType(1), 0)
             
        return compiler.builder.fcmp_ordered(mapping[op], lhs_cmp, rhs_cmp, name="fcmp")

    # 3. Integer/Pointer Comparisons
    else:
        # [FIX] Integer Promotion (Width Mismatch)
        if isinstance(left.type, ir.IntType) and isinstance(right.type, ir.IntType):
            if left.type.width != right.type.width:
                if left.type.width > right.type.width:
                    right = compiler.builder.sext(right, left.type, name="sext_r")
                else:
                    left = compiler.builder.sext(left, right.type, name="sext_l")

        # [FIX] Pointer Mismatch
        if isinstance(left.type, ir.PointerType) and isinstance(right.type, ir.PointerType):
            if left.type != right.type:
                # Cast right to left's type for comparison
                right = compiler.builder.bitcast(right, left.type, name="ptr_bitcast")

        # Handle Pointer comparison (treat as unsigned int)
        if isinstance(left.type, ir.PointerType):
            return compiler.builder.icmp_unsigned(op, left, right, name="ptr_cmp")
        
        # Handle Integer comparison (signed)
        return compiler.builder.icmp_signed(op, left, right, name="icmp")
# ---------------------------------------------------------------------------
# <Method name=compile_additive args=[<Compiler>, <AdditiveOperator>]>
# <Description>
# Compiles '+' and '-'.
# Handles Operator Overloading.
# </Description>
def compile_additive(compiler: Compiler, ast: AdditiveOperator) -> ir.Value:
    left = compiler.compile(ast.left)
    right = compiler.compile(ast.right)
    op = ast.operator
    
    # Check for Overload
    struct_name = None
    if isinstance(left.type, ir.PointerType) and isinstance(left.type.pointee, ir.IdentifiedStructType):
        struct_name = left.type.pointee.name
    elif isinstance(left.type, ir.IdentifiedStructType):
        struct_name = left.type.name

    if struct_name and struct_name in compiler.struct_operators and op in compiler.struct_operators[struct_name]:
        return compiler.emit_operator_call( struct_name, op, left, right)
    
    # Default Behavior
    if left.type != right.type:
        # Basic coercion?
        pass # Rely on strict typing for now or add coercion here
        
    if isinstance(left.type, ir.FloatType):
        if op == "+": return compiler.builder.fadd(left, right, name="faddtmp")
        else: return compiler.builder.fsub(left, right, name="fsubtmp")
    else:
        if op == "+": return compiler.builder.add(left, right, name="addtmp")
        else: return compiler.builder.sub(left, right, name="subtmp")

# ---------------------------------------------------------------------------
# <Method name=compile_multiplicative args=[<Compiler>, <MultiplicativeOperator>]>
# <Description>
# Compiles '*', '/', '%'.
# Handles Operator Overloading and Division by Zero checks.
# </Description>
def compile_multiplicative(compiler: Compiler, ast: MultiplicativeOperator) -> ir.Value:
    left = compiler.compile(ast.left)
    right = compiler.compile(ast.right)
    op = ast.operator

    # Check for Overload
    struct_name = None
    if isinstance(left.type, ir.PointerType) and isinstance(left.type.pointee, ir.IdentifiedStructType):
        struct_name = left.type.pointee.name
    elif isinstance(left.type, ir.IdentifiedStructType):
        struct_name = left.type.name

    if struct_name and struct_name in compiler.struct_operators and op in compiler.struct_operators[struct_name]:
        return compiler.emit_operator_call( struct_name, op, left, right)

    # Default Behavior
    if isinstance(left.type, ir.FloatType):
        if op == "*": return compiler.builder.fmul(left, right, name="fmultmp")
        elif op == "/": return compiler.builder.fdiv(left, right, name="fdivtmp")
        else: 
            compiler.errors.error(ast, "Floating-point remainder not supported")
            return ir.Constant(ir.FloatType(), 0.0)
    else:
        if op == "*": return compiler.builder.mul(left, right, name="multmp")
        elif op == "/":
            compiler._emit_runtime_check_zero(right, "Division by zero", node=ast)
            return compiler.builder.sdiv(left, right, name="sdivtmp")
        elif op == "%":
            compiler._emit_runtime_check_zero(right, "Modulo by zero", node=ast)
            return compiler.builder.srem(left, right, name="sremtmp")
            
    return ir.Constant(ir.IntType(32), 0)

# ---------------------------------------------------------------------------
# <Method name=compile_postfix args=[<Compiler>, <PostfixOperator>]>
# <Description>
# Compiles 'x++' and 'x--'.
# Handles L-Value resolution for Variables, Fields, and Array Elements.
# </Description>
def compile_postfix(compiler: Compiler, ast: PostfixOperator) -> ir.Value:
    lvalue_ptr = None
    
    # --- Case 1: Simple Identifier (x++) ---
    if isinstance(ast.operand, str):
        var_name = ast.operand
        lvalue_ptr = compiler.current_scope.resolve(var_name)
        if not lvalue_ptr:
            compiler.errors.error(ast, f"Undeclared variable '{var_name}' for postfix operator.")
            return ir.Constant(ir.IntType(32), 0)
    
    # --- Case 2: Member Access (obj.field++) ---
    elif isinstance(ast.operand, MemberAccess):
        # Request POINTER (L-Value)
        lvalue_ptr = compiler.compile_member_access( ast.operand, want_pointer=True)

    # --- Case 3: Array Index (arr[i]++) ---
    elif isinstance(ast.operand, ArrayIndexNode):
        # Request POINTER (L-Value)
        lvalue_ptr = compiler.compile_array_index( ast.operand, want_pointer=True)
    
    else:
        compiler.errors.error(ast, f"Postfix operator not supported for {type(ast.operand)}")
        return ir.Constant(ir.IntType(32), 0)

    # --- Perform Operation ---
    if not lvalue_ptr:
         compiler.errors.error(ast, "Could not resolve L-Value for postfix operator.")
         return ir.Constant(ir.IntType(32), 0)

    # 1. Load Old Value
    old_val = compiler.builder.load(lvalue_ptr, name="postfix_old")
    
    # 2. Calculate New Value
    new_val = None
    
    if isinstance(old_val.type, ir.IntType):
        one = ir.Constant(old_val.type, 1)
        if ast.operator == "++": new_val = compiler.builder.add(old_val, one, name="inc")
        elif ast.operator == "--": new_val = compiler.builder.sub(old_val, one, name="dec")
    elif isinstance(old_val.type, ir.FloatType):
        one = ir.Constant(old_val.type, 1.0)
        if ast.operator == "++": new_val = compiler.builder.fadd(old_val, one, name="finc")
        elif ast.operator == "--": new_val = compiler.builder.fsub(old_val, one, name="fdec")
    else:
        compiler.errors.error(ast, f"Postfix operator only works on int/float, got {old_val.type}")
        return old_val
    
    # 3. Store New Value
    compiler.builder.store(new_val, lvalue_ptr)
    
    # 4. Return Old Value
    return old_val