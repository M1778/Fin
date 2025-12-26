import argparse
import os
import sys
from pathlib import Path

try:
    from src.codegen.fin import FinCompiler
    from src.utils.helpers import parse_code
    from src.utils.module_loader import ModuleLoader
except:
    sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '..')))
    from src.codegen.fin import FinCompiler
    from src.utils.helpers import parse_code
    from src.utils.module_loader import ModuleLoader
    from src.ast2.nodes import *

    
if __name__ == "__main__":
    prs = argparse.ArgumentParser(description="Fin Compiler")
    prs.add_argument("input", type=str, help="Input Fin file")

    prs.add_argument(
        "-o", "--output", type=str, help="Name of the output executable file"
    )
    prs.add_argument(
        "--obj", action="store_true", help="Generate object code only (output.o)"
    )
    prs.add_argument(
        "-O", "--optimization-level", help="LLVM Optimization level", type=int
    )
    prs.add_argument(
        "-C", "--codemodel", help="LLVM CodeModel (default, small,...)", type=str
    )
    prs.add_argument(
        "--keep-obj",
        action="store_true",
        help="Keep intermediate object file when generating executable",
    )

    prs.add_argument(
        "-r", "--run", action="store_true", help="Run the program using JIT"
    )
    prs.add_argument(
        "--ir", "-i", action="store_true", help="Generate and print LLVM IR code"
    )
    prs.add_argument(
        "-e", "--experimental",
        action="store_true",
        help="Experimental Interpreter mode"
    )

    args = prs.parse_args()

    input_file_path = os.path.abspath(args.input)
    if not os.path.exists(input_file_path):
        print(f"Error: File '{args.input}' not found.")
        exit(1)

    # 2. Read Code
    with open(input_file_path, "r") as f:
        code = f.read()

    print("Parsing code...")
    # Pass filename for error reporting
    ast = parse_code(code, filename=input_file_path)
    if ast is None:
        print("Parsing failed, AST is None.")
        exit(1)
    print("Parsing successful.")
    print(f"AST: {ast}")

    # 3. Initialize ModuleLoader with the ENTRYPOINT FILE
    module_loader = ModuleLoader(entrypoint_file=input_file_path)

    # 4. Initialize Compiler with the loader and path
    compiler = FinCompiler(
        open(input_file_path, "r").read(),
        input_file_path,
        opt=args.optimization_level, 
        codemodel=args.codemodel, 
        is_jit=args.run, 
        module_loader=module_loader,
        initial_file_path=input_file_path
    )
    compiler.load_library(str(Path(__file__).parent.parent.joinpath("stdlib/").joinpath("builtins.fin")))
    compiler.compile(ast)
        

    if args.ir:
        print("--- Generated LLVM IR ---")
        print(str(compiler.module))
        print("-------------------------")

    if args.run:
        try:
            print("Running with JIT...")
            compiler.runwithjit("main")
        except Exception as e:
            print(f"Error during JIT execution: {e}")

            exit(1)

    compiler.shutdown()
    print("Compilation process finished.")
