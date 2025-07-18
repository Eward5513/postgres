#ifndef TRAJECTORY_MANAGER_H
#define TRAJECTORY_MANAGER_H

#include <vector>
#include <unordered_map>
#include "spatiotemporal_data.h"

/**
 * @brief Manager class for trajectory data operations using singleton pattern
 * 
 * This class provides comprehensive database operations for storing and retrieving
 * trajectory data in PostgreSQL database using binary storage. It uses singleton 
 * pattern to ensure global unique instance and provides thread-safe operations 
 * for trajectory data management.
 * 
 * Key features:
 * - Singleton pattern for global access and consistency
 * - Binary data storage for optimal performance
 * - Batch operations for multiple trajectories
 * - Data validation and integrity checking
 * - Comprehensive error handling and logging
 * 
 * Database schema:
 * - Table: trajectory_table
 * - Primary key: id (trajectory identifier)
 * - Data: Binary serialized SpatioTemporalData arrays
 */
class TrajectoryManager {
public:
    static constexpr const char* TABLE_NAME = "trajectory_table";
    
    /**
     * @brief Get singleton instance of TrajectoryManager
     * 
     * Implements thread-safe lazy initialization using static local variable.
     * The first call creates the instance, subsequent calls return the same instance.
     * 
     * @return TrajectoryManager& Reference to the singleton instance
     * 
     * @note Thread-safe in C++11 and later due to static local variable initialization
     * @note Memory is automatically managed by the runtime
     */
    static TrajectoryManager& getInstance();
    
    // Delete copy constructor and assignment operations to enforce singleton
    TrajectoryManager(const TrajectoryManager&) = delete;
    TrajectoryManager& operator=(const TrajectoryManager&) = delete;
    TrajectoryManager(TrajectoryManager&&) = delete;
    TrajectoryManager& operator=(TrajectoryManager&&) = delete;
    
    /**
     * @brief Clear all data from the trajectory table
     * 
     * Truncates the entire trajectory table to remove all existing data.
     * Creates the table if it doesn't exist. This operation is typically 
     * performed before loading new trajectory data to ensure a clean state.
     * 
     * Database operations:
     * - Creates table if it doesn't exist
     * - Executes TRUNCATE TABLE command for complete data removal
     * - More efficient than DELETE for removing all rows
     * 
     * @note This operation is irreversible - all trajectory data will be lost
     * @note Logs the operation for debugging and monitoring purposes
     */
    void clearTable();
    
    /**
     * @brief Write trajectory data to database
     * 
     * Stores a trajectory as binary data in the database associated with the given ID.
     * Uses INSERT...ON CONFLICT to handle duplicate IDs by updating existing data.
     * 
     * @param id Unique identifier for the trajectory
     * @param points Vector of SpatioTemporalData points representing the trajectory
     * 
     * @note Uses binary serialization for efficient storage
     * @note Handles ID conflicts by updating existing data
     * @note Transaction is automatically committed
     */
    void writeTrajectoryToDatabase(int id, const std::vector<SpatioTemporalData>& points);
    
    /**
     * @brief Batch write multiple trajectories to database
     * 
     * Efficiently stores multiple trajectories in a single transaction.
     * Each trajectory is identified by its key in the input map.
     * 
     * @param trajectoryMap Map from trajectory ID to vector of points
     * 
     * @note All insertions are performed in a single transaction for efficiency
     * @note Does not handle conflicts - assumes all IDs are new
     * @note More efficient than multiple individual writes
     */
    void bulkInsertTrajectories(const std::unordered_map<int, std::vector<SpatioTemporalData>>& trajectoryMap);
    
    /**
     * @brief Load trajectory from database by ID
     * 
     * Retrieves and deserializes trajectory data associated with the given ID.
     * The returned trajectory object includes both the ID and all points.
     * 
     * @param id Unique identifier for the trajectory
     * @return Trajectory Complete trajectory object with ID and points
     * @throws StringException if binary data size doesn't match expected size
     * @throws std::runtime_error if no trajectory found for the given ID
     */
    Trajectory loadTrajectoryFromDatabase(int id);
    
    /**
     * @brief Load multiple trajectories from database by IDs
     * 
     * Efficiently retrieves multiple trajectories in a single query using IN clause.
     * Returns a map from trajectory ID to trajectory object for easy access.
     * 
     * @param ids Vector of trajectory IDs to load
     * @return std::unordered_map<int, Trajectory> Map from ID to trajectory object
     * @throws std::runtime_error if binary data size doesn't match expected size
     * 
     * @note Returns empty map if ids vector is empty
     * @note Missing IDs are silently ignored (not included in result)
     * @note More efficient than multiple individual loads
     */
    std::unordered_map<int, Trajectory> loadTrajectoriesFromDatabase(const std::vector<int>& ids);
    
    /**
     * @brief Load all trajectories from database
     * 
     * Retrieves all trajectory data from the database and returns them
     * organized by their IDs. Useful for bulk operations or complete data export.
     * 
     * @return std::unordered_map<int, Trajectory> Map from ID to trajectory object
     * 
     * @note Corrupted data entries are skipped with error logging
     * @note May consume significant memory for large datasets
     * @note Consider using pagination for very large trajectory collections
     */
    std::unordered_map<int, Trajectory> loadAllTrajectoriesFromDatabase();
    
    /**
     * @brief Get all trajectory IDs from database
     * 
     * Retrieves only the trajectory IDs without loading the actual trajectory data.
     * Useful for getting an overview of available trajectories or for pagination.
     * 
     * @return std::vector<int> Vector of all trajectory IDs in the database
     * 
     * @note Much more efficient than loading full trajectory data
     * @note Handles database errors gracefully with error logging
     * @note Returns empty vector if no trajectories exist or on error
     */
    std::vector<int> getAllTrajectoryIdsFromDatabase();

private:
    /**
     * @brief Private constructor for singleton pattern
     * 
     * Prevents direct instantiation of the class, enforcing singleton behavior.
     * Initialization is performed automatically when getInstance() is first called.
     */
    TrajectoryManager() = default;
    
    /**
     * @brief Private destructor for singleton pattern
     * 
     * Handles cleanup of any resources when the application terminates.
     * Called automatically by the runtime for static instance cleanup.
     */
    ~TrajectoryManager() = default;
};

/**
 * @brief Global singleton instance of TrajectoryManager
 * 
 * This provides convenient global access to the trajectory manager instance
 * throughout the application, similar to other manager classes.
 * 
 * Usage:
 * ```cpp
 * trajectoryManager.writeTrajectoryToDatabase(id, points);
 * auto trajectory = trajectoryManager.loadTrajectoryFromDatabase(id);
 * ```
 */
extern TrajectoryManager& trajectoryManager;

#endif // TRAJECTORY_MANAGER_H
    