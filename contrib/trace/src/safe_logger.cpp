#include "../include/safe_logger.h"
#include "../include/parameter.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <filesystem>

/**
 * @brief Static log file path definition
 */
const std::string SafeLogger::LOG_FILE_PATH = data_dir + "/logfile";

/**
 * @brief Global logger instance - references the singleton
 * 
 * This global variable provides convenient access to the SafeLogger singleton.
 * It's initialized when the module is loaded, ensuring the singleton is created early.
 */
SafeLogger& logger = SafeLogger::getInstance();

/**
 * @brief Get the singleton instance of SafeLogger
 * 
 * This method implements the singleton pattern using Meyer's singleton,
 * which is thread-safe in C++11 and later due to the guarantee that
 * static local variables are initialized in a thread-safe manner.
 * 
 * @return SafeLogger& Reference to the singleton instance
 */
SafeLogger& SafeLogger::getInstance() {
    static SafeLogger instance;
    
    // Initialize log file if not already done
    if (!instance.log_file_.is_open()) {
        std::lock_guard<std::mutex> lock(instance.log_mutex_);
        
        // Double check after acquiring lock
        if (!instance.log_file_.is_open()) {
            // Create directory if it doesn't exist
            std::filesystem::path filePath(LOG_FILE_PATH);
            std::filesystem::path dirPath = filePath.parent_path();
            
            if (!dirPath.empty() && !std::filesystem::exists(dirPath)) {
                std::filesystem::create_directories(dirPath);
            }
            
            // Open the log file for writing (append mode)
            instance.log_file_.open(LOG_FILE_PATH, std::ios::app);
            
            if (instance.log_file_.is_open()) {
                // Write initialization message
                std::string timestamp = instance.getCurrentTimestamp();
                instance.log_file_ << "[" << timestamp << "] [INFO] SafeLogger initialized with log file: " 
                                  << LOG_FILE_PATH << std::endl;
                instance.log_file_.flush();
            }
        }
    }
    
    return instance;
}

/**
 * @brief Log a message with default INFO level
 * 
 * @param message The message to log
 */
void SafeLogger::log(const std::string& message) {
    logInfo(message);
}

/**
 * @brief Log a message with INFO level
 * 
 * @param message The message to log
 */
void SafeLogger::logInfo(const std::string& message) {
    internalLog("INFO", message);
}

/**
 * @brief Log a message with WARNING level
 * 
 * @param message The message to log
 */
void SafeLogger::logWarning(const std::string& message) {
    internalLog("WARNING", message);
}

/**
 * @brief Log a message with ERROR level
 * 
 * @param message The message to log
 */
void SafeLogger::logError(const std::string& message) {
    internalLog("ERROR", message);
}

/**
 * @brief Log a message with custom level
 * 
 * @param level The log level (e.g., "DEBUG", "INFO", "WARNING", "ERROR")
 * @param message The message to log
 */
void SafeLogger::logWithLevel(const std::string& level, const std::string& message) {
    internalLog(level, message);
}

/**
 * @brief Internal logging method with mutex protection
 * 
 * This method is the core logging function that ensures thread safety
 * by using a mutex to protect the output stream. It formats the log
 * message with timestamp and level information.
 * 
 * @param level The log level
 * @param message The message to log
 */
void SafeLogger::internalLog(const std::string& level, const std::string& message) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    std::string timestamp = getCurrentTimestamp();
    
    if (log_file_.is_open()) {
        log_file_ << "[" << timestamp << "] [" << level << "] " << message << std::endl;
        log_file_.flush(); // Ensure immediate write to file
    } else {
        // Fallback to console if file not available
        std::cout << "[" << timestamp << "] [" << level << "] " << message << std::endl;
    }
}

/**
 * @brief Get current timestamp as string
 * 
 * This method generates a formatted timestamp string in the format
 * "YYYY-MM-DD HH:MM:SS" using the current system time.
 * 
 * @return std::string Current timestamp in format "YYYY-MM-DD HH:MM:SS"
 */
std::string SafeLogger::getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time_t);
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
} 