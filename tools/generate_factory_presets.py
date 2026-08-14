#!/usr/bin/env python3
"""Validate verified Poly-800 MkI factory data and generate LV2 presets.

The source CSV contains only factual program parameter values transcribed from
independently preserved factory documentation/dumps.  It intentionally does
not contain cassette audio, firmware, SysEx dumps, scans, or manual pages.
"""

from __future__ import annotations

import argparse
import csv
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
CATALOG = ROOT / "factory" / "poly800-mk1-catalog.csv"
DATA = ROOT / "factory" / "poly800-mk1-verified.csv"
OUTPUT = ROOT / "lv2" / "factory-presets.ttl"
PLUGIN = "https://interspock.github.io/isla-poly800"
BANK = PLUGIN + "#bank-factory-mk1"

# Original parameter number, LV2 symbol, inclusive minimum, inclusive maximum.
PARAMS = [
    (11, "p11_dco1_octave", 1, 3),
    (12, "p12_dco1_waveform", 1, 2),
    (13, "p13_dco1_h16", 0, 1),
    (14, "p14_dco1_h8", 0, 1),
    (15, "p15_dco1_h4", 0, 1),
    (16, "p16_dco1_h2", 0, 1),
    (17, "p17_dco1_level", 0, 31),
    (18, "p18_dco_mode", 1, 2),
    (21, "p21_dco2_octave", 1, 3),
    (22, "p22_dco2_waveform", 1, 2),
    (23, "p23_dco2_h16", 0, 1),
    (24, "p24_dco2_h8", 0, 1),
    (25, "p25_dco2_h4", 0, 1),
    (26, "p26_dco2_h2", 0, 1),
    (27, "p27_dco2_level", 0, 31),
    (31, "p31_dco2_interval", 0, 12),
    (32, "p32_dco2_detune", 0, 3),
    (33, "p33_noise", 0, 15),
    (41, "p41_vcf_cutoff", 0, 99),
    (42, "p42_vcf_resonance", 0, 15),
    (43, "p43_vcf_keytrack", 0, 2),
    (44, "p44_vcf_polarity", 1, 2),
    (45, "p45_vcf_env", 0, 15),
    (46, "p46_vcf_trigger", 1, 2),
    (48, "p48_chorus", 0, 1),
    (51, "p51_deg1_attack", 0, 31),
    (52, "p52_deg1_decay", 0, 31),
    (53, "p53_deg1_breakpoint", 0, 31),
    (54, "p54_deg1_slope", 0, 31),
    (55, "p55_deg1_sustain", 0, 31),
    (56, "p56_deg1_release", 0, 31),
    (61, "p61_deg2_attack", 0, 31),
    (62, "p62_deg2_decay", 0, 31),
    (63, "p63_deg2_breakpoint", 0, 31),
    (64, "p64_deg2_slope", 0, 31),
    (65, "p65_deg2_sustain", 0, 31),
    (66, "p66_deg2_release", 0, 31),
    (71, "p71_deg3_attack", 0, 31),
    (72, "p72_deg3_decay", 0, 31),
    (73, "p73_deg3_breakpoint", 0, 31),
    (74, "p74_deg3_slope", 0, 31),
    (75, "p75_deg3_sustain", 0, 31),
    (76, "p76_deg3_release", 0, 31),
    (81, "p81_mg_frequency", 0, 15),
    (82, "p82_mg_delay", 0, 15),
    (83, "p83_mg_dco", 0, 15),
    (84, "p84_mg_vcf", 0, 15),
]
EXPECTED_PROGRAMS = [10 * bank + slot for bank in range(1, 9) for slot in range(1, 9)]
EXPECTED_DATA_HEADER = ["program"] + [f"p{number}" for number, *_ in PARAMS]


def read_catalog() -> dict[int, str]:
    with CATALOG.open(newline="", encoding="utf-8") as fh:
        rows = list(csv.DictReader(fh))
    got = []
    catalog: dict[int, str] = {}
    for row in rows:
        try:
            program = int(row["program"])
        except (KeyError, ValueError) as exc:
            raise ValueError(f"invalid catalog program row: {row!r}") from exc
        name = row.get("name", "").strip()
        if not name:
            raise ValueError(f"program {program}: empty name")
        if program in catalog:
            raise ValueError(f"duplicate catalog program {program}")
        catalog[program] = name
        got.append(program)
    if got != EXPECTED_PROGRAMS:
        raise ValueError("catalog must contain programs 11..88 (digits 1..8) exactly once, in order")
    return catalog


def read_verified(catalog: dict[int, str]) -> list[tuple[int, list[int]]]:
    with DATA.open(newline="", encoding="utf-8") as fh:
        reader = csv.DictReader(fh)
        if reader.fieldnames != EXPECTED_DATA_HEADER:
            raise ValueError(
                "verified CSV header mismatch; expected: " + ",".join(EXPECTED_DATA_HEADER)
            )
        result: list[tuple[int, list[int]]] = []
        seen: set[int] = set()
        previous = 0
        ranges = {number: (lo, hi) for number, _symbol, lo, hi in PARAMS}
        for line, row in enumerate(reader, start=2):
            if not any((value or "").strip() for value in row.values()):
                continue
            try:
                program = int(row["program"])
            except (TypeError, ValueError) as exc:
                raise ValueError(f"line {line}: invalid program") from exc
            if program not in catalog:
                raise ValueError(f"line {line}: unknown MkI program {program}")
            if program in seen:
                raise ValueError(f"line {line}: duplicate program {program}")
            if program <= previous:
                raise ValueError("verified rows must be in ascending program order")
            values: list[int] = []
            for number, _symbol, lo, hi in PARAMS:
                cell = (row.get(f"p{number}") or "").strip()
                if cell == "":
                    raise ValueError(f"program {program}: p{number} is blank; partial presets are forbidden")
                try:
                    value = int(cell)
                except ValueError as exc:
                    raise ValueError(f"program {program}: p{number} must be an integer") from exc
                if not lo <= value <= hi:
                    raise ValueError(
                        f"program {program}: p{number}={value} outside {ranges[number][0]}..{ranges[number][1]}"
                    )
                values.append(value)
            seen.add(program)
            previous = program
            result.append((program, values))
        return result


def ttl_escape(text: str) -> str:
    return text.replace("\\", "\\\\").replace('"', '\\"')


def render(catalog: dict[int, str], rows: list[tuple[int, list[int]]]) -> str:
    lines = [
        "# Generated by tools/generate_factory_presets.py; do not edit by hand.",
        "@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .",
        "@prefix pset: <http://lv2plug.in/ns/ext/presets#> .",
        "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .",
        "",
        f"<{BANK}>",
        "    a pset:Bank ;",
        '    rdfs:label "Korg Poly-800 MkI Factory Programs" .',
        "",
    ]
    for program, values in rows:
        uri = f"{PLUGIN}#factory-{program}"
        label = f"{program} {catalog[program]}"
        ports = [("gain", "0.32"), ("tune", "0.0")]
        ports.extend((symbol, str(value)) for (_n, symbol, _lo, _hi), value in zip(PARAMS, values))
        lines.extend([
            f"<{uri}>",
            "    a pset:Preset ;",
            f"    lv2:appliesTo <{PLUGIN}> ;",
            f"    pset:bank <{BANK}> ;",
            f'    rdfs:label "{ttl_escape(label)}" ;',
            "    lv2:port",
        ])
        for index, (symbol, value) in enumerate(ports):
            suffix = " ." if index == len(ports) - 1 else " ,"
            lines.append(f'        [ lv2:symbol "{symbol}" ; pset:value {value} ]{suffix}')
        lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="validate and require checked-in TTL to match")
    parser.add_argument("--require-complete", action="store_true", help="fail unless all 64 programs are verified")
    parser.add_argument("--output", type=pathlib.Path, default=OUTPUT)
    args = parser.parse_args()

    try:
        catalog = read_catalog()
        rows = read_verified(catalog)
        if args.require_complete and len(rows) != 64:
            raise ValueError(f"factory bank incomplete: {len(rows)}/64 verified programs")
        generated = render(catalog, rows)
        if args.check:
            existing = args.output.read_text(encoding="utf-8") if args.output.exists() else ""
            if existing != generated:
                raise ValueError(f"{args.output.relative_to(ROOT)} is stale; regenerate it")
        else:
            args.output.write_text(generated, encoding="utf-8")
    except (OSError, ValueError) as exc:
        print(f"factory preset validation failed: {exc}", file=sys.stderr)
        return 1

    print(f"factory catalog: 64 programs; verified parameter rows: {len(rows)}/64")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
