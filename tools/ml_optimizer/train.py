#!/usr/bin/env python3
"""
Train a simple pattern-based optimizer model from collected samples.
The model maps frequent substrings to their optimized replacements and
writes them to `assets/ml_model.txt`.

Published under the GNU AGPL-3.0-or-later license.
"""
import argparse
import collections
import pathlib

MODEL_PATH = pathlib.Path(__file__).resolve().parents[2] / "assets" / "ml_model.txt"

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("dataset", type=pathlib.Path, help="dataset produced by collect.py")
    args = parser.parse_args()

    counts: collections.Counter[tuple[str, str]] = collections.Counter()
    for line in args.dataset.read_text(encoding="utf-8").splitlines():
        raw, opt = line.split("\t", 1)
        if raw != opt:
            counts[(raw, opt)] += 1

    rules = [f"{raw}\t{opt}" for (raw, opt), _ in counts.most_common()]
    MODEL_PATH.write_text("\n".join(rules), encoding="utf-8")
    print(f"wrote {len(rules)} rules to {MODEL_PATH}")

if __name__ == "__main__":
    main()
