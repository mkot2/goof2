#!/usr/bin/env python3
"""Train a simple Brainfuck optimizer model from raw/optimized pairs."""
from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path


def loadPairs(pairPath: Path) -> list[tuple[str, str]]:
    """Return pairs of raw and optimized Brainfuck code."""
    pairList: list[tuple[str, str]] = []
    for line in pairPath.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        try:
            rawCode, optimizedCode = line.split("\t", 1)
        except ValueError:
            continue
        pairList.append((rawCode, optimizedCode))
    return pairList


def trainModel(pairList: list[tuple[str, str]]) -> dict[str, str]:
    """Choose the most common optimized code for each raw input."""
    countMap: dict[str, dict[str, int]] = defaultdict(dict)
    for rawCode, optimizedCode in pairList:
        innerMap = countMap.setdefault(rawCode, {})
        innerMap[optimizedCode] = innerMap.get(optimizedCode, 0) + 1
    modelMap: dict[str, str] = {}
    for rawCode, optimizedMap in countMap.items():
        optimizedCode = max(optimizedMap.items(), key=lambda item: item[1])[0]
        modelMap[rawCode] = optimizedCode
    return modelMap


def saveModel(modelMap: dict[str, str], outPath: Path) -> None:
    """Write the model mapping to disk."""
    with outPath.open("w", encoding="utf-8") as outFile:
        for rawCode, optimizedCode in modelMap.items():
            outFile.write(f"{rawCode}\t{optimizedCode}\n")


def main() -> None:
    argParser = argparse.ArgumentParser(
        description="Train Brainfuck optimizer from raw/optimized pairs"
    )
    argParser.add_argument(
        "pairFile",
        help="Path to tab-separated raw and optimized Brainfuck code pairs",
    )
    argParser.add_argument(
        "--output",
        default="assets/ml_model.txt",
        help="Destination for the trained model",
    )
    args = argParser.parse_args()

    pairPath = Path(args.pairFile)
    outPath = Path(args.output)
    pairList = loadPairs(pairPath)
    modelMap = trainModel(pairList)
    saveModel(modelMap, outPath)


if __name__ == "__main__":
    main()
