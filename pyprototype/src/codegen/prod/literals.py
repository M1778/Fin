from .essentials import *
import codecs

# ---------------------------------------------------------------------------
# <Method name=compile_literal args=[<Compiler>, <Literal>]>
# <Description>
# Compiles a Literal AST node into an LLVM Constant.
# Handles:
# 1. Null (i8* null)
# 2. Strings (Interned Global i8*)
# 3. Integers, Floats, Booleans (Inferred types)
# </Description>
def compile_literal(compiler: Compiler, ast: Literal) -> ir.Value:
    py_value = ast.value

    # 1. Handle Null
    if py_value is None:
        return ir.Constant(ir.IntType(8).as_pointer(), None)

    # 2. Handle Strings
    if isinstance(py_value, str):
        try:
            # Handle escape sequences like \n, \t, \x00
            unescaped_str = codecs.decode(py_value, "unicode_escape")
            return compiler.create_global_string(unescaped_str)
        except Exception as e:
            compiler.errors.error(ast, f"Invalid string literal: {e}")
            # Return empty string to continue compilation
            return compiler.create_global_string("")

    # 3. Handle Primitives (Int, Float, Bool)
    elif isinstance(py_value, (int, float, bool)):
        # Infer LLVM type (i32, i64, float, i1, etc.)
        try:
            llvm_type = compiler.guess_type(py_value)
        except Exception as e:
            compiler.errors.error(ast, str(e))
            return ir.Constant(ir.IntType(32), 0)

        try:
            # Create Constant
            # Note: For bools, py_value is True/False. LLVM expects 1/0 for i1.
            val = py_value
            if isinstance(py_value, bool):
                val = 1 if py_value else 0
                
            return ir.Constant(llvm_type, val)
            
        except OverflowError:
            compiler.errors.error(ast, f"Literal '{py_value}' is too large for type {llvm_type}.")
            return ir.Constant(llvm_type, 0)
        except Exception as e:
            compiler.errors.error(ast, f"Failed to create constant for '{py_value}': {e}")
            return ir.Constant(llvm_type, 0)

    else:
        compiler.errors.error(ast, f"Unsupported literal value type: {type(py_value)}")
        return ir.Constant(ir.IntType(32), 0)