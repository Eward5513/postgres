// DataLoader implementation - simplified version focused on data loading functionality only
// All complex indexing, sampling, and processing functions have been removed

#include "../include/safe_header.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <stdexcept>

#include "../include/data_loader.h"

using std::string;
using std::vector;

/**
 * @brief Constructor for DataLoader class
 * 
 * Initializes a new DataLoader instance with the specified configuration parameters.
 * The constructor only sets up the basic configuration - actual data loading occurs
 * when load_data() is called.
 * 
 * @param directory The directory path containing data files to be loaded
 * @param max_file_num Maximum number of files to process (0 means no limit)
 * @param sample_ratio Sampling ratio for data reduction (0.0 to 1.0)
 * 
 * @note The constructor only initializes configuration parameters. 
 *       Actual data loading begins when load_data() is called.
 */
DataLoader::DataLoader(const string& directory, int max_file_num, float sample_ratio)
    : original_directory(directory), max_file_num(max_file_num), sample_ratio(sample_ratio)
{
}

/**
 * @brief Destructor for DataLoader class
 * 
 * Cleans up resources and performs necessary cleanup operations.
 * Currently performs default cleanup as most resources are managed
 * by RAII containers.
 */
DataLoader::~DataLoader()
{
}

/**
 * @brief Main entry point for data loading and processing pipeline
 * 
 * This function processes the provided array of filenames to calculate
 * spatial bounds for each file. The file discovery is handled externally,
 * allowing for more flexible file management.
 * 
 * @param filenames Vector of file paths to process
 * @return SimpleBounds Overall bounds calculated from all processed files
 * 
 * @note Uses concurrent processing with std::thread for optimal performance
 * @throws May throw exceptions from file I/O operations
 */
SimpleBounds DataLoader::load_data(const std::vector<std::string>& filenames)
{
    elog(INFO, "DataLoader::load_data - Processing %zu provided files", filenames.size());
    
    // Create SimpleBounds array for each thread to work on
    std::vector<SimpleBounds> file_bounds_array(filenames.size());
    
    // Concurrent processing of data files without locks
    std::vector<std::thread> worker_threads;
    std::atomic<int> processed_count(0);
    std::mutex progress_mutex;  // Only for logging progress
    
    for (size_t i = 0; i < filenames.size(); ++i) {
        worker_threads.emplace_back([this, &filenames, &file_bounds_array, i, &processed_count, &progress_mutex]() {
            try {
                // Each thread modifies its own SimpleBounds reference
                this->calculate_bound(filenames[i], file_bounds_array[i]);
                
                int current_count = ++processed_count;
                std::lock_guard<std::mutex> lock(progress_mutex);
                elog(INFO, "Processed file %zu/%zu: %s", 
                     current_count, filenames.size(), filenames[i].c_str());
            } catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(progress_mutex);
                elog(ERROR, "Failed to process file %s: %s", filenames[i].c_str(), e.what());
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& thread : worker_threads) {
        thread.join();
    }
    
    elog(INFO, "Completed concurrent processing of %d files", processed_count.load());
    
    // Store filenames for internal reference
    this->filenames = filenames;
    
    // Calculate final bounds by iterating through the SimpleBounds array
    SimpleBounds final_bounds;
    bool first_valid_bounds = true;
    
    for (size_t i = 0; i < file_bounds_array.size(); ++i) {
        const SimpleBounds& bounds = file_bounds_array[i];
        
        // Check if this file has valid bounds (non-zero values indicate valid data)
        bool has_valid_bounds = (bounds.min_x != 0.0f || bounds.max_x != 0.0f || 
                                bounds.min_y != 0.0f || bounds.max_y != 0.0f ||
                                bounds.min_z != 0.0f || bounds.max_z != 0.0f);
        
        if (has_valid_bounds) {
            if (first_valid_bounds) {
                // Initialize final bounds with first valid file bounds
                final_bounds = bounds;
                first_valid_bounds = false;
            } else {
                // Update final bounds with this file's bounds
                final_bounds.min_x = std::min(final_bounds.min_x, bounds.min_x);
                final_bounds.min_y = std::min(final_bounds.min_y, bounds.min_y);
                final_bounds.min_z = std::min(final_bounds.min_z, bounds.min_z);
                final_bounds.min_time = std::min(final_bounds.min_time, bounds.min_time);
                
                final_bounds.max_x = std::max(final_bounds.max_x, bounds.max_x);
                final_bounds.max_y = std::max(final_bounds.max_y, bounds.max_y);
                final_bounds.max_z = std::max(final_bounds.max_z, bounds.max_z);
                final_bounds.max_time = std::max(final_bounds.max_time, bounds.max_time);
            }
        }
    }
    
    if (first_valid_bounds) {
        elog(WARNING, "No valid boundary data found in any files");
        return SimpleBounds();  // Return default bounds
    }
    
    elog(INFO, "Final bounds: Longitude[%.6f-%.6f], Latitude[%.6f-%.6f], Altitude[%.3f-%.3f], Time[%.6f-%.6f]",
         final_bounds.min_x, final_bounds.max_x,
         final_bounds.min_y, final_bounds.max_y,
         final_bounds.min_z, final_bounds.max_z,
         final_bounds.min_time, final_bounds.max_time);
    
    return final_bounds;
}


/**
 * @brief Calculate spatial bounds for a data file
 * 
 * Reads the data file and calculates the minimum and maximum values for
 * longitude, latitude, and altitude. Each line contains three comma-separated
 * numbers: longitude, latitude, altitude
 * 
 * @param filename Path to the data file to process
 * @param bounds Reference to bounds structure to update with calculated bounds
 */
void DataLoader::calculate_bound(const std::string& filename, SimpleBounds& bounds)
{
    elog(INFO, "Calculating bounds for file: %s", filename.c_str());
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        elog(ERROR, "Failed to open data file: %s", filename.c_str());
        return;
    }
    
    std::string line;
    bool first_point = true;
    int line_count = 0;
    int valid_points = 0;
    
    // Read data lines (no header)
    while (std::getline(file, line)) {
        line_count++;
        
        // Skip empty lines
        if (line.empty()) continue;
        
        // Parse comma-separated line: longitude,latitude,altitude
        std::stringstream ss(line);
        std::vector<float> values;
        std::string token;
        
        // Split by comma
        while (std::getline(ss, token, ',')) {
            try {
                // Remove any leading/trailing whitespace
                token.erase(0, token.find_first_not_of(" \t\r\n"));
                token.erase(token.find_last_not_of(" \t\r\n") + 1);
                
                if (!token.empty()) {
                    values.push_back(std::stof(token));
                }
            } catch (const std::exception& e) {
                // Skip invalid tokens
                continue;
            }
        }
        
        // Need exactly 3 values: longitude, latitude, altitude
        if (values.size() != 3) {
            elog(WARNING, "Invalid data format at line %d in file %s: expected 3 values, got %zu", 
                 line_count, filename.c_str(), values.size());
            continue;
        }
        
        float longitude = values[0];  // x coordinate
        float latitude = values[1];   // y coordinate
        float altitude = values[2];   // z coordinate
        float time = 0.0f;           // Default time value since not provided
        
        if (first_point) {
            // Initialize bounds with first valid point
            bounds.min_x = bounds.max_x = longitude;
            bounds.min_y = bounds.max_y = latitude;
            bounds.min_z = bounds.max_z = altitude;
            bounds.min_time = bounds.max_time = time;
            first_point = false;
        } else {
            // Update bounds
            bounds.min_x = std::min(bounds.min_x, longitude);
            bounds.min_y = std::min(bounds.min_y, latitude);
            bounds.min_z = std::min(bounds.min_z, altitude);
            bounds.min_time = std::min(bounds.min_time, time);
            
            bounds.max_x = std::max(bounds.max_x, longitude);
            bounds.max_y = std::max(bounds.max_y, latitude);
            bounds.max_z = std::max(bounds.max_z, altitude);
            bounds.max_time = std::max(bounds.max_time, time);
        }
        
        valid_points++;
    }
    
    file.close();
    
    if (valid_points == 0) {
        elog(WARNING, "No valid points found in file: %s", filename.c_str());
        return;
    }
    
    elog(INFO, "File %s bounds: Longitude[%.6f-%.6f], Latitude[%.6f-%.6f], Altitude[%.3f-%.3f], Points: %d",
         filename.c_str(),
         bounds.min_x, bounds.max_x,  // longitude range
         bounds.min_y, bounds.max_y,  // latitude range
         bounds.min_z, bounds.max_z,  // altitude range
         valid_points);
}