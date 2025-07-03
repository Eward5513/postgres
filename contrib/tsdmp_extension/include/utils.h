#ifndef TSDMP_UTILS_H
#define TSDMP_UTILS_H

#include <string>
#include <vector>
#include <random>
#include <chrono>
#include <cstdint>

class ContinuousRandomGenerator {
    public:
        ContinuousRandomGenerator(float min, float max);
    
        float generate();
    
    private:
        std::random_device rd;
        std::mt19937 gen;
        std::uniform_real_distribution<float> dist;
};

class TimerClock {
    private:
        std::chrono::time_point<std::chrono::high_resolution_clock> _ticker;
    public:
        TimerClock() {
            tick();
        }
    
        ~TimerClock() = default;
    
        void tick() {
            _ticker = std::chrono::high_resolution_clock::now();
        }
    
        [[nodiscard]] double second() const {
            return static_cast<double>(nanoSec()) * 1e-9;
        }
    
        [[nodiscard]] double milliSec() const {
            return static_cast<double>(nanoSec()) * 1e-6;
        }
    
        [[nodiscard]] double microSec() const {
            return static_cast<double>(nanoSec()) * 1e-3;
        }
        [[nodiscard]] long long nanoSec() const {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::high_resolution_clock::now() - _ticker).count();
        }
    
};

bool clear_folder(std::string folderPath);
std::vector<std::string> findFilesWithPrefix(const std::string &folderPath, const std::string &prefix);
uint64_t interleaveBits(uint64_t x, uint64_t y, uint64_t z, uint64_t level);

#endif