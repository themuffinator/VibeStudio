"""Read-only progs.dat (version 6) statement lister for resolving 1996 Quake profile citations.

The shipped 1996 id1 progs.dat has no matching QuakeC source on this machine, so data/profiles cites
its facts as ``progs1996:<function>:<first>[-<last>]|<token>``. This module renders each statement in
the same text form the Phase-1 facts task used (out/benchmark-draft/phase1-evidence/facts_digest.md),
so a citation token can be checked against the listing. It only reads the file; it never writes,
copies or stores any bytes of it.

Rendering: an operand that has a global definition prints its name, with ``(=value)`` for float
globals; immediates print as ``'x y z'`` vectors, strings as ``"..."?`` when the operand type is
not known to be a string. Named constants and immediates with equal values share storage in qcc
output, so an immediate can print under a constant's name (for example ``SVC_INTERMISSION(=30)``);
the ``(=value)`` part is the value that matters.

usage: python -B tools/profiles/progs_disasm.py <progs.dat> <function> [<function> ...]
       python -B tools/profiles/progs_disasm.py <progs.dat> --list | --header | --global <name> ...
"""
from __future__ import annotations

import struct
import sys
from dataclasses import dataclass, field

OPS = ["DONE", "MUL_F", "MUL_V", "MUL_FV", "MUL_VF", "DIV_F", "ADD_F", "ADD_V", "SUB_F", "SUB_V",
       "EQ_F", "EQ_V", "EQ_S", "EQ_E", "EQ_FNC", "NE_F", "NE_V", "NE_S", "NE_E", "NE_FNC",
       "LE", "GE", "LT", "GT", "LOAD_F", "LOAD_V", "LOAD_S", "LOAD_ENT", "LOAD_FLD", "LOAD_FNC",
       "ADDRESS", "STORE_F", "STORE_V", "STORE_S", "STORE_ENT", "STORE_FLD", "STORE_FNC",
       "STOREP_F", "STOREP_V", "STOREP_S", "STOREP_ENT", "STOREP_FLD", "STOREP_FNC",
       "RETURN", "NOT_F", "NOT_V", "NOT_S", "NOT_ENT", "NOT_FNC", "IF", "IFNOT",
       "CALL0", "CALL1", "CALL2", "CALL3", "CALL4", "CALL5", "CALL6", "CALL7", "CALL8",
       "STATE", "GOTO", "AND", "OR", "BITAND", "BITOR"]


@dataclass
class Function:
    name: str
    source_file: str
    first_statement: int
    statements: dict[int, str] = field(default_factory=dict)


@dataclass
class Progs:
    version: int
    crc: int
    statement_count: int
    functions: dict[str, Function]
    globals: dict[str, str] = field(default_factory=dict)

    def header_text(self) -> str:
        return (f"version {self.version} crc {self.crc} statements {self.statement_count} "
                f"functions {len(self.functions)}")


def _kinds() -> dict[int, tuple[str, str, str]]:
    kinds: dict[int, tuple[str, str, str]] = {}
    for index, name in enumerate(OPS):
        a = b = c = "f"
        if name.endswith("_V") or name == "MUL_FV":
            a = b = c = "v"
        if name == "MUL_FV":
            a, b, c = "f", "v", "v"
        if name == "MUL_VF":
            a, b, c = "v", "f", "v"
        if name == "MUL_V":
            a, b, c = "v", "v", "f"
        if name in ("EQ_S", "NE_S", "NOT_S", "STORE_S", "STOREP_S"):
            a = "s"
            if name in ("EQ_S", "NE_S", "NOT_S"):
                b = "s"
        if name.startswith("LOAD_") or name == "ADDRESS":
            a, b = "e", "fld"
        if name in ("EQ_V", "NE_V"):
            c = "f"
        if name in ("STORE_FNC", "STOREP_FNC"):
            a = "fn"
        kinds[index] = (a, b, c)
    return kinds


def load(path: str) -> Progs:
    with open(path, "rb") as handle:
        data = handle.read()
    (version, crc, ofs_st, n_st, ofs_gd, n_gd, ofs_fd, n_fd, ofs_fn, n_fn,
     ofs_str, n_str, ofs_gl, n_gl, _entfields) = struct.unpack_from("<15i", data, 0)
    if version != 6:
        raise ValueError(f"unsupported progs version {version}")
    strings = data[ofs_str:ofs_str + n_str]

    def text(offset: int) -> str:
        if offset < 0 or offset >= len(strings):
            return f"<badstr {offset}>"
        end = strings.find(b"\0", offset)
        return strings[offset:end].decode("latin-1")

    gdefs: dict[int, tuple[int, str]] = {}
    for k in range(n_gd):
        kind, offset, name = struct.unpack_from("<HHi", data, ofs_gd + 8 * k)
        gdefs.setdefault(offset, (kind & 0x7FFF, text(name)))
    fdefs: dict[int, str] = {}
    for k in range(n_fd):
        _kind, offset, name = struct.unpack_from("<HHi", data, ofs_fd + 8 * k)
        fdefs.setdefault(offset, text(name))
    gl_f = struct.unpack_from(f"<{n_gl}f", data, ofs_gl)
    gl_i = struct.unpack_from(f"<{n_gl}i", data, ofs_gl)
    raw_functions = []
    for k in range(n_fn):
        first, _parm_start, _locals, _profile, name, source, _parms = struct.unpack_from("<7i", data, ofs_fn + 36 * k)
        raw_functions.append((first, text(name), text(source)))
    starts = {first for first, _name, _source in raw_functions}

    def operand(offset: int, kind: str) -> str:
        definition = gdefs.get(offset)
        name = definition[1] if definition else None
        if name and name not in ("IMMEDIATE", ""):
            if kind == "f" and definition[0] == 2:
                return f"{name}(={gl_f[offset]:g})"
            return name
        if kind == "v" and 0 < gl_i[offset] < len(strings) and abs(gl_f[offset]) < 1e-30 and offset >= 28:
            return '"%s"?' % text(gl_i[offset]).replace("\n", "\\n")
        if kind == "s":
            return '"%s"' % text(gl_i[offset]).replace("\n", "\\n")
        if kind == "v":
            return "'%g %g %g'" % gl_f[offset:offset + 3]
        if kind == "fn":
            index = gl_i[offset]
            return "fn#%d(%s)" % (index, raw_functions[index][1] if 0 <= index < len(raw_functions) else "?")
        if kind == "fld":
            return f"fld({fdefs.get(gl_i[offset], gl_i[offset])})"
        return f"{gl_f[offset]:g}"

    kinds = _kinds()
    functions: dict[str, Function] = {}
    for first, name, source in raw_functions:
        function = Function(name, source, first)
        if first > 0:
            index = first
            while index < n_st:
                op, a, b, c = struct.unpack_from("<Hhhh", data, ofs_st + 8 * index)
                if index != first and index in starts:
                    break
                op_name = OPS[op] if op < len(OPS) else f"op{op}"
                ka, kb, kc = kinds.get(op, ("f", "f", "f"))
                if op_name in ("IF", "IFNOT"):
                    line = f"{op_name} {operand(a, 'f')} goto {index + b}"
                elif op_name == "GOTO":
                    line = f"GOTO {index + a}"
                elif op_name.startswith("CALL"):
                    line = f"{op_name} {operand(a, 'fn')}"
                elif op_name.startswith("STORE") and not op_name.startswith("STOREP"):
                    line = f"{op_name} {operand(a, ka)} -> {operand(b, ka)}"
                elif op_name.startswith("STOREP"):
                    line = f"{op_name} {operand(a, ka)} -> *{operand(b, 'f')}"
                elif op_name in ("RETURN", "DONE"):
                    line = op_name
                elif op_name.startswith("NOT"):
                    line = f"{op_name} {operand(a, ka)} -> {operand(c, 'f')}"
                elif op_name == "STATE":
                    line = f"STATE {operand(a, 'f')} {operand(b, 'fn')}"
                else:
                    line = f"{op_name} {operand(a, ka)}, {operand(b, kb)} -> {operand(c, kc)}"
                function.statements[index] = line
                if op_name == "DONE":
                    break
                index += 1
        functions.setdefault(name, function)
    named_globals: dict[str, str] = {}
    for k in range(n_gd):
        kind, offset, name_offset = struct.unpack_from("<HHi", data, ofs_gd + 8 * k)
        name = text(name_offset)
        kind &= 0x7FFF
        if not name or name == "IMMEDIATE" or name in named_globals:
            continue
        if kind == 2:
            named_globals[name] = f"{gl_f[offset]:g}"
        elif kind == 3:
            named_globals[name] = "'%g %g %g'" % gl_f[offset:offset + 3]
        elif kind == 1 and 0 <= gl_i[offset] < len(strings):
            named_globals[name] = '"%s"' % text(gl_i[offset]).replace("\n", "\\n")
    return Progs(version, crc, n_st, functions, named_globals)


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print(__doc__)
        return 2
    progs = load(argv[1])
    if argv[2] == "--header":
        print(progs.header_text())
        return 0
    if argv[2] == "--global":
        for name in argv[3:]:
            print(name, progs.globals.get(name, "<not found>"))
        return 0
    if argv[2] == "--list":
        for function in progs.functions.values():
            print(function.name, function.source_file, function.first_statement)
        return 0
    status = 0
    for name in argv[2:]:
        function = progs.functions.get(name)
        if function is None:
            print("### not found:", name)
            status = 1
            continue
        print(f"### {function.name}  ({function.source_file}) first_statement={function.first_statement}")
        for index, line in function.statements.items():
            print(f"  {index:5d}  {line}")
    return status


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
