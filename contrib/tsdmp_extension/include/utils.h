#ifndef TSDMP_UTILS_H
#define TSDMP_UTILS_H

#include <string>
#include <vector>
#include <random>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <boost/asio.hpp>

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
std::string getFileNameFromPath(const std::string &filePath);


class ThreadPoolWrapper {
    public:
        // 构造函数 - 使用explicit防止隐式类型转换
        // 原因：
        // 1. 防止意外的隐式转换：避免 ThreadPoolWrapper pool = 10; 这种危险的写法
        // 2. 避免函数参数的意外转换：防止函数调用时数字被自动转换为临时线程池对象
        // 3. 提高代码安全性：线程池是重要的系统资源，创建成本高，必须明确表达创建意图
        // 4. 增强代码可读性：强制使用 ThreadPoolWrapper(10) 的显式构造方式
        explicit ThreadPoolWrapper(std::size_t num_threads);
    
        // 提交任务到线程池
        template <typename F>
        void post_task(F&& f);
    
        // 等待所有任务完成
        void wait_for_all_tasks(std::int64_t remain=0);
    
        // 调整线程池大小
        void resize(std::size_t new_num_threads);
    
    private:
        // 创建线程池的函数
        void create_thread_pool(std::size_t num_threads);
    
        // 任务完成时的回调
        void on_task_done();
    
    private:
        std::unique_ptr<boost::asio::thread_pool> thread_pool;  // 使用unique_ptr管理线程池
        std::mutex mtx;                        // 保护任务计数的互斥锁
        std::condition_variable cv;            // 用于等待任务完成的条件变量
        int tasks_pending;                     // 等待完成的任务数量
    };
    
    template <typename F>
    void ThreadPoolWrapper::post_task(F&& f) {
        {
            std::lock_guard lock(mtx);
            ++tasks_pending;
        }
        boost::asio::post(*thread_pool, [this, f = std::forward<F>(f)]() {
            f();
            on_task_done();
        });
}
    
inline ThreadPoolWrapper thread_pool(200);

template <typename T>
std::vector<std::pair<T, T>> split(T start, T end, std::int64_t parts) {
    // 如果 parts 小于 1，抛出异常
    if (parts < 1) {
        // throw std::invalid_argument("Number of parts must be at least 1");
        parts = 1;
    }
    std::vector<std::pair<T, T>> result;
    double range = static_cast<double>(end - start);
    double step = range / parts; // 每一段的长度

    for (std::int64_t i = 0; i < parts; ++i) {
        T sub_start = static_cast<T>(start + i * step);
        T sub_end = (i == parts - 1) ? end : static_cast<T>(start + (i + 1) * step);
        result.emplace_back(sub_start, sub_end);
    }

    return result;
}
template std::vector<std::pair<double, double>> split(double start, double end, std::int64_t parts);
template std::vector<std::pair<float, float>> split(float start, float end, std::int64_t parts);
template std::vector<std::pair<std::int64_t, std::int64_t>> split(std::int64_t start, std::int64_t end, std::int64_t parts);

#endif