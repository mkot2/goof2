#!/usr/bin/env python3
"""
Collect Brainfuck programs and their optimized variants.
This script scans an input directory for `.bf` files, applies simple
normalization rules, and writes paired samples for training.

Published under the GNU AGPL-3.0-or-later license.
"""
import argparse
import pathlib
import re

def normalize(code: str) -> str:
    # Basic cleanup similar to the VM's regex rules
    code = re.sub(r"[^+\-<>\.,\[\]]", "", code)
    code = re.sub(r"([+-]){2,}", lambda m: m.group(0)[0] * (m.group(0).count(m.group(0)[0]) % 2), code)
    code = re.sub(r"([<>]){2,}", lambda m: m.group(0)[0] * (m.group(0).count(m.group(0)[0]) % 2), code)
    return code

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=pathlib.Path, help="directory containing Brainfuck programs")
    parser.add_argument("output", type=pathlib.Path, help="file to write dataset lines")
    args = parser.parse_args()

    samples = []
    for bf in sorted(args.input.glob("*.bf")):
        raw = bf.read_text(encoding="utf-8")
        optimized = normalize(raw)
        samples.append(f"{raw.replace(chr(10), ' ')}\t{optimized}\n")

    args.output.write_text("".join(samples), encoding="utf-8")

if __name__ == "__main__":
    main()
