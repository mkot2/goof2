#!/usr/bin/env python3
"""
Train a simple bigram sequence model for Brainfuck programs based on
coverage data produced by the fuzz harness. Each input line in the
coverage file should be a JSON object of the form::

    {"program": "++-", "coverage": [1,0,2]}

Programs that leave more paths uncovered are given higher weight when
building the model. The resulting transition probabilities are written to
``model.txt`` with one comma-separated transition per line:

    prev,next,probability

where ``prev`` is either ``^`` for the start state or a Brainfuck
character and ``next`` is one of ``+-<>.,[]``.
"""
import json
from collections import defaultdict
from pathlib import Path


def trainModel(coveragePath: Path, outputPath: Path) -> None:
    tokens = ['+', '-', '<', '>', '.', ',', '[', ']']
    startToken = '^'
    transitions = defaultdict(lambda: defaultdict(float))
    if coveragePath.exists():
        with coveragePath.open() as inFile:
            for line in inFile:
                data = json.loads(line)
                programStr = data.get('program', '')
                coverageVec = data.get('coverage', [])
                weight = coverageVec.count(0) + 1
                prevToken = startToken
                for ch in programStr:
                    if ch in tokens:
                        transitions[prevToken][ch] += weight
                        prevToken = ch
                transitions[prevToken][startToken] += weight
    epsilon = 1e-3
    for frmToken in tokens + [startToken]:
        for toToken in tokens:
            transitions[frmToken][toToken] += epsilon
    with outputPath.open('w') as outFile:
        for frmToken in [startToken] + tokens:
            total = sum(transitions[frmToken][toToken] for toToken in tokens)
            for toToken in tokens:
                prob = transitions[frmToken][toToken] / total if total else 1.0 / len(tokens)
                outFile.write(f"{frmToken},{toToken},{prob}\n")


if __name__ == '__main__':
    import argparse

    parser = argparse.ArgumentParser(description='Train bigram model for Brainfuck fuzzing')
    parser.add_argument('coverage', type=Path, help='Path to coverage JSONL file')
    parser.add_argument('-o', '--output', type=Path, default=Path('model.txt'),
                        help='Output model file')
    args = parser.parse_args()
    trainModel(args.coverage, args.output)
