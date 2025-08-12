#!/usr/bin/env python3
"""Train a tiny logistic regression model for memory model selection."""
import numpy as np
from sklearn.linear_model import LogisticRegression

# Features: [length, loops, io_density]
X = np.array([
    [3, 0, 0.0],
    [3, 1, 0.0],
    [10, 2, 0.2],
    [30, 3, 0.3],
])
y = np.array([0, 1, 1, 1])
clf = LogisticRegression().fit(X, y)
print("weights", clf.coef_[0])
print("bias", clf.intercept_[0])
