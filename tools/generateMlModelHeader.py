#!/usr/bin/env python3
import argparse
import pathlib


def escape_cpp(s: str) -> str:
    return s.replace('\\', r'\\').replace('"', r'\"')


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate ml model header from text rules")
    parser.add_argument('input', type=pathlib.Path)
    parser.add_argument('output', type=pathlib.Path)
    args = parser.parse_args()

    rules = []
    with args.input.open('r', encoding='utf-8') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#') or line.startswith('//'):
                continue
            parts = line.split('\t')
            if len(parts) != 2:
                continue
            pattern, replacement = parts
            rules.append((pattern, replacement))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('w', encoding='utf-8') as out:
        out.write('// Auto-generated from assets/ml_model.txt\n')
        out.write('#pragma once\n\n')
        out.write('namespace goof2 {\n')
        out.write('struct MlRule { const char* pattern; const char* replacement; };\n')
        out.write('inline constexpr MlRule mlModel[] = {\n')
        for pattern, replacement in rules:
            out.write(f'    {{R"({pattern})", "{escape_cpp(replacement)}"}},\n')
        out.write('};\n')
        out.write('inline constexpr size_t mlModelCount = sizeof(mlModel) / sizeof(mlModel[0]);\n')
        out.write('} // namespace goof2\n')


if __name__ == '__main__':
    main()
