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
