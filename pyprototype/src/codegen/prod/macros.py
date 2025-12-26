from .essentials import *
from copy import deepcopy

# ---------------------------------------------------------------------------
# <Method name=compile_macro_declaration args=[<Compiler>, <MacroDeclaration>]>
# <Description>
# Registers a macro definition. Supports Overloading.
# </Description>
def compile_macro_declaration(compiler: Compiler, ast: MacroDeclaration):
    if ast.name not in compiler.macros:
        compiler.macros[ast.name] = []
    
    # Check for duplicate signature (same arg count)
    existing = compiler.macros[ast.name]
    for params, _ in existing:
        if len(params) == len(ast.params):
            compiler.errors.error(ast, f"Macro '{ast.name}' with {len(ast.params)} arguments already declared.")
            return

    # Store (params, body)
    compiler.macros[ast.name].append((ast.params, ast.body))

# ---------------------------------------------------------------------------
# <Method name=compile_macro_call args=[<Compiler>, <MacroCall>]>
# <Description>
# Expands a macro call.
# 1. Overload Resolution: Finds definition matching arg count.
# 2. Hygiene: Enters a new scope.
# 3. Argument Binding:
#    - Mode 'expr': Compiles arg to value, creates temp variable (Let).
#    - Mode 'ast': Direct substitution.
# 4. Body Compilation.
# </Description>
def compile_macro_call(compiler: Compiler, ast: MacroCall) -> Optional[ir.Value]:
    if ast.name not in compiler.macros:
        compiler.errors.error(ast, f"Macro '{ast.name}' not declared.")
        return None

    # 1. Overload Resolution
    definitions = compiler.macros[ast.name]
    target_def = None
    
    for params, body in definitions:
        # Check if last param is vararg
        if params and params[-1].is_vararg:
            # Vararg match: provided args must be >= fixed params
            if len(ast.args) >= len(params) - 1:
                target_def = (params, body)
                break
        else:
            # Exact match
            if len(params) == len(ast.args):
                target_def = (params, body)
                break
    
    if not target_def:
        compiler.errors.error(ast, f"No macro '{ast.name}' accepts {len(ast.args)} arguments.")
        return None

    params, body_stmts = target_def
    
    # 2. Hygiene: Enter Scope
    compiler.enter_scope()

    # 3. Argument Binding & Substitution Map
    ast_mapping = {} # For 'ast' mode (Direct substitution)
    
    # We need to inject 'let' statements for 'expr' mode args
    # But we can't inject them into the AST easily without modifying the body list.
    # Instead, we compile the 'let' statements immediately into the current block.
    
    for i, param in enumerate(params):
        if param.is_vararg:
            # Collect all remaining args
            remaining_args = ast.args[i:]
            
            # For macros, we usually want to substitute them as a list of nodes.
            # The substitution logic needs to handle expanding this list.
            ast_mapping[param.name] = remaining_args
            break # Vararg consumes everything
        else:
            arg_node = ast.args[i]
            
            if param.mode == "ast":
                # Direct Substitution (Meta-Variable)
                # Used for Types, Blocks, or raw code injection
                ast_mapping[param.name] = arg_node
                
            elif param.mode == "expr":
                # Safe Evaluation (Call-by-Value)
                # 1. Compile the argument expression NOW
                # This ensures it runs once and in the caller's context (mostly)
                val = compiler.compile(arg_node)
                
                # 2. Create a temporary variable in the macro scope
                # We need a unique name to avoid collisions if the user used the same name
                temp_name = f"__macro_arg_{param.name}_{compiler.block_count}"
                
                # 3. Create the variable
                # We use create_variable_mut directly
                # We infer type from the value
                fin_type = compiler.infer_fin_type_from_llvm(val.type)
                var_ptr = compiler.create_variable_mut(temp_name, val.type, val)
                
                # 4. Map the param name to a Variable Reference Node
                # Whenever 'param.name' is used in the body, we replace it with 'temp_name'
                # We create a dummy Identifier/String that resolves to this var
                ast_mapping[param.name] = temp_name
                
            else:
                compiler.errors.error(ast, f"Unknown macro parameter mode '{param.mode}'. Use 'expr' or 'ast'.")

    # 4. Compile Body
    result_val = None
    
    for stmt in body_stmts:
        # Clone statement
        stmt_copy = deepcopy(stmt)
        
        # Substitute
        # If mapping value is a string (temp var name), we replace identifiers.
        # If mapping value is a Node (ast mode), we replace the node.
        expanded_stmt = _substitute_macro_params(stmt_copy, ast_mapping)
        
        # Handle @return
        if isinstance(expanded_stmt, MacroReturnStatement):
            if expanded_stmt.value:
                result_val = compiler.compile(expanded_stmt.value)
            break
            
        # Handle return (Function exit)
        elif isinstance(expanded_stmt, ReturnStatement):
            compiler.compile(expanded_stmt)
            break
            
        else:
            compiler.compile(expanded_stmt)

    # 5. Hygiene: Exit Scope
    compiler.exit_scope()

    return result_val

# ---------------------------------------------------------------------------
# <Method name=_substitute_macro_params args=[<Node>, <Dict>]>
# <Description>
# Performs AST substitution.
# Handles replacing identifiers with Temp Variable Names (expr mode)
# or replacing Nodes with Argument Nodes (ast mode).
# </Description>
def _substitute_macro_params(node: Node, mapping: Dict[str, Union[str, Node]]) -> Node:
    # If node is a string (Identifier) and matches a param
    if isinstance(node, str) and node in mapping:
        replacement = mapping[node]
        if isinstance(replacement, str):
            return replacement # Replace name with temp_name
        elif isinstance(replacement, Node):
            return deepcopy(replacement) # Replace with AST node

    if hasattr(node, '__dict__'):
        for key, value in node.__dict__.items():
            # 1. Value is String (Identifier)
            if isinstance(value, str) and value in mapping:
                replacement = mapping[value]
                if isinstance(replacement, str):
                    setattr(node, key, replacement)
                elif isinstance(replacement, Node):
                    # We are replacing a string field with a Node.
                    # This is valid for Expression fields (e.g. BinaryOp.left),
                    # but invalid for Declaration fields (e.g. VariableDeclaration.identifier).
                    # We assume macros are mostly used in Expressions.
                    setattr(node, key, deepcopy(replacement))

            # 2. Value is Node
            elif isinstance(value, Node):
                # Special check: If the node itself should be replaced entirely
                # (e.g. replacing a Literal node with a complex Expression)
                # This is hard to do via recursion on the child.
                # But since we recurse, we can't easily replace 'value' in 'node'.
                # We rely on the parent loop (this loop) to set the attribute.
                
                # Check if this node is a wrapper for an identifier we want to replace?
                # No, we just recurse.
                _substitute_macro_params(value, mapping)

            # 3. Value is List
            elif isinstance(value, list):
                new_list = []
                for item in value:
                    # [FIX] Check for Spread Operator: args...
                    # If item is PostfixOperator("...", operand) and operand matches a vararg param
                    if isinstance(item, PostfixOperator) and item.operator == "...":
                        # Operand might be a string "args"
                        op_name = item.operand
                        if isinstance(op_name, str) and op_name in mapping:
                            mapped_val = mapping[op_name]
                            if isinstance(mapped_val, list):
                                # EXPAND THE LIST!
                                for expanded_node in mapped_val:
                                    new_list.append(deepcopy(expanded_node))
                                continue # Skip appending the 'item' itself
                    
                    # Standard replacement
                    if isinstance(item, str) and item in mapping:
                        rep = mapping[item]
                        if isinstance(rep, list):
                            # Direct list replacement? (Rare)
                            new_list.extend([deepcopy(x) for x in rep])
                        else:
                            new_list.append(deepcopy(rep) if isinstance(rep, Node) else rep)
                    elif isinstance(item, Node):
                        _substitute_macro_params(item, mapping)
                        new_list.append(item)
                    else:
                        new_list.append(item)
                setattr(node, key, new_list)

    return node