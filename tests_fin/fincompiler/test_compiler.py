from pathlib import Path
import os

def smart_search(dir:str = "tests/fintests/"):
    for test_file in os.listdir(dir):
        if not Path(dir).joinpath(test_file).is_file():
            smart_search(Path(dir).joinpath(test_file))
        else:
            file = str(Path(dir).joinpath(test_file).absolute())
            os.system(f"uv run python tests/run_jit.py {file} -r")

def test_compiler():
    smart_search()