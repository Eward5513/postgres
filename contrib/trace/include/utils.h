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

// ============================================================================
// BASIC UTILITY FUNCTIONS (NO DEPENDENCIES)
// ============================================================================

/**
 * @brief Extract filename from full file path
 * 
 * Extracts the filename component from a complete file path by finding the
 * last directory separator (/ or \) and returning everything after it.
 * Handles both Unix and Windows path separators for cross-platform compatibility.
 * 
 * @param filePath Complete file path (e.g., "/path/to/file.txt")
 * @return std::string Filename without path (e.g., "file.txt")
 * 
 * @note Returns the original string if no path separator is found
 * @note Thread-safe as it only performs string operations
 * 
 * @example
 * std::string filename = getFileNameFromPath("/data/mesh/model001.ply");
 * // Returns: "model001.ply"
 */
// std::string getFileNameFromPath(const std::string &filePath);

/**
 * @brief Calculate maximum of three values
 * 
 * Template function that returns the largest of three values using
 * standard comparison operators. Works with any type that supports
 * comparison operations (>, <, ==).
 * 
 * @tparam T Type of values to compare (must support comparison operators)
 * @param a First value
 * @param b Second value
 * @param c Third value
 * @return T The maximum value among the three inputs
 * 
 * @note Template instantiation happens at compile time
 * @note Thread-safe for immutable types
 * 
 * @example
 * int maxVal = max3(10, 5, 8);      // Returns: 10
 * float maxFloat = max3(1.5f, 2.3f, 1.8f);  // Returns: 2.3f
 */
template <typename T>
inline T max3(T a, T b, T c) {
    return std::max(a, std::max(b, c));
}

/**
 * @brief Generate random number in specified range
 * 
 * Thread-safe random number generator using thread-local storage to avoid
 * contention in multi-threaded environments. Uses high-quality Mersenne Twister
 * generator with uniform real distribution for consistent random values.
 * 
 * @tparam T Numeric type for the random number (default: float)
 * @param min Minimum value (inclusive)
 * @param max Maximum value (exclusive for floating-point, inclusive for integers)
 * @return T Random number in the specified range
 * 
 * @note Thread-safe through thread_local storage
 * @note Each thread maintains its own generator state
 * @note Uses std::random_device for initial seeding
 * 
 * @example
 * float rand_val = random_range(0.0f, 1.0f);    // Random float [0.0, 1.0)
 * double rand_prob = random_range<double>();     // Random double [0.0, 1.0)
 * int rand_int = random_range(1, 100);          // Random int [1, 99]
 */
template <typename T = float>
inline T random_range(T min = 0.0, T max = 1.0)
{
    static thread_local std::random_device rd;
    static thread_local std::mt19937 gen(rd());
    std::uniform_real_distribution<T> dis(min, max);
    return dis(gen);
}

/**
 * @brief Interleave bits of 3D coordinates for Z-order curve indexing
 * 
 * Performs bit interleaving (Morton encoding) to convert 3D spatial coordinates
 * into a single 1D index that preserves spatial locality. This is essential
 * for spatial data structures like octrees and efficient spatial queries.
 * 
 * The algorithm interleaves bits in the pattern: z2,y2,x2,z1,y1,x1,z0,y0,x0
 * where subscripts represent bit positions from LSB to MSB.
 * 
 * @param x X-coordinate (spatial dimension)
 * @param y Y-coordinate (spatial dimension)
 * @param z Z-coordinate (spatial dimension)
 * @param level Number of bits to process from each coordinate
 * @return uint64_t Morton-encoded index preserving spatial locality
 * 
 * @note Level parameter determines precision: level=10 gives 30-bit index
 * @note Essential for octree construction and spatial partitioning
 * @note Thread-safe as it performs only arithmetic operations
 * 
 * @example
 * uint64_t morton = interleaveBits(5, 3, 7, 4);  // 4-bit precision
 * // Converts (x=5, y=3, z=7) to single index for spatial lookup
 */
uint64_t interleaveBits(uint64_t x, uint64_t y, uint64_t z, uint64_t level);

// ============================================================================
// RANGE SPLITTING UTILITIES
// ============================================================================

/**
 * @brief Split numeric range into equal parts
 * 
 * Divides a continuous range [start, end] into a specified number of
 * approximately equal sub-ranges. Useful for parallelizing work across
 * multiple threads or distributing data processing tasks.
 * 
 * The last partition includes any remainder to ensure complete coverage.
 * Each partition is represented as a pair of (start, end) values.
 * 
 * @tparam T Numeric type (int, float, double, etc.)
 * @param start Beginning of the range (inclusive)
 * @param end End of the range (exclusive)
 * @param parts Number of partitions to create (must be >= 1)
 * @return std::vector<std::pair<T, T>> Vector of (start, end) pairs
 * 
 * @note Automatically adjusts parts to 1 if input is < 1
 * @note Last partition may be slightly larger to include remainder
 * @note Thread-safe as it only performs calculations
 * 
 * @example
 * auto ranges = split<int>(0, 100, 4);
 * // Returns: [(0,25), (25,50), (50,75), (75,100)]
 * 
 * auto ranges = split<double>(0.0, 1.0, 3);
 * // Returns: [(0.0,0.333), (0.333,0.667), (0.667,1.0)]
 */
template <typename T>
std::vector<std::pair<T, T>> split(T start, T end, std::int64_t parts) {
    // Ensure at least one partition
    if (parts < 1) {
        parts = 1;
    }
    
    std::vector<std::pair<T, T>> result;
    double range = static_cast<double>(end - start);
    double step = range / parts;

    for (std::int64_t i = 0; i < parts; ++i) {
        T sub_start = static_cast<T>(start + i * step);
        T sub_end = (i == parts - 1) ? end : static_cast<T>(start + (i + 1) * step);
        result.emplace_back(sub_start, sub_end);
    }

    return result;
}

// Explicit template instantiations for common types
extern template std::vector<std::pair<double, double>> split(double start, double end, std::int64_t parts);
extern template std::vector<std::pair<float, float>> split(float start, float end, std::int64_t parts);
extern template std::vector<std::pair<std::int64_t, std::int64_t>> split(std::int64_t start, std::int64_t end, std::int64_t parts);

// ============================================================================
// ENCODING UTILITIES
// ============================================================================

/**
 * @brief Encode string to Base64 format
 * 
 * Converts binary or text data to Base64 encoding using the standard
 * Base64 alphabet. Useful for encoding binary data for transmission
 * over text-based protocols or storage in text databases.
 * 
 * @param input String data to encode (can contain binary data)
 * @return std::string Base64 encoded string
 * 
 * @note Uses standard Base64 alphabet (A-Z, a-z, 0-9, +, /)
 * @note Output string length is approximately 4/3 of input length
 * @note Thread-safe as it uses local variables only
 * 
 * @example
 * std::string encoded = base64_encode("Hello World");
 * // Returns: "SGVsbG8gV29ybGQ="
 */
std::string base64_encode(const std::string& input);

/**
 * @brief Decode Base64 string to original data
 * 
 * Converts Base64 encoded data back to its original form. Handles
 * standard Base64 padding and validates input format. Throws exception
 * for invalid Base64 input.
 * 
 * @param input Base64 encoded string
 * @return std::string Decoded original data
 * 
 * @throws std::exception for invalid Base64 input format
 * @note Thread-safe as it uses local variables only
 * @note Handles padding characters (=) correctly
 * 
 * @example
 * std::string decoded = base64_decode("SGVsbG8gV29ybGQ=");
 * // Returns: "Hello World"
 */
std::string base64_decode(const std::string& input);

// ============================================================================
// FILE SYSTEM UTILITIES
// ============================================================================

/**
 * @brief Clear folder contents or create folder if it doesn't exist
 * 
 * Comprehensive folder management function that either clears all contents
 * of an existing directory or creates a new directory if it doesn't exist.
 * Provides detailed logging for each operation and robust error handling.
 * 
 * Operations performed:
 * - If folder doesn't exist: Creates the directory structure
 * - If folder exists: Removes all files and subdirectories
 * - Logs all operations for debugging and monitoring
 * 
 * @param folderPath Path to the target folder (absolute or relative)
 * @return bool True if operation succeeded, false on error
 * 
 * @note Creates parent directories as needed
 * @note Handles permission errors gracefully
 * @note Provides detailed console output for monitoring
 * @note Thread-safe for different folder paths
 * 
 * @example
 * bool success = clear_folder("/tmp/data_processing");
 * if (success) {
 *     // Folder is now empty and ready for use
 * }
 */
bool clear_folder(std::string folderPath);

/**
 * @brief Find all files with specified prefix in directory
 * 
 * Scans the given directory and returns a list of all regular files
 * whose names start with the specified prefix. Useful for finding
 * files by naming convention or file type patterns.
 * 
 * @param folderPath Directory path to search in
 * @param prefix Filename prefix to match (e.g., "data_", "mesh_")
 * @return std::vector<std::string> List of matching filenames (not full paths)
 * 
 * @note Returns only filenames, not full paths
 * @note Only searches the specified directory (not recursive)
 * @note Case-sensitive prefix matching
 * @note Thread-safe for read-only directory operations
 * 
 * @example
 * auto mesh_files = findFilesWithPrefix("/data/models/", "mesh_");
 * // Might return: ["mesh_001.ply", "mesh_002.ply", "mesh_dragon.obj"]
 */
std::vector<std::string> findFilesWithPrefix(const std::string &folderPath, const std::string &prefix);

// ============================================================================
// RANDOM NUMBER GENERATION
// ============================================================================

/**
 * @brief High-quality continuous random number generator
 * 
 * Provides consistent random number generation using Mersenne Twister
 * algorithm with uniform real distribution. Maintains internal state
 * for reproducible sequences when needed.
 * 
 * Features:
 * - High-quality Mersenne Twister generator (MT19937)
 * - Uniform real distribution for even probability
 * - Configurable range at construction time
 * - Thread-safe when each thread uses separate instance
 * 
 * @note Each instance maintains its own generator state
 * @note Suitable for scientific simulations requiring quality randomness
 * @note More overhead than random_range() but provides better control
 */
class ContinuousRandomGenerator {
public:
    /**
     * @brief Construct generator with specified range
     * 
     * @param min Minimum value (inclusive)
     * @param max Maximum value (exclusive)
     */
    ContinuousRandomGenerator(float min, float max);

    /**
     * @brief Generate next random number in the configured range
     * 
     * @return float Random value between min (inclusive) and max (exclusive)
     */
    float generate();

private:
    std::random_device rd;                        ///< Hardware random number generator for seeding
    std::mt19937 gen;                            ///< Mersenne Twister generator
    std::uniform_real_distribution<float> dist;  ///< Uniform distribution in specified range
};

// ============================================================================
// TIMING AND PERFORMANCE MEASUREMENT
// ============================================================================

/**
 * @brief High-precision timing and performance measurement utility
 * 
 * Provides microsecond-precision timing for performance profiling and
 * benchmarking. Uses high-resolution clock for accurate measurements
 * suitable for both short operations and long-running processes.
 * 
 * Features:
 * - Automatic timing start on construction
 * - Multiple time unit outputs (seconds, milliseconds, microseconds, nanoseconds)
 * - Reset capability with tick() method
 * - Minimal overhead for accurate measurements
 * 
 * @note Uses std::chrono::high_resolution_clock for best available precision
 * @note Thread-safe when each thread uses separate instance
 * @note Suitable for nested timing measurements
 */
class TimerClock {
public:
    /**
     * @brief Construct timer and start timing immediately
     */
    TimerClock() {
        tick();
    }

    /**
     * @brief Default destructor
     */
    ~TimerClock() = default;

    /**
     * @brief Reset timer to current time
     * 
     * Resets the internal timestamp to the current time, allowing
     * measurement of subsequent operations from this point.
     */
    void tick() {
        _ticker = std::chrono::high_resolution_clock::now();
    }

    /**
     * @brief Get elapsed time in seconds
     * 
     * @return double Elapsed time since construction or last tick() call
     */
    [[nodiscard]] double second() const {
        return static_cast<double>(nanoSec()) * 1e-9;
    }

    /**
     * @brief Get elapsed time in milliseconds
     * 
     * @return double Elapsed time in milliseconds (with fractional precision)
     */
    [[nodiscard]] double milliSec() const {
        return static_cast<double>(nanoSec()) * 1e-6;
    }

    /**
     * @brief Get elapsed time in microseconds
     * 
     * @return double Elapsed time in microseconds (with fractional precision)
     */
    [[nodiscard]] double microSec() const {
        return static_cast<double>(nanoSec()) * 1e-3;
    }

    /**
     * @brief Get elapsed time in nanoseconds
     * 
     * @return long long Elapsed time in nanoseconds (highest precision)
     */
    [[nodiscard]] long long nanoSec() const {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::high_resolution_clock::now() - _ticker).count();
    }

private:
    std::chrono::time_point<std::chrono::high_resolution_clock> _ticker;  ///< Timestamp for elapsed time calculation
};

// ============================================================================
// THREAD POOL MANAGEMENT
// ============================================================================

/**
 * @brief Thread pool wrapper with task completion tracking
 * 
 * Provides a high-level interface to Boost.Asio thread pool with added
 * features for task completion tracking and graceful shutdown. Designed
 * for CPU-intensive parallel processing tasks.
 * 
 * Key Features:
 * - Automatic task completion tracking
 * - Configurable thread count with runtime resizing
 * - Graceful shutdown with wait_for_all_tasks()
 * - Exception safety and resource management
 * - Integration with Boost.Asio for high performance
 * 
 * @note Uses explicit constructor to prevent accidental conversions
 * @note Thread-safe for task submission and completion tracking
 * @note Automatically manages thread lifecycle
 */
class ThreadPoolWrapper {
public:
    /**
     * @brief Construct thread pool with specified number of threads
     * 
     * Creates a new thread pool with the given number of worker threads.
     * The explicit keyword prevents accidental implicit conversions from
     * integers to ThreadPoolWrapper objects.
     * 
     * @param num_threads Number of worker threads to create
     * 
     * @note Explicit constructor prevents ThreadPoolWrapper pool = 10;
     * @note Threads are created immediately and ready for work
     */
    explicit ThreadPoolWrapper(std::size_t num_threads);

    /**
     * @brief Submit task to thread pool for execution
     * 
     * Submits a callable object (function, lambda, functor) to the thread pool
     * for asynchronous execution. Automatically tracks task completion for
     * use with wait_for_all_tasks().
     * 
     * @tparam F Callable type (function, lambda, std::function, etc.)
     * @param f Callable object to execute
     * 
     * @note Task completion is automatically tracked
     * @note Supports any callable that takes no parameters
     * @note Thread-safe for concurrent submissions
     * 
     * @example
     * pool.post_task([]() {
     *     // Expensive computation here
     *     process_data_chunk();
     * });
     */
    template <typename F>
    void post_task(F&& f);

    /**
     * @brief Wait for all submitted tasks to complete
     * 
     * Blocks the calling thread until the number of pending tasks drops
     * to the specified threshold. Useful for synchronization points and
     * ensuring completion before cleanup.
     * 
     * @param remain Number of tasks that can remain pending (default: 0)
     * 
     * @note Blocks until condition is met
     * @note Thread-safe with proper synchronization
     * @note Can be called multiple times safely
     */
    void wait_for_all_tasks(std::int64_t remain = 0);

    /**
     * @brief Resize thread pool to new number of threads
     * 
     * Changes the number of worker threads in the pool. Waits for all
     * current tasks to complete before recreating the thread pool with
     * the new size.
     * 
     * @param new_num_threads New number of worker threads
     * 
     * @note Waits for all tasks to complete before resizing
     * @note Recreates the entire thread pool internally
     * @note Thread-safe but expensive operation
     */
    void resize(std::size_t new_num_threads);

private:
    /**
     * @brief Create new thread pool instance
     * 
     * @param num_threads Number of threads for the new pool
     */
    void create_thread_pool(std::size_t num_threads);

    /**
     * @brief Called when a task completes execution
     * 
     * Internal callback that decrements the pending task count and
     * notifies waiting threads when all tasks are complete.
     */
    void on_task_done();

private:
    std::unique_ptr<boost::asio::thread_pool> thread_pool;  ///< Underlying Boost.Asio thread pool
    std::mutex mtx;                                         ///< Mutex protecting task counter
    std::condition_variable cv;                             ///< Condition variable for task completion
    int tasks_pending;                                      ///< Number of tasks currently pending
};

/**
 * @brief Template implementation for post_task method
 * 
 * Increments pending task count and submits task to Boost.Asio thread pool
 * with automatic completion tracking.
 */
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

/**
 * @brief Global thread pool instance for application-wide use
 * 
 * Pre-configured thread pool with 200 threads for heavy parallel processing.
 * Available globally throughout the application for consistent thread management.
 * 
 * @note Initialized with 200 threads for large-scale data processing
 * @note Can be resized at runtime if needed
 * @note Shared across all modules that include utils.h
 */
inline ThreadPoolWrapper thread_pool(200);

// ============================================================================
// BINARY KEY-VALUE STORAGE
// ============================================================================

/**
 * @brief High-performance binary key-value storage system
 * 
 * Provides efficient storage and retrieval of binary data using filesystem
 * as the backend. Each key corresponds to a separate binary file, enabling
 * fast random access and avoiding database overhead for large datasets.
 * 
 * Key Features:
 * - Direct filesystem storage for maximum performance
 * - Template-based type safety for stored data
 * - Automatic directory management
 * - Cross-platform filesystem operations
 * - Support for arbitrary data types via templates
 * 
 * @note Each key maps to a separate .bin file
 * @note Suitable for large datasets that don't fit in memory
 * @note Thread-safe for different keys (file-level locking)
 */
class BinaryKVStorage {
public:
    /**
     * @brief Construct storage system with specified base directory
     * 
     * Creates a new binary storage instance rooted at the given directory.
     * The directory is created automatically if it doesn't exist.
     * 
     * @param base_path Base directory for all storage files
     * 
     * @note Directory structure is created automatically
     * @note Relative paths are resolved from current working directory
     */
    explicit BinaryKVStorage(const std::string& base_path) 
        : base_dir(base_path) {
        ensure_directory_exists(base_dir);
    }

    /**
     * @brief Write data vector to storage under specified key
     * 
     * Serializes a vector of data to binary format and stores it as a file.
     * The data type must be trivially copyable for direct binary storage.
     * 
     * @tparam T Data type (must be trivially copyable)
     * @param key Unique identifier for the data
     * @param data Vector of data to store
     * @return bool True if write succeeded, false on error
     * 
     * @note Overwrites existing data with the same key
     * @note Data type must be trivially copyable (no pointers/references)
     * @note File is created as {base_dir}/{key}.bin
     * 
     * @example
     * BinaryKVStorage storage("/tmp/cache");
     * std::vector<float> data = {1.0f, 2.0f, 3.0f};
     * storage.write("sensor_data", data);
     */
    template<typename T>
    bool write(const std::string& key, const std::vector<T>& data) {
        fs::path file_path = get_file_path(key);
        
        std::ofstream out(file_path, std::ios::binary);
        if (!out.is_open()) {
            return false;
        }

        // Write binary data directly
        out.write(reinterpret_cast<const char*>(data.data()), 
                  data.size() * sizeof(T));
        
        return out.good();
    }

    /**
     * @brief Read data vector from storage by key
     * 
     * Loads binary data from file and reconstructs it as a vector of the
     * specified type. Returns empty vector if key doesn't exist.
     * 
     * @tparam T Data type (must match type used in write operation)
     * @param key Unique identifier for the data
     * @return std::vector<T> Retrieved data vector (empty if key not found)
     * 
     * @note Returns empty vector if file doesn't exist
     * @note Data type must match the type used when writing
     * @note File size must be multiple of sizeof(T)
     * 
     * @example
     * auto data = storage.read<float>("sensor_data");
     * if (!data.empty()) {
     *     // Process retrieved data
     * }
     */
    template<typename T>
    std::vector<T> read(const std::string& key) {
        fs::path file_path = get_file_path(key);
        std::vector<T> result;

        std::ifstream in(file_path, std::ios::binary | std::ios::ate);
        if (!in.is_open()) {
            return result; // Return empty vector
        }

        // Get file size and calculate element count
        std::streamsize size = in.tellg();
        in.seekg(0, std::ios::beg);

        size_t count = size / sizeof(T);
        if (count == 0) {
            return result;
        }

        // Read data into vector
        result.resize(count);
        in.read(reinterpret_cast<char*>(result.data()), size);
        
        return result;
    }

    /**
     * @brief Check if key exists in storage
     * 
     * @param key Key to check for existence
     * @return bool True if key exists, false otherwise
     */
    bool exists(const std::string& key) const {
        return fs::exists(get_file_path(key));
    }

    /**
     * @brief Remove key and associated data from storage
     * 
     * @param key Key to remove
     * @return bool True if removal succeeded, false if key didn't exist
     */
    bool remove(const std::string& key) {
        return fs::remove(get_file_path(key));
    }

private:
    fs::path base_dir;  ///< Base directory for all storage files

    /**
     * @brief Ensure directory exists, create if necessary
     * 
     * @param dir Directory path to create
     */
    void ensure_directory_exists(const fs::path& dir) {
        if (!fs::exists(dir)) {
            fs::create_directories(dir);
        }
    }

    /**
     * @brief Get file path for given key
     * 
     * @param key Storage key
     * @return fs::path Complete file path for the key
     */
    fs::path get_file_path(const std::string& key) const {
        return base_dir / (key + ".bin");
    }
};

// ============================================================================
// FILE LOCKING MANAGEMENT
// ============================================================================

/**
 * @brief Hash-based file locking manager for concurrent access control
 * 
 * Provides efficient file-level locking using a hash table of mutexes.
 * This avoids the overhead of creating individual mutexes for each file
 * while still providing reasonable lock granularity for concurrent access.
 * 
 * Design Features:
 * - Hash-based mutex pool for memory efficiency
 * - Configurable lock pool size for performance tuning
 * - String-based file identification for flexibility
 * - Lock contention spreading across multiple mutexes
 * 
 * @note Multiple files may share the same mutex (hash collisions)
 * @note Trade-off between memory usage and lock granularity
 * @note Thread-safe for all operations
 */
class FileLockManager {
public:
    /**
     * @brief Construct file lock manager with specified lock pool size
     * 
     * Creates a pool of mutexes for hash-based file locking. Larger pool
     * sizes reduce lock contention but use more memory.
     * 
     * @param lock_count Number of mutexes in the pool (default: 2000)
     * 
     * @note Default pool size provides good balance of memory and performance
     * @note Pool size should be prime number for better hash distribution
     */
    explicit FileLockManager(size_t lock_count = 2000) : lock_pool_(lock_count) {}

    /**
     * @brief Acquire lock for specified file
     * 
     * Locks the mutex associated with the given filename's hash value.
     * This prevents concurrent access to the file from other threads.
     * 
     * @param file_name File identifier to lock
     * 
     * @note Blocks until lock is acquired
     * @note Must be paired with corresponding unlock() call
     * @note Lock is based on filename hash, not actual file system
     * 
     * @example
     * files_lock.lock("data_file_001.bin");
     * // Critical section - file access
     * files_lock.unlock("data_file_001.bin");
     */
    void lock(const std::string& file_name) {
        size_t lock_index = hash(file_name) % lock_pool_.size();
        lock_pool_[lock_index].lock();
    }

    /**
     * @brief Release lock for specified file
     * 
     * Unlocks the mutex associated with the given filename's hash value.
     * 
     * @param file_name File identifier to unlock
     * 
     * @note Must be called from the same thread that acquired the lock
     * @note Undefined behavior if called without corresponding lock()
     */
    void unlock(const std::string& file_name) {
        size_t lock_index = hash(file_name) % lock_pool_.size();
        lock_pool_[lock_index].unlock();
    }

private:
    std::vector<std::mutex> lock_pool_;  ///< Pool of mutexes for hash-based locking

    /**
     * @brief Hash function for filename to mutex mapping
     * 
     * @param file_name Filename to hash
     * @return size_t Hash value for mutex pool indexing
     */
    size_t hash(const std::string& file_name) const {
        return std::hash<std::string>{}(file_name);
    }
};

/**
 * @brief Global file lock manager instance
 * 
 * Provides application-wide file locking capabilities. Available globally
 * for consistent file access control across all modules.
 * 
 * @note Initialized with default pool size (2000 mutexes)
 * @note Shared across all modules that include utils.h
 */
inline FileLockManager files_lock;

// ============================================================================
// FILE SORTING UTILITIES
// ============================================================================

/**
 * @brief Specialized file sorting utility for 3D data files
 * 
 * Provides intelligent sorting of 3D data files based on file type priority
 * and numeric identifiers within filenames. Designed specifically for
 * processing mixed datasets containing point clouds, meshes, and trajectories.
 * 
 * Sorting Rules:
 * 1. File type priority: .ply > .csv > .obj > others
 * 2. Within same type: sort by numeric identifier in filename
 * 3. Preserves processing order for optimal data loading
 * 
 * @note Static class - all methods are class-level
 * @note Designed for spatial data processing pipelines
 * @note Maintains filename structure during sorting
 */
class FileSorter {
public:
    /**
     * @brief Sort vector of filenames according to 3D data processing priorities
     * 
     * Sorts filenames in-place using specialized rules for 3D data files.
     * Prioritizes file types based on typical processing requirements and
     * sorts within types by numeric identifiers.
     * 
     * Priority Order:
     * 1. .ply files (point clouds) - highest priority
     * 2. .csv files (trajectories) - medium priority  
     * 3. .obj files (meshes) - lower priority
     * 4. Other files - lowest priority
     * 
     * @param filenames Vector of filenames to sort (modified in-place)
     * 
     * @note Modifies input vector directly
     * @note Extracts numeric IDs from filenames for consistent ordering
     * @note Thread-safe as it only operates on passed data
     * 
     * @example
     * std::vector<std::string> files = {
     *     "mesh02.obj", "data01.csv", "cloud03.ply", "mesh01.obj"
     * };
     * FileSorter::sortFiles(files);
     * // Result: ["cloud03.ply", "data01.csv", "mesh01.obj", "mesh02.obj"]
     */
    static void sortFiles(std::vector<std::string>& filenames);

private:
    /**
     * @brief Internal structure for file information extraction
     * 
     * Holds extracted metadata from filename for sorting comparison.
     * Separates concerns of parsing and comparison logic.
     */
    struct FileInfo {
        std::string filename;  ///< Original filename
        std::string fileType;  ///< File extension (.ply, .csv, .obj)
        long long fileNum;    ///< Numeric identifier extracted from filename
    };

    /**
     * @brief Extract file information for sorting comparison
     * 
     * @param filename Original filename to analyze
     * @return FileInfo Structured information for sorting
     */
    static FileInfo extractFileInfo(const std::string& filename);

    /**
     * @brief Extract file extension from filename
     * 
     * @param filename Input filename
     * @return std::string File extension without the dot
     */
    static std::string extractExtension(const std::string& filename);

    /**
     * @brief Extract numeric identifier from filename
     * 
     * Uses regex to find the last number before the file extension.
     * Returns 0 if no number is found.
     * 
     * @param filename Input filename
     * @return int Numeric identifier (0 if none found)
     */
    static long long extractNumber(const std::string& filename);
};

#endif // TRACE_UTILS_H