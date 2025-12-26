from src.lexer import lexer
from src.parser import parser

from pathlib import Path
import argparse
import os

argparser = argparse.ArgumentParser()
argparser.add_argument("file")

def runOn(filepath:str):
    if not Path(filepath).is_file(): return
    ast = parser.parse(open(filepath, 'r').read(),lexer=lexer)
    print(ast)

if __name__ == "__main__":
    lexer.parser_instance = parser
    args = argparser.parse_args()
    if args.file:
        runOn(args.file)
