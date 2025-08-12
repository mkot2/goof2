#pragma once

#include <dlib/matrix.h>

#include <cmath>
#include <string>

#include "vm.hxx"

namespace goof2 {

struct ProgramFeatures {
    double length;
    double loops;
    double io_density;
};

ProgramFeatures extract_features(const std::string& code);

inline MemoryModel predict_memory_model(const ProgramFeatures& f) {
    dlib::matrix<double, 3, 1> x;
    x = f.length, f.loops, f.io_density;
    const dlib::matrix<double, 3, 1> w = {0.0, 2.0, 5.0};
    const double b = -1.0;
    double z = dlib::dot(w, x) + b;
    double p = 1.0 / (1.0 + std::exp(-z));
    return p > 0.5 ? MemoryModel::Paged : MemoryModel::Contiguous;
}

}  // namespace goof2
