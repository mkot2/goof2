#!/usr/bin/env python3
"""Train memory model predictor.

Uses scikit-learn (BSD-3-Clause license) logistic regression to fit
features [log2(tape_size+1), log2(access_delta+1)] to MemoryModel class.
"""
import argparse
import csv
import math
from sklearn.linear_model import LogisticRegression

def main(data_path: str, out_path: str) -> None:
    X, y = [], []
    with open(data_path) as f:
        reader = csv.reader(f)
        for row in reader:
            tape_size, current_cell, needed_index, model = map(int, row)
            delta = needed_index - current_cell
            X.append([math.log2(tape_size + 1), math.log2(delta + 1)])
            y.append(model)
    clf = LogisticRegression(max_iter=1000, multi_class='auto')
    clf.fit(X, y)
    with open(out_path, 'w') as f:
        for intercept, coef in zip(clf.intercept_, clf.coef_):
            f.write(f"{intercept},{coef[0]},{coef[1]}\n")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('--data', default='data.csv')
    parser.add_argument('--out', default='model.csv')
    args = parser.parse_args()
    main(args.data, args.out)
