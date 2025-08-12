# Machine Learning Optimizer

Goof2 can optionally apply a machine-learned optimizer to Brainfuck source code.
The optimizer is trained on pairs of raw and optimized programs and produces a
set of rewrite rules stored in `assets/ml_model.txt`.

## Rule format

The model file is a plain text, tab-delimited list of pattern-replacement
pairs. Each line contains the original Brainfuck snippet, a tab character, and
its optimized form. Lines with no tab are ignored, allowing the file to contain
comments if desired.

Example rules:

```
[-]\t0
<>\t><
```

The first rule replaces a loop that clears the current cell with `0`, while the second swaps `<>` for `><`.

## Location

`goof2` looks for the model at `assets/ml_model.txt` relative to the working
directory. Distribute the `assets/` folder alongside the executable so the
optimizer can load the file at runtime.

Enable the optimizer with the `--ml-opt` flag:

```sh
./goof2 --ml-opt -e "+-[-]"
```

When enabled, the model-driven rewrites run after the built-in regex
optimizations and can further simplify the program.
