#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include "vm.hxx"

#ifdef USE_MODEL_FUZZ
struct Transition {
    char nextOp;
    double cumulative;
};
using TransitionVec = std::vector<Transition>;
using BigramModel = std::unordered_map<char, TransitionVec>;

static BigramModel loadModel(const std::string& path) {
    BigramModel model;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ss(line);
        std::string fromStr, toStr, probStr;
        if (!std::getline(ss, fromStr, ',')) continue;
        if (!std::getline(ss, toStr, ',')) continue;
        if (!std::getline(ss, probStr)) continue;
        double prob = std::stod(probStr);
        model[fromStr[0]].push_back({toStr[0], prob});
    }
    for (auto& kv : model) {
        double acc = 0.0;
        for (auto& t : kv.second) {
            acc += t.cumulative;
            t.cumulative = acc;
        }
    }
    return model;
}

static std::string modelProgram(BigramModel& model, std::mt19937& gen) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    std::string program;
    char prev = '^';
    for (int i = 0; i < 16; ++i) {
        auto it = model.find(prev);
        if (it == model.end() || it->second.empty()) break;
        double r = dist(gen);
        char next = '^';
        for (const auto& t : it->second) {
            if (r <= t.cumulative) {
                next = t.nextOp;
                break;
            }
        }
        if (next == '^') break;
        program += next;
        prev = next;
    }
    return program;
}
#else
static std::string randomProgram(std::mt19937& gen) {
    static const char ops[] = "+-<>.,";
    std::uniform_int_distribution<int> lenDist(0, 16);
    std::uniform_int_distribution<int> opDist(0, sizeof(ops) - 2);
    int len = lenDist(gen);
    std::string program;
    program.reserve(len);
    int ptr = 0;
    for (int i = 0; i < len; ++i) {
        char op = ops[opDist(gen)];
        if (op == '>' && ptr >= 31) {
            --i;
            continue;
        }
        if (op == '<' && ptr <= 0) {
            --i;
            continue;
        }
        program += op;
        if (op == '>')
            ++ptr;
        else if (op == '<')
            --ptr;
    }
    return program;
}
#endif

static std::string randomInput(std::mt19937& gen) {
    std::uniform_int_distribution<int> lenDist(0, 8);
    std::uniform_int_distribution<int> byteDist(0, 255);
    int len = lenDist(gen);
    std::string input;
    input.reserve(len);
    for (int i = 0; i < len; ++i) {
        input += static_cast<char>(byteDist(gen));
    }
    return input;
}

int main() {
    std::mt19937 gen(123456u);
#ifdef USE_MODEL_FUZZ
    BigramModel model = loadModel("model.txt");
#endif
    for (int i = 0; i < 100; ++i) {
#ifdef USE_MODEL_FUZZ
        std::string code = modelProgram(model, gen);
#else
        std::string code = randomProgram(gen);
#endif
        std::string input = randomInput(gen);
        std::vector<uint8_t> cells(32, 0);
        size_t ptr = 0;
        std::istringstream in(input);
        std::ostringstream out;
        auto* cinBuf = std::cin.rdbuf(in.rdbuf());
        auto* coutBuf = std::cout.rdbuf(out.rdbuf());
        std::cin.clear();
        std::atomic_bool done{false};
        std::thread watchdogThread([&done]() {
            for (int i = 0; i < 100; ++i) {
                if (done.load()) return;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            std::terminate();
        });
        goof2::ProfileInfo profile;
        try {
            goof2::execute<uint8_t>(cells, ptr, code, true, 0, true, false,
                                    goof2::MemoryModel::Auto, &profile);
        } catch (...) {
        }
        done = true;
        watchdogThread.join();
        std::cin.rdbuf(cinBuf);
        std::cout.rdbuf(coutBuf);
        std::ofstream covFile("coverage.jsonl", std::ios::app);
        covFile << "{\"program\":\"" << code << "\",\"coverage\":[";
        for (size_t j = 0; j < profile.loopCounts.size(); ++j) {
            if (j) covFile << ',';
            covFile << profile.loopCounts[j];
        }
        covFile << "]}\n";
    }
    return 0;
}
