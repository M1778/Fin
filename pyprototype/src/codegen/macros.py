from .essentials import *

# <Method name=compile_special_declaration args=[<Compiler>, <SpecialDeclaration>]>
# <Description>
# Compiles a special function definition (starting with @).
# Applies attributes like #[llvm_name="..."] and #[set_linkage="..."].
# </Description>
def compile_special_declaration(compiler: Compiler, ast: SpecialDeclaration):
    func_name = ast.name
    
    # 1. Resolve Name using Attributes
    default_mangled = compiler.get_mangled_name(func_name)
    llvm_name = compiler.attributes_lib.resolve_llvm_name(ast, default_mangled)

    # 2. Setup Function Signature
    compiler.enter_scope()
    
    # Convert Params
    llvm_param_types = []
    for p_ast in ast.params:
        # Special declarations might use raw types or standard types
        # We use standard conversion here
        llvm_param_types.append(compiler.convert_type(p_ast.var_type))

    # Return type is usually void (noret) for specials like panic, but let's support others
    # SpecialDeclaration AST might not have return_type field in your current node definition?
    # If not, assume Void or infer. 
    # *Assuming you added return_type to SpecialDeclaration in nodes.py, otherwise default to Void*
    llvm_ret_type = ir.VoidType() 
    if hasattr(ast, 'return_type') and ast.return_type:
        llvm_ret_type = compiler.convert_type(ast.return_type)

    llvm_func_type = ir.FunctionType(llvm_ret_type, llvm_param_types)
    
    # 3. Create Function
    try:
        llvm_function = compiler.module.get_global(llvm_name)
    except KeyError:
        llvm_function = ir.Function(compiler.module, llvm_func_type, name=llvm_name)
    
    # 3.5. Apply Post-Creation Attributes (Linkage, etc.)
    compiler.attributes_lib.apply(llvm_function, ast)
    
    # 4. Compile Body
    compiler.function = llvm_function
    entry_block = llvm_function.append_basic_block(name="entry")
    compiler.builder = ir.IRBuilder(entry_block)

    # Bind Args
    for i, param_ast in enumerate(ast.params):
        arg = llvm_function.args[i]
        arg.name = param_ast.identifier
        
        # Allocate stack space
        arg_ptr = compiler.builder.alloca(arg.type, name=f"{arg.name}_ptr")
        compiler.builder.store(arg, arg_ptr)
        
        # Register in scope
        fin_type = compiler.ast_to_fin_type(param_ast.var_type)
        compiler.current_scope.define(param_ast.identifier, arg_ptr, fin_type)

    # Compile Statements
    for stmt in ast.body:
        compiler.compile(stmt)

    # Implicit Return
    if not compiler.builder.block.is_terminated:
        if isinstance(llvm_ret_type, ir.VoidType):
            compiler.builder.ret_void()

    compiler.exit_scope()
    compiler.function = None
    compiler.builder = None