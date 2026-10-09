#!/usr/bin/env python3
"""Generates core/opcodes.gen.cpp from tools/opcodes.json.

The JSON table describes all 512 SM83 opcodes (256 plain, 256 behind the 0xCB
prefix): mnemonic, operands, length, duration and which flags change. This
script turns each entry into one `case` of a C++ switch that calls the small
hand-written helpers in core/cpu.cpp.

Timing is not taken from the table. It falls out of the memory accesses the
generated code makes (see ADR-005), and the unit tests then compare that
against the table's numbers.

Usage:
    python3 tools/gen_opcodes.py           write core/opcodes.gen.cpp
    python3 tools/gen_opcodes.py --check   fail if the file on disk is stale

Source of opcodes.json: https://github.com/gbdev/gb-opcodes (CC0-1.0).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TABLE = ROOT / "tools" / "opcodes.json"
OUTPUT = ROOT / "core" / "opcodes.gen.cpp"

R8 = {"A", "B", "C", "D", "E", "H", "L"}
R16 = {"BC", "DE", "HL", "SP", "AF"}
CONDITIONS = {
    "NZ": "!reg_.flag(Flag::Z)",
    "Z": "reg_.flag(Flag::Z)",
    "NC": "!reg_.flag(Flag::C)",
    "C": "reg_.flag(Flag::C)",
}
ALU_A = {"ADD": "alu_add", "ADC": "alu_adc", "SUB": "alu_sub", "SBC": "alu_sbc",
         "AND": "alu_and", "XOR": "alu_xor", "OR": "alu_or", "CP": "alu_cp"}
ROTATES = {"RLC": "alu_rlc", "RRC": "alu_rrc", "RL": "alu_rl", "RR": "alu_rr",
           "SLA": "alu_sla", "SRA": "alu_sra", "SWAP": "alu_swap", "SRL": "alu_srl"}
SIMPLE = {"NOP": [], "RLCA": ["rlca();"], "RRCA": ["rrca();"], "RLA": ["rla();"],
          "RRA": ["rra();"], "DAA": ["daa();"], "CPL": ["cpl();"], "SCF": ["scf();"],
          "CCF": ["ccf();"], "HALT": ["halt();"], "STOP": ["stop();"], "DI": ["di();"],
          "EI": ["ei();"], "RETI": ["reti();"], "PREFIX": ["execute_cb(fetch8());"]}


class TableError(Exception):
    pass


def is_r8(op: dict) -> bool:
    return op["immediate"] and op["name"] in R8


def is_r16(op: dict) -> bool:
    return op["immediate"] and op["name"] in R16


def r16_get(name: str) -> str:
    return "reg_.sp" if name == "SP" else f"reg_.{name.lower()}()"


def r16_set(name: str, value: str) -> str:
    return f"reg_.sp = {value};" if name == "SP" else f"reg_.set_{name.lower()}({value});"


def address_of(op: dict) -> str:
    """C++ expression for the address a memory operand points at."""
    name = op["name"]
    if name == "HL":
        if op.get("increment"):
            return "hl_post_increment()"
        if op.get("decrement"):
            return "hl_post_decrement()"
        return "reg_.hl()"
    if name in ("BC", "DE"):
        return r16_get(name)
    if name == "a16":
        return "fetch16()"
    if name == "a8":
        return "high_page(fetch8())"
    if name == "C":
        return "high_page(reg_.c)"
    raise TableError(f"unknown memory operand {op}")


def read8(op: dict) -> str:
    """C++ expression that yields an 8-bit operand's value."""
    if is_r8(op):
        return f"reg_.{op['name'].lower()}"
    if op["immediate"] and op["name"] == "n8":
        return "fetch8()"
    if not op["immediate"]:
        return f"read8({address_of(op)})"
    raise TableError(f"cannot read operand {op}")


def write8(op: dict, value: str) -> str:
    """C++ statement that stores an 8-bit value into an operand."""
    if is_r8(op):
        return f"reg_.{op['name'].lower()} = {value};"
    if not op["immediate"]:
        return f"write8({address_of(op)}, {value});"
    raise TableError(f"cannot write operand {op}")


def gen_ld(code: int, ops: list[dict]) -> list[str]:
    dst, src = ops[0], ops[1]
    if code == 0x08:  # LD [a16], SP
        return ["ld_a16_sp();"]
    if code == 0xF8:  # LD HL, SP+e8
        return ["reg_.set_hl(sp_plus_offset());", "internal_cycle();"]
    if code == 0xF9:  # LD SP, HL
        return ["reg_.sp = reg_.hl();", "internal_cycle();"]
    if is_r16(dst) and src["name"] == "n16":
        return [r16_set(dst["name"], "fetch16()")]
    return [write8(dst, read8(src))]


def gen_inc_dec(mnemonic: str, ops: list[dict]) -> list[str]:
    op = ops[0]
    if is_r16(op):
        helper = "inc16" if mnemonic == "INC" else "dec16"
        return [r16_set(op["name"], f"{helper}({r16_get(op['name'])})")]
    helper = "alu_inc" if mnemonic == "INC" else "alu_dec"
    return [write8(op, f"{helper}({read8(op)})")]


def gen_add(ops: list[dict]) -> list[str]:
    dst, src = ops[0], ops[1]
    if dst["name"] == "HL":
        return [f"alu_add_hl({r16_get(src['name'])});"]
    if dst["name"] == "SP":
        return ["reg_.sp = sp_plus_offset();", "internal_cycle();", "internal_cycle();"]
    return [f"alu_add({read8(src)});"]


def gen_flow(mnemonic: str, ops: list[dict]) -> list[str]:
    if mnemonic == "RST":
        return [f"rst(0x{int(ops[0]['name'].lstrip('$'), 16):02X});"]
    if mnemonic == "JP" and ops[0]["name"] == "HL":
        return ["reg_.pc = reg_.hl();"]
    # The conditional forms carry one extra operand in front: the condition.
    conditional = len(ops) == (1 if mnemonic == "RET" else 2)
    condition = CONDITIONS[ops[0]["name"]] if conditional else "true"
    if mnemonic == "RET":
        return [f"ret_if({condition});"] if conditional else ["ret();"]
    return [f"{mnemonic.lower()}({condition});"]


def gen_unprefixed(code: int, entry: dict) -> list[str]:
    mnemonic, ops = entry["mnemonic"], entry["operands"]
    if mnemonic in SIMPLE:
        return SIMPLE[mnemonic]
    if mnemonic.startswith("ILLEGAL"):
        return ["locked_ = true;"]
    if mnemonic in ("LD", "LDH"):
        return gen_ld(code, ops)
    if mnemonic in ("INC", "DEC"):
        return gen_inc_dec(mnemonic, ops)
    if mnemonic == "ADD":
        return gen_add(ops)
    if mnemonic in ALU_A:
        return [f"{ALU_A[mnemonic]}({read8(ops[1])});"]
    if mnemonic in ("JP", "JR", "CALL", "RET", "RST"):
        return gen_flow(mnemonic, ops)
    if mnemonic == "PUSH":
        return [f"push({r16_get(ops[0]['name'])});"]
    if mnemonic == "POP":
        return [r16_set(ops[0]["name"], "pop16()")]
    raise TableError(f"unhandled mnemonic {mnemonic} at 0x{code:02X}")


def gen_cb(code: int, entry: dict) -> list[str]:
    mnemonic, ops = entry["mnemonic"], entry["operands"]
    if mnemonic in ROTATES:
        return [write8(ops[0], f"{ROTATES[mnemonic]}({read8(ops[0])})")]
    bit = int(ops[0]["name"])
    if mnemonic == "BIT":
        return [f"alu_bit({bit}, {read8(ops[1])});"]
    if mnemonic in ("RES", "SET"):
        return [write8(ops[1], f"{mnemonic.lower()}_bit({bit}, {read8(ops[1])})")]
    raise TableError(f"unhandled CB mnemonic {mnemonic} at 0x{code:02X}")


def operand_text(op: dict) -> str:
    text = op["name"]
    if op.get("increment"):
        text += "+"
    if op.get("decrement"):
        text += "-"
    return text if op["immediate"] else f"[{text}]"


def disassembly(code: int, entry: dict, prefixed: bool) -> str:
    mnemonic, ops = entry["mnemonic"], entry["operands"]
    if not prefixed and code == 0xF8:
        return "LD HL, SP+e8"
    if mnemonic.startswith("ILLEGAL"):
        return "ILLEGAL"
    if not ops:
        return mnemonic
    return f"{mnemonic} " + ", ".join(operand_text(op) for op in ops)


def switch_body(table: dict, prefixed: bool) -> list[str]:
    lines = []
    for code in range(256):
        entry = table[f"0x{code:02X}"]
        body = gen_cb(code, entry) if prefixed else gen_unprefixed(code, entry)
        lines.append(f"        case 0x{code:02X}:  // {disassembly(code, entry, prefixed)}")
        lines.extend(f"            {statement}" for statement in body)
        lines.append("            break;")
    return lines


def info_rows(table: dict, prefixed: bool) -> list[str]:
    rows = []
    for code in range(256):
        entry = table[f"0x{code:02X}"]
        cycles = entry["cycles"]
        flags = "".join(entry["flags"][name] for name in "ZNHC")
        rows.append(
            f'    {{"{disassembly(code, entry, prefixed)}", {entry["bytes"]}, '
            f'{cycles[0]}, {cycles[-1]}, "{flags}"}},  // 0x{code:02X}'
        )
    return rows


def render() -> str:
    with TABLE.open(encoding="utf-8") as handle:
        tables = json.load(handle)
    plain, cb = tables["unprefixed"], tables["cbprefixed"]
    lines = [
        "// Generated by tools/gen_opcodes.py from tools/opcodes.json. Do not edit by hand.",
        "// To change an instruction, edit the helpers in core/cpu.cpp or the generator,",
        "// then run: python3 tools/gen_opcodes.py",
        "//",
        "// clang-format off",
        "",
        '#include "core/cpu.h"',
        '#include "core/opcode_info.h"',
        "",
        "namespace core {",
        "",
        "// Several opcodes share a body (the 11 undefined ones, for example); that is expected.",
        "// NOLINTBEGIN(bugprone-branch-clone, readability-function-size)",
        "",
        "void Cpu::execute(std::uint8_t opcode) {",
        "    switch (opcode) {",
        *switch_body(plain, prefixed=False),
        "        default:",
        "            break;",
        "    }",
        "}",
        "",
        "void Cpu::execute_cb(std::uint8_t opcode) {",
        "    switch (opcode) {",
        *switch_body(cb, prefixed=True),
        "        default:",
        "            break;",
        "    }",
        "}",
        "",
        "// NOLINTEND(bugprone-branch-clone, readability-function-size)",
        "",
        "const std::array<OpcodeInfo, 256> kOpcodeInfo = {{",
        *info_rows(plain, prefixed=False),
        "}};",
        "",
        "const std::array<OpcodeInfo, 256> kCbOpcodeInfo = {{",
        *info_rows(cb, prefixed=True),
        "}};",
        "",
        "}  // namespace core",
        "",
    ]
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true",
                        help="do not write; exit 1 if the generated file is out of date")
    args = parser.parse_args()

    try:
        text = render()
    except TableError as error:
        print(f"gen_opcodes: {error}", file=sys.stderr)
        return 2

    if args.check:
        current = OUTPUT.read_text(encoding="utf-8") if OUTPUT.exists() else ""
        if current != text:
            print(f"gen_opcodes: {OUTPUT.relative_to(ROOT)} is out of date; "
                  "run python3 tools/gen_opcodes.py", file=sys.stderr)
            return 1
        return 0

    OUTPUT.write_text(text, encoding="utf-8")
    print(f"wrote {OUTPUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
