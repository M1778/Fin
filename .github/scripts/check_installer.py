#!/usr/bin/env python3
"""Assert that tools/packaging/finc.iss calls only real Inno Setup registry
functions.

The release matrix compiles that script with ISCC on a Windows runner, the only
place it can be compiled at all, so an invented identifier in its [Code] section
costs a full Windows rebuild: the run that first reached [Code] died on
`RegWriteExpandszValue`, a name that has never existed (the real one is
`RegWriteExpandStringValue`).  Checking the script here turns that typo into a
Linux failure, before the matrix is spent.

Usage: check_installer.py <path-to-.iss>
"""

import re
import sys

# The registry support functions Inno Setup 6 registers, exactly as spelled in
# Projects/Src/Shared.ScriptFunc.pas (jrsoftware/issrc).  Pascal Script is
# case-insensitive.  A name missing from this set is not provided by Inno Setup;
# do not add one to silence this check -- that is the bug it exists to catch.
REGISTRY_FUNCTIONS = {
    "regdeletekeyifempty", "regdeletekeyincludingsubkeys", "regdeletevalue",
    "reggetsubkeynames", "reggetvaluenames", "regkeyexists",
    "regquerybinaryvalue", "regquerydwordvalue", "regquerymultistringvalue",
    "regquerystringvalue", "regvalueexists", "regwritebinaryvalue",
    "regwritedwordvalue", "regwriteexpandstringvalue", "regwritemultistringvalue",
    "regwritestringvalue",
}


def code_section(text):
    """The [Code] section body, with { } and // comments removed."""
    lines = text.splitlines()
    try:
        start = next(i for i, line in enumerate(lines)
                     if line.strip().lower() == "[code]")
    except StopIteration:
        return ""
    body, in_brace = [], False
    for line in lines[start + 1:]:
        if line.lstrip().startswith("["):
            break
        out = []
        for char in line:
            if in_brace:
                if char == "}":
                    in_brace = False
            elif char == "{":
                in_brace = True
            else:
                out.append(char)
        body.append("".join(out).split("//", 1)[0])
    return "\n".join(body)


def main(argv):
    if len(argv) != 2:
        sys.exit(__doc__.strip().splitlines()[-1])
    path = argv[1]
    with open(path, encoding="utf-8") as handle:
        body = code_section(handle.read())
    if not body.strip():
        sys.exit("::error::%s has no [Code] section to check" % path)

    called = {name.lower() for name in re.findall(r"([A-Za-z_]\w*)\s*\(", body)}
    unknown = sorted(name for name in called
                     if name.startswith("reg") and name not in REGISTRY_FUNCTIONS)
    if unknown:
        for name in unknown:
            print("::error::%s calls %s(), which Inno Setup does not provide"
                  % (path, name))
        sys.exit(1)
    print("%s: every Reg* call is an Inno Setup registry function" % path)


if __name__ == "__main__":
    main(sys.argv)
