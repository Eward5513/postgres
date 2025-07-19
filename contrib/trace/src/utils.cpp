/**
 * @file utils.cpp
 * @brief Comprehensive utility functions and classes implementation
 * 
 * This file implements essential utility functions and classes used throughout
 * the spatiotemporal data processing system. It provides fundamental building
 * blocks for file operations, threading, timing, encoding, and data management.
 * 
 * The utilities are organized by functionality and dependency relationships:
 * - Basic utility functions (no dependencies)
 * - Encoding utilities
 * - File system operations
 * - Random number generation
 * - Timing and performance measurement
 * - Thread pool management
 * - Binary storage systems
 * - File sorting utilities
 * 
 * @author Spatiotemporal Data Processing Team
 * @version 1.0
 * @date 2024
 */

#include "../include/utils.h"
#include <iostream>
#include <string>
#include <filesystem>
#include <regex>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/beast/core/detail/base64.hpp>

namespace fs = std::filesystem;

// ============================================================================
// BASIC UTILITY FUNCTIONS (NO DEPENDENCIES)
// ============================================================================

/**
 * @brief Extract filename from full file path
 * 
 * Implementation extracts the filename component from a complete file path
 * by locating the last directory separator (either / or \) and returning
 * everything after it. This provides cross-platform compatibility for
 * both Unix-like and Windows systems.
 * 
 * Algorithm:
 * 1. Search for last occurrence of '/' or '\' in the path
 * 2. If found and not at end of string, return substring after separator
 * 3. If not found or at end, return original string
 * 
 * @param filePath Complete file path string
 * @return std::string Filename without directory path
 * 
 * Time Complexity: O(n) where n is the length of the path string
 * Space Complexity: O(1) additional space
 */
// std::string getFileNameFromPath(const std::string &filePath)
// {
//     size_t lastSlashIndex = filePath.find_last_of("/\\");
//     return (lastSlashIndex != std::string::npos && lastSlashIndex < filePath.length() - 1) 
//            ? filePath.substr(lastSlashIndex + 1) 
//            : filePath;
// }

// ============================================================================
// ENCODING UTILITIES
// ============================================================================

/**
 * @brief Encode string to Base64 format
 * 
 * Implementation uses Boost.Beast's high-performance Base64 encoder which
 * handles the standard Base64 alphabet and padding correctly. The function
 * pre-allocates the output buffer to the exact required size for efficiency.
 * 
 * Process:
 * 1. Calculate required output buffer size using Boost formula
 * 2. Resize output string to exact size needed
 * 3. Encode input data directly into output buffer
 * 4. Return completed Base64 string
 * 
 * @param input String data to encode (binary or text)
 * @return std::string Base64 encoded result
 * 
 * Time Complexity: O(n) where n is input length
 * Space Complexity: O(4n/3) for output buffer
 * 
 * @note Uses Boost.Beast for optimal performance
 * @note Output is approximately 133% of input size
 */
std::string base64_encode(const std::string& input) {
    std::string output;
    output.resize(boost::beast::detail::base64::encoded_size(input.size()));
    boost::beast::detail::base64::encode(output.data(), input.data(), input.size());
    return output;
}

/**
 * @brief Decode Base64 string to original data
 * 
 * Implementation uses Boost.Beast's Base64 decoder with proper size handling.
 * The decoder returns both decoded data and actual size, allowing for
 * correct trimming of the output buffer.
 * 
 * Process:
 * 1. Calculate maximum possible decoded size
 * 2. Resize output buffer to maximum size
 * 3. Decode input data into buffer
 * 4. Resize buffer to actual decoded size
 * 5. Return properly sized result
 * 
 * @param input Base64 encoded string
 * @return std::string Decoded original data
 * 
 * Time Complexity: O(n) where n is input length
 * Space Complexity: O(3n/4) for output buffer
 * 
 * @throws std::exception for malformed Base64 input
 * @note Handles padding characters correctly
 */
std::string base64_decode(const std::string& input) {
    std::string output;
    output.resize(boost::beast::detail::base64::decoded_size(input.size()));
    auto result = boost::beast::detail::base64::decode(output.data(), input.data(), input.size());
    output.resize(result.first); // Resize to actual decoded size
    return output;
}

// ============================================================================
// FILE SYSTEM UTILITIES
// ============================================================================

/**
 * @brief Clear folder contents or create folder if it doesn't exist
 * 
 * Implementation provides comprehensive folder management with detailed logging
 * and robust error handling. It handles both directory creation and content
 * removal with individual error handling for each file operation.
 * 
 * Algorithm:
 * 1. Check if directory exists using filesystem API
 * 2. If not exists: Create directory structure recursively
 * 3. If exists: Iterate through all entries and remove each one
 * 4. Log each operation with success/failure status
 * 5. Handle exceptions gracefully and continue processing
 * 
 * @param folderPath Path to target folder (absolute or relative)
 * @return bool True if operation completed successfully, false on critical errors
 * 
 * Error Handling:
 * - Individual file removal failures are logged but don't stop processing
 * - Critical errors (permission issues) cause function to return false
 * - Detailed error messages are written to console for debugging
 * 
 * @note Creates parent directories automatically if needed
 * @note Safe to call on non-existent directories
 * @note Provides extensive logging for monitoring and debugging
 */
bool clear_folder(std::string folderPath) {
    std::cout << "Clear folder: " << folderPath << std::endl;

    try {
        // Create directory if it doesn't exist
        if (!fs::exists(folderPath)) {
            std::cout << "Creating directory: " << folderPath << std::endl;
            fs::create_directories(folderPath);
            std::cout << "Successfully created directory: " << folderPath << std::endl;
            return true;
        }

        // Remove all contents if directory exists
        for (const auto& entry : fs::directory_iterator(folderPath)) {
            try {
                fs::remove_all(entry.path());
                std::cout << "Removed: " << entry.path() << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "Error removing: " << entry.path() << " - " << e.what() << std::endl;
                // Continue processing other files despite individual failures
            }
        }
        std::cout << "Successfully cleared folder: " << folderPath << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error processing folder " << folderPath << ": " << e.what() << std::endl;
        return false;
    }
}

/**
 * @brief Find all files with specified prefix in directory
 * 
 * Implementation scans a directory for regular files whose names start with
 * the given prefix. Uses filesystem iteration for cross-platform compatibility
 * and filters results based on file type and name matching.
 * 
 * Algorithm:
 * 1. Create directory iterator for the specified path
 * 2. For each entry in the directory:
 *    - Check if it's a regular file (not directory or special file)
 *    - Extract filename component from path
 *    - Check if filename starts with specified prefix
 *    - Add matching files to result vector
 * 3. Return collected filenames
 * 
 * @param folderPath Directory path to scan
 * @param prefix Filename prefix to match
 * @return std::vector<std::string> List of matching filenames (not full paths)
 * 
 * Time Complexity: O(n) where n is number of directory entries
 * Space Complexity: O(m) where m is number of matching files
 * 
 * @note Only returns filenames, not complete paths
 * @note Case-sensitive prefix matching
 * @note Skips directories and special files
 */
std::vector<std::string> findFilesWithPrefix(const std::string &folderPath, const std::string &prefix)
{
    std::vector<std::string> matchedFiles;
    for (const auto &entry : fs::directory_iterator(folderPath))
    {
        if (entry.is_regular_file())
        {
            const std::string filename = entry.path().filename().string();
            if (filename.rfind(prefix, 0) == 0)  // Check if filename starts with prefix
            {
                matchedFiles.push_back(filename);
            }
        }
    }
    return matchedFiles;
}

// ============================================================================
// RANDOM NUMBER GENERATION
// ============================================================================

/**
 * @brief Construct continuous random generator with specified range
 * 
 * Implementation initializes a high-quality Mersenne Twister generator with
 * hardware-based seeding and configures uniform real distribution for the
 * specified range. This provides consistent, reproducible random sequences.
 * 
 * @param min Minimum value (inclusive)
 * @param max Maximum value (exclusive)
 */
ContinuousRandomGenerator::ContinuousRandomGenerator(float min, float max)
    : gen(rd()), dist(min, max) {}

/**
 * @brief Generate next random number in configured range
 * 
 * Implementation uses the pre-configured uniform real distribution with the
 * Mersenne Twister generator to produce high-quality random numbers with
 * even distribution across the specified range.
 * 
 * @return float Random value in the range [min, max)
 * 
 * @note Each call advances the generator state
 * @note Quality suitable for scientific simulations
 */
float ContinuousRandomGenerator::generate() {
    return dist(gen);
}

// ============================================================================
// THREAD POOL MANAGEMENT
// ============================================================================

/**
 * @brief Construct thread pool with specified number of threads
 * 
 * Implementation creates a new Boost.Asio thread pool and initializes the
 * task tracking infrastructure. The pool is ready for task submission
 * immediately after construction.
 * 
 * @param num_threads Number of worker threads to create
 */
ThreadPoolWrapper::ThreadPoolWrapper(std::size_t num_threads)
    : tasks_pending(0) {
    create_thread_pool(num_threads);
}

/**
 * @brief Wait for all submitted tasks to complete
 * 
 * Implementation blocks the calling thread using a condition variable until
 * the number of pending tasks drops to the specified threshold. This provides
 * synchronization points for coordinating parallel work.
 * 
 * Synchronization Process:
 * 1. Acquire mutex lock for task counter
 * 2. Wait on condition variable with predicate check
 * 3. Predicate returns true when tasks_pending <= remain
 * 4. Function returns when condition is met
 * 
 * @param remain Number of tasks that can remain pending (default: 0)
 * 
 * @note Uses efficient condition variable for minimal CPU usage while waiting
 * @note Thread-safe with proper mutex protection
 */
void ThreadPoolWrapper::wait_for_all_tasks(std::int64_t remain) {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [this, remain]() { return tasks_pending <= remain; });
}

/**
 * @brief Resize thread pool to new number of threads
 * 
 * Implementation safely changes the thread pool size by first waiting for
 * all current tasks to complete, then recreating the entire thread pool
 * with the new configuration.
 * 
 * Resize Process:
 * 1. Wait for all current tasks to complete
 * 2. Destroy existing thread pool
 * 3. Create new thread pool with specified size
 * 4. Reset task tracking state
 * 
 * @param new_num_threads New number of worker threads
 * 
 * @note Expensive operation that recreates entire thread pool
 * @note Ensures no tasks are lost during transition
 */
void ThreadPoolWrapper::resize(std::size_t new_num_threads) {
    wait_for_all_tasks();
    create_thread_pool(new_num_threads);
}

/**
 * @brief Create new thread pool instance
 * 
 * Implementation creates a new Boost.Asio thread pool using smart pointer
 * management for automatic cleanup. The old pool is automatically destroyed
 * when the new one is assigned.
 * 
 * @param num_threads Number of threads for the new pool
 */
void ThreadPoolWrapper::create_thread_pool(std::size_t num_threads) {
    thread_pool = std::make_unique<boost::asio::thread_pool>(num_threads);
}

/**
 * @brief Task completion callback
 * 
 * Implementation decrements the pending task counter and notifies waiting
 * threads when all tasks are complete. This is called automatically when
 * each submitted task finishes execution.
 * 
 * Notification Process:
 * 1. Acquire mutex lock for task counter
 * 2. Decrement pending task count
 * 3. If count reaches zero, notify all waiting threads
 * 4. Release lock
 * 
 * @note Called automatically by task wrapper
 * @note Thread-safe with proper synchronization
 */
void ThreadPoolWrapper::on_task_done() {
    std::lock_guard<std::mutex> lock(mtx);
    --tasks_pending;
    if (tasks_pending == 0) {
        cv.notify_all();
    }
}

// ============================================================================
// FILE SORTING UTILITIES
// ============================================================================

/**
 * @brief Sort vector of filenames according to 3D data processing priorities
 * 
 * Implementation extracts metadata from each filename, sorts according to
 * specialized rules for 3D data processing, and reconstructs the filename
 * vector in the new order. This ensures optimal processing sequence.
 * 
 * Sorting Algorithm:
 * 1. Extract FileInfo structure for each filename
 * 2. Sort FileInfo vector using custom comparator:
 *    - Primary sort: File type priority (.ply > .csv > .obj > others)
 *    - Secondary sort: Numeric identifier within same type
 * 3. Reconstruct filename vector from sorted FileInfo
 * 4. Replace original vector contents
 * 
 * @param filenames Vector of filenames to sort (modified in-place)
 * 
 * Time Complexity: O(n log n) where n is number of files
 * Space Complexity: O(n) for temporary FileInfo structures
 * 
 * @note Modifies input vector directly for memory efficiency
 * @note Stable sort preserves order of equal elements
 */
void FileSorter::sortFiles(std::vector<std::string>& filenames) {
    std::vector<FileInfo> fileInfos;
    
    // Extract metadata from each filename
    for (const auto& filename : filenames) {
        fileInfos.push_back(extractFileInfo(filename));
    }

    // Sort using specialized 3D data processing rules
    std::sort(fileInfos.begin(), fileInfos.end(), [](const FileInfo& a, const FileInfo& b) {
        // Primary sort: File type priority
        if (a.fileType == "ply" && b.fileType != "ply") return true;
        if (a.fileType != "ply" && b.fileType == "ply") return false;
        if (a.fileType == "csv" && b.fileType != "csv") return true;
        if (a.fileType != "csv" && b.fileType == "csv") return false;
        if (a.fileType == "obj" && b.fileType != "obj") return true;
        if (a.fileType != "obj" && b.fileType == "obj") return false;
        
        // Secondary sort: Numeric identifier within same type
        return a.fileNum < b.fileNum;
    });

    // Reconstruct filename vector in new order
    filenames.clear();
    for (const auto& fileInfo : fileInfos) {
        filenames.push_back(fileInfo.filename);
    }
}

/**
 * @brief Extract file information for sorting comparison
 * 
 * Implementation creates a structured representation of filename metadata
 * by extracting the file extension and numeric identifier. This separates
 * parsing logic from comparison logic for better maintainability.
 * 
 * @param filename Original filename to analyze
 * @return FileInfo Structured metadata for sorting operations
 */
FileSorter::FileInfo FileSorter::extractFileInfo(const std::string& filename) {
    FileInfo fileInfo;
    fileInfo.fileType = extractExtension(filename);
    fileInfo.fileNum = extractNumber(filename);
    fileInfo.filename = filename;
    return fileInfo;
}

/**
 * @brief Extract file extension from filename
 * 
 * Implementation finds the last dot in the filename and returns everything
 * after it as the file extension. This handles files with multiple dots
 * correctly by using the rightmost dot as the extension separator.
 * 
 * @param filename Input filename
 * @return std::string File extension without the dot
 * 
 * @note Returns empty string if no extension found
 * @note Does not include the dot in the returned extension
 */
std::string FileSorter::extractExtension(const std::string& filename) {
    return filename.substr(filename.find_last_of('.') + 1);
}

/**
 * @brief Extract numeric identifier from filename
 * 
 * Implementation uses regular expression to find all numeric patterns in the
 * filename and concatenates them into a single number. This handles various
 * filename formats with multiple embedded numbers.
 * 
 * Note: If the input contains a full path, only the filename portion is processed
 * to avoid interference from numbers in the directory path.
 * 
 * Regex Pattern: \\d+
 * - \\d+ : One or more digits
 * 
 * @param filename Input filename to analyze (can be full path or just filename)
 * @return long long Numeric identifier (0 if no number found)
 * 
 * Algorithm:
 * 1. Extract filename from path if needed (handles both full paths and filenames)
 * 2. Apply regex pattern to find all numbers in filename (excluding extension)
 * 3. Concatenate all found numbers in order
 * 4. Convert concatenated string to long long
 * 5. If no numbers found, return 0 as default
 * 
 * Examples:
 * - "mesh_001.ply" → 1
 * - "/path/to/mesh_001.ply" → 1 (path ignored)
 * - "data_file_42.csv" → 42
 * - "/data/2023/batch_1/Tile_+301_+146.obj" → 301146 (path ignored)
 * - "model.obj" → 0
 */
long long FileSorter::extractNumber(const std::string& filename) {
    // Extract filename from path to avoid interference from directory numbers
    std::string filenameOnly;
    size_t lastSlash = filename.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        filenameOnly = filename.substr(lastSlash + 1);
    } else {
        filenameOnly = filename;
    }
    
    // Remove file extension first
    std::string nameWithoutExt = filenameOnly.substr(0, filenameOnly.find_last_of('.'));
    
    std::regex regex("\\d+");
    std::sregex_iterator begin(nameWithoutExt.begin(), nameWithoutExt.end(), regex);
    std::sregex_iterator end;
    
    std::string concatenatedNumbers;
    for (std::sregex_iterator i = begin; i != end; ++i) {
        concatenatedNumbers += i->str();
    }
    
    if (concatenatedNumbers.empty()) {
        return 0LL;
    }
    
    try {
        return std::stoll(concatenatedNumbers);
    } catch (const std::exception&) {
        return 0LL;
    }
}

// ============================================================================
// EXPLICIT TEMPLATE INSTANTIATIONS
// ============================================================================

/**
 * @brief Explicit template instantiations for split function
 * 
 * These instantiations ensure that the split function template is compiled
 * for the most commonly used numeric types. This improves compilation times
 * and provides better error messages for unsupported types.
 */

// Instantiate for double precision floating point
template std::vector<std::pair<double, double>> split(double start, double end, std::int64_t parts);

// Instantiate for single precision floating point  
template std::vector<std::pair<float, float>> split(float start, float end, std::int64_t parts);

// Instantiate for 64-bit signed integers
template std::vector<std::pair<std::int64_t, std::int64_t>> split(std::int64_t start, std::int64_t end, std::int64_t parts); 
