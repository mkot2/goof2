#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "vm.hxx"

[[maybe_unused]] static goof2::MemoryModel runProgram(const std::string& code) {
    const char* dataset = "mm_log.txt";
    std::remove(dataset);
    setenv("GOOF2_MM_DATASET", dataset, 1);
    std::vector<uint8_t> cells(1, 0);
    size_t ptr = 0;
    std::string codeCopy = code;
    goof2::execute<uint8_t>(cells, ptr, codeCopy, true, 0, true, false, goof2::MemoryModel::Auto);
    std::ifstream f(dataset);
    std::string line;
    std::getline(f, line);
    std::remove(dataset);
    int model = std::stoi(line.substr(line.rfind(',') + 1));
    return static_cast<goof2::MemoryModel>(model);
}

int main() {
    setenv("GOOF2_MM_MODEL", "../../tools/ml_memmodel/model.csv", 1);
    assert(runProgram(">") == goof2::MemoryModel::Contiguous);
    assert(goof2::predictMemoryModel(70000, 70000) == goof2::MemoryModel::Fibonacci);
    return 0;
}
