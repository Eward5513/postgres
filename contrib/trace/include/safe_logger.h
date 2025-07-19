#ifndef TRACE_SAFE_LOGGER_H
#define TRACE_SAFE_LOGGER_H

#include <string>
#include <mutex>
#include <iostream>
#include <fstream>

/**
 * @brief Thread-safe logger class using singleton pattern
 * 
 * This class provides thread-safe logging functionality for PostgreSQL extensions.
 * It uses a singleton pattern to ensure there's only one global instance,
 * and uses mutex to guarantee thread safety for all logging operations.
 * 
 * Features:
 * - Singleton pattern for global access
 * - Thread-safe logging with mutex protection
 * - Support for different log levels (INFO, WARNING, ERROR)
 * - Automatic timestamp and level prefixing
 * 
 * Usage:
 *   SafeLogger::getInstance().log("Processing data...");
 *   SafeLogger::getInstance().logInfo("File loaded successfully");
 *   SafeLogger::getInstance().logWarning("Memory usage high");
 *   SafeLogger::getInstance().logError("Failed to open file");
 */
class SafeLogger {
public:
    /**
     * @brief Get the singleton instance of SafeLogger
     * 
     * @return SafeLogger& Reference to the singleton instance
     */
    static SafeLogger& getInstance();

    /**
     * @brief Log a message with default INFO level
     * 
     * @param message The message to log
     */
    void log(const std::string& message);
    
    /**
     * @brief Log a message with INFO level
     * 
     * @param message The message to log
     */
    void logInfo(const std::string& message);
    
    /**
     * @brief Log a message with WARNING level
     * 
     * @param message The message to log
     */
    void logWarning(const std::string& message);
    
    /**
     * @brief Log a message with ERROR level
     * 
     * @param message The message to log
     */
    void logError(const std::string& message);
    
    /**
     * @brief Log a message with custom level
     * 
     * @param level The log level (e.g., "DEBUG", "INFO", "WARNING", "ERROR")
     * @param message The message to log
     */
    void logWithLevel(const std::string& level, const std::string& message);

private:
    /**
     * @brief Private constructor for singleton pattern
     */
    SafeLogger() = default;
    
    /**
     * @brief Private destructor
     */
    ~SafeLogger() = default;
    
    /**
     * @brief Deleted copy constructor to prevent copying
     */
    SafeLogger(const SafeLogger&) = delete;
    
    /**
     * @brief Deleted assignment operator to prevent assignment
     */
    SafeLogger& operator=(const SafeLogger&) = delete;
    
    /**
     * @brief Deleted move constructor to prevent moving
     */
    SafeLogger(SafeLogger&&) = delete;
    
    /**
     * @brief Deleted move assignment operator to prevent moving
     */
    SafeLogger& operator=(SafeLogger&&) = delete;
    
    /**
     * @brief Internal logging method with mutex protection
     * 
     * @param level The log level
     * @param message The message to log
     */
    void internalLog(const std::string& level, const std::string& message);
    
    /**
     * @brief Check and initialize log file if necessary
     * 
     * This method checks if the log file is open, and if not, initializes it
     * with proper error handling and logging. Should be called with mutex protection.
     */
     void checkLogFile();
    
    /**
     * @brief Get current timestamp as string
     * 
     * @return std::string Current timestamp in format "YYYY-MM-DD HH:MM:SS"
     */
    std::string getCurrentTimestamp();
    
    /**
     * @brief Mutex for thread safety
     */
    mutable std::mutex log_mutex_;
    
    /**
     * @brief Static log file path
     */
    static const std::string LOG_FILE_PATH;
    
    /**
     * @brief Output file stream for logging
     */
    std::ofstream log_file_;
};

/**
 * @brief Global logger instance for convenient access
 * 
 * This provides a convenient way to access the SafeLogger singleton
 * without calling getInstance() every time.
 * 
 * Usage:
 *   logger.log("message");
 *   logger.logInfo("info message");
 *   logger.logWarning("warning message");
 *   logger.logError("error message");
 */
extern SafeLogger& logger;

#endif // TRACE_SAFE_LOGGER_H 