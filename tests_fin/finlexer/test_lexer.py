from src.lexer import lexer
from src.parser import parser
from pathlib import Path
import os

def smart_search(lexer,dir:str = "tests/fintests/"):
    for test_file in os.listdir(dir):
        if not Path(dir).joinpath(test_file).is_file():
            smart_search(lexer, Path(dir).joinpath(test_file))
        else:
            lexer.input(open(str(Path(dir).joinpath(test_file).absolute()), 'r').read())


def test_lexer():
    assert lexer != None
    assert parser != None
    
    lexer.parser_instance = parser
    smart_search(lexer)