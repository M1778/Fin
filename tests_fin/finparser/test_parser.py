from src.utils.helpers import parse_code
from pathlib import Path
import os

def smart_search(dir:str = "tests/fintests/"):
    for test_file in os.listdir(dir):
        if not Path(dir).joinpath(test_file).is_file():
            smart_search(Path(dir).joinpath(test_file))
        else:
            if Path(dir).joinpath(test_file).suffix != "fin":
                return
            ast = parse_code(open(str(Path(dir).joinpath(test_file).absolute()), 'r').read(),filename=Path(dir).joinpath(test_file))
            assert len(ast.statements) <= 0

def test_lexer():
    smart_search()