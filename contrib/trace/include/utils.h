#ifndef TRACE_UTILS_H
#define TRACE_UTILS_H

#include <string>
#include <vector>
#include <random>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <filesystem>
#include <fstream>
#include <boost/asio.hpp>

namespace fs = std::filesystem;

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

class BinaryKVStorage {
    private:
        fs::path base_dir;
    
        // 确保目录存在
        void ensure_directory_exists(const fs::path& dir) {
            if (!fs::exists(dir)) {
                fs::create_directories(dir);
            }
        }
    
        // 获取key对应的文件路径
        fs::path get_file_path(const std::string& key) const {
            return base_dir / (key + ".bin");
        }
    
    public:
        // 构造函数，设置公共文件夹路径
        explicit BinaryKVStorage(const std::string& base_path) 
            : base_dir(base_path) {
            ensure_directory_exists(base_dir);
        }
    
        // 写入数据
        template<typename T>
        bool write(const std::string& key, const std::vector<T>& data) {
            fs::path file_path = get_file_path(key);
            
            std::ofstream out(file_path, std::ios::binary);
            if (!out.is_open()) {
                return false;
            }
    
            // 写入数据
            out.write(reinterpret_cast<const char*>(data.data()), 
                        data.size() * sizeof(T));
            
            return out.good();
        }
        // 读取数据
        template<typename T>
        std::vector<T> read(const std::string& key) {
            fs::path file_path = get_file_path(key);
            std::vector<T> result;
    
            std::ifstream in(file_path, std::ios::binary | std::ios::ate);
            if (!in.is_open()) {
                return result; // 返回空vector
            }
    
            // 获取文件大小
            std::streamsize size = in.tellg();
            in.seekg(0, std::ios::beg);
    
            // 计算元素数量
            size_t count = size / sizeof(T);
            if (count == 0) {
                return result;
            }
    
            // 读取数据
            result.resize(count);
            in.read(reinterpret_cast<char*>(result.data()), size);
            
            return result;
        }
    
        // 检查key是否存在
        bool exists(const std::string& key) const {
            return fs::exists(get_file_path(key));
        }
    
        // 删除key对应的文件
        bool remove(const std::string& key) {
            
            return fs::remove(get_file_path(key));
        }
};

class FileLockManager {
    public:
        explicit FileLockManager(size_t lock_count = 2000) : lock_pool_(lock_count) {}
    
        // 锁定指定的文件名
        void lock(const std::string& file_name) {
            size_t lock_index = hash(file_name) % lock_pool_.size();
            lock_pool_[lock_index].lock(); // 锁定对应的互斥锁
        }
    
        // 解锁指定的文件名
        void unlock(const std::string& file_name) {
            size_t lock_index = hash(file_name) % lock_pool_.size();
            lock_pool_[lock_index].unlock(); // 解锁对应的互斥锁
        }
    
    private:
        // 哈希函数，用于将文件名映射到锁
        size_t hash(const std::string& file_name) const {
            return std::hash<std::string>{}(file_name);
        }
                      // 锁池大小
        std::vector<std::mutex> lock_pool_;       // 直接存储互斥锁，避免动态分配
};

inline FileLockManager files_lock;

template <typename T>
inline T max3(T a, T b, T c) {
    return std::max(a, std::max(b, c));
}

/**
 * @brief Template function for generating random numbers in a range
 * @tparam T Numeric type for the random number (default: float)
 * @param min Minimum value (inclusive)
 * @param max Maximum value (exclusive)
 * @return Random number in the specified range
 * 
 * Thread-safe random number generator using static thread_local generators
 * to avoid contention in multi-threaded environments.
 */
 template <typename T = float>
 inline T random_range(T min = 0.0, T max = 1.0)
 {
     static std::random_device rd;
     static std::mt19937 gen(rd());
     std::uniform_real_distribution<T> dis(min, max);
     return dis(gen);
 }

 std::string base64_encode(const std::string& input);
 std::string base64_decode(const std::string& input);

#endif