#ifndef TRACE_DATA_LOADER_H
#define TRACE_DATA_LOADER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include "trace.h"

// Add using declarations for commonly used types
using std::vector;
namespace fs = std::filesystem;

/**
 * @brief DataLoader class for loading spatiotemporal data files
 * 
 * Simplified version that only handles data loading functionality.
 * All indexing and complex processing functions have been removed.
 */
class DataLoader{
public:
    /**
     * @brief Constructor for DataLoader
     * @param directory Directory containing data files
     * @param max_file_num Maximum number of files to process
     * @param sample_ratio Sampling ratio for data loading
     */
    DataLoader(const std::string& directory, int max_file_num, float sample_ratio);
    
    /**
     * @brief Destructor
     */
    ~DataLoader();
    
    /**
     * @brief Load data from specified filenames
     * @param filenames Vector of filenames to load
     * @return SimpleBounds Overall bounds of loaded data
     */
    SimpleBounds load_data(const std::vector<std::string>& filenames);

private:
    /**
     * @brief Calculate spatial bounds for a single file
     * @param filename File to process
     * @param bounds Reference to bounds structure to update
     */
    void calculate_bound(const std::string& filename, SimpleBounds& bounds);
    
    // Core member variables needed for data loading
    std::string original_directory;   ///< Source directory path
    int max_file_num;                ///< Maximum files to process
    float sample_ratio;              ///< Data sampling ratio
    std::vector<std::string> filenames; ///< List of data files
    std::unordered_map<std::string, double> build_time; ///< Timing information
};

#endif // TRACE_DATA_LOADER_H