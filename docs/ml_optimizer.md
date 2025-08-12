# Machine Learning Optimizer

Goof2 can optionally apply a machine-learned optimizer to Brainfuck source code.
The optimizer is trained on pairs of raw and optimized programs and produces a
set of rewrite rules stored in `assets/ml_model.txt`.

Enable the optimizer with the `--ml-opt` flag:

```sh
./goof2 --ml-opt -e "+-[-]" 
```

When enabled, the model-driven rewrites run after the built-in regex
optimizations and can further simplify the program.

## Training the model

Create a tab-separated text file where each line contains a raw Brainfuck snippet
followed by its optimized form. Train a new model with:

```sh
python tools/train_ml_model.py path/to/pairs.tsv
```

The generated rules are written to `assets/ml_model.txt`.
