#include "../include/safe_header.h"
#include "../include/trajectory_manager.h"
#include "../include/pgutils.h"
#include "../include/safe_logger.h"
#include <cstring>

extern "C" {
#include "postgres.h"
#include "utils/elog.h"
}

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
TrajectoryManager& TrajectoryManager::getInstance() {
    static TrajectoryManager instance;
    return instance;
}

/**
 * @brief Global singleton instance for convenient access
 * 
 * Provides direct access to the TrajectoryManager singleton instance throughout
 * the application without requiring explicit getInstance() calls.
 */
TrajectoryManager& trajectoryManager = TrajectoryManager::getInstance();

void TrajectoryManager::clearTable() {
    elog(INFO, "TrajectoryManager::clearTable()");
    
    // Clear table data (table creation is handled in SQL file)
    std::string sql = "TRUNCATE TABLE " + std::string(TABLE_NAME) + ";";
    pgutils.executeSQL(sql.c_str());
}

void TrajectoryManager::writeTrajectoryToDatabase(int id, const std::vector<SpatioTemporalData>& points) {
    elog(INFO, "TrajectoryManager::writeTrajectoryToDatabase(id=%d, size=%zu)", id, points.size());
    
    // First check if the trajectory already exists
    BinarySelectResult* existing = pgutils.executeBinarySelect(TABLE_NAME, id);
    
    if (existing != NULL && existing->data != NULL) {
        // Trajectory exists, need to update
        elog(INFO, "TrajectoryManager::writeTrajectoryToDatabase: Trajectory id=%d exists, updating data", id);
        
        // Create UPDATE SQL statement
        std::string update_sql = "UPDATE " + std::string(TABLE_NAME) + " SET data = $1, updated_at = CURRENT_TIMESTAMP WHERE id = " + std::to_string(id) + ";";
        
        // Execute SQL with binary parameter
        // For now, we'll use the binary insert approach with DELETE first
        std::string delete_sql = "DELETE FROM " + std::string(TABLE_NAME) + " WHERE id = " + std::to_string(id) + ";";
        pgutils.executeSQL(delete_sql.c_str());
        
        // Clean up existing result
        if (existing->data) {
            pfree(existing->data);
        }
        pfree(existing);
    } else {
        elog(INFO, "TrajectoryManager::writeTrajectoryToDatabase: Trajectory id=%d does not exist, inserting new data", id);
    }
    
    // Insert the trajectory data using binary insert
    pgutils.executeBinaryInsert(TABLE_NAME, id, points.data(), points.size() * sizeof(SpatioTemporalData));
    
    elog(INFO, "TrajectoryManager::writeTrajectoryToDatabase: Successfully inserted/updated %zu bytes for id=%d", 
         points.size() * sizeof(SpatioTemporalData), id);
}

void TrajectoryManager::bulkInsertTrajectories(const std::unordered_map<int, std::vector<SpatioTemporalData>>& trajectoryMap) {
    elog(INFO, "TrajectoryManager::bulkInsertTrajectories: Inserting %zu trajectories", trajectoryMap.size());
    
    for (const auto& [id, points] : trajectoryMap) {
        pgutils.executeBinaryInsert(TABLE_NAME, id, points.data(), points.size() * sizeof(SpatioTemporalData));
    }
    
    elog(INFO, "TrajectoryManager::bulkInsertTrajectories: Successfully inserted %zu trajectories", trajectoryMap.size());
}

Trajectory TrajectoryManager::loadTrajectoryFromDatabase(int id) {
    elog(INFO, "TrajectoryManager::loadTrajectoryFromDatabase(id=%d)", id);
    
    Trajectory result;
    result.id = id;
    
    // Use SPI interface to query binary data
    BinarySelectResult* binary_result = pgutils.executeBinarySelect(TABLE_NAME, id);
    if (binary_result != NULL && binary_result->data != NULL && binary_result->size > 0) {
        // Validate data size
        size_t point_size = sizeof(SpatioTemporalData);
        if (binary_result->size % point_size != 0) {
            ereport(ERROR, (errcode(ERRCODE_DATA_CORRUPTED),
                           errmsg("Binary data size does not match expected size for SpatioTemporalData")));
        }
        
        // Convert data
        int num_points = binary_result->size / point_size;
        const SpatioTemporalData* buffer = reinterpret_cast<const SpatioTemporalData*>(binary_result->data);
        result.points.assign(buffer, buffer + num_points);
        
        elog(INFO, "TrajectoryManager::loadTrajectoryFromDatabase: Successfully loaded %d points for id=%d", num_points, id);
        
        // Clean up memory allocated by PostgreSQL
        pfree(binary_result->data);
        pfree(binary_result);
    } else {
        ereport(ERROR, (errcode(ERRCODE_NO_DATA_FOUND),
                       errmsg("No trajectory found for the given ID: %d", id)));
    }
    
    return result;
}

std::unordered_map<int, Trajectory> TrajectoryManager::loadTrajectoriesFromDatabase(const std::vector<int>& ids) {
    elog(INFO, "TrajectoryManager::loadTrajectoriesFromDatabase: Loading %zu trajectories", ids.size());
    
    std::unordered_map<int, Trajectory> trajectoryMap;
    
    // If ids is empty, return empty map
    if (ids.empty()) {
        return trajectoryMap;
    }
    
    // Load each trajectory individually using SPI
    for (int id : ids) {
        BinarySelectResult* binary_result = pgutils.executeBinarySelect(TABLE_NAME, id);
        if (binary_result != NULL && binary_result->data != NULL && binary_result->size > 0) {
            // Validate data size
            size_t point_size = sizeof(SpatioTemporalData);
            if (binary_result->size % point_size != 0) {
                elog(WARNING, "TrajectoryManager::loadTrajectoriesFromDatabase: Invalid data size for id=%d", id);
                pfree(binary_result->data);
                pfree(binary_result);
                continue;
            }
            
            // Convert data
            int num_points = binary_result->size / point_size;
            const SpatioTemporalData* buffer = reinterpret_cast<const SpatioTemporalData*>(binary_result->data);
            
            Trajectory traj;
            traj.id = id;
            traj.points.assign(buffer, buffer + num_points);
            
            trajectoryMap[id] = std::move(traj);
            
            // Clean up memory
            pfree(binary_result->data);
            pfree(binary_result);
        } else {
            elog(WARNING, "TrajectoryManager::loadTrajectoriesFromDatabase: No data found for id=%d", id);
        }
    }
    
    elog(INFO, "TrajectoryManager::loadTrajectoriesFromDatabase: Successfully loaded %zu trajectories", trajectoryMap.size());
    return trajectoryMap;
}

std::unordered_map<int, Trajectory> TrajectoryManager::loadAllTrajectoriesFromDatabase() {
    elog(INFO, "TrajectoryManager::loadAllTrajectoriesFromDatabase: Loading all trajectories");
    
    std::unordered_map<int, Trajectory> trajectoryMap;
    
    // Query all binary data from the trajectory table
    BinarySelectAllResult* result = pgutils.executeBinarySelectAll(TABLE_NAME);
    if (result != NULL) {
        // Process each trajectory in the result set
        for (int i = 0; i < result->count; i++) {
            int id = result->keys[i];
            
            // Check if data exists and is non-empty for this key
            if (result->data_array[i] != NULL && result->size_array[i] > 0) {
                // Validate binary data size
                size_t point_size = sizeof(SpatioTemporalData);
                if (result->size_array[i] % point_size != 0) {
                    elog(WARNING, "TrajectoryManager::loadAllTrajectoriesFromDatabase: Invalid data size for id=%d", id);
                    continue;
                }
                
                // Convert data
                int num_points = result->size_array[i] / point_size;
                const SpatioTemporalData* buffer = reinterpret_cast<const SpatioTemporalData*>(result->data_array[i]);
                
                Trajectory traj;
                traj.id = id;
                traj.points.assign(buffer, buffer + num_points);
                
                trajectoryMap[id] = std::move(traj);
            } else {
                elog(WARNING, "TrajectoryManager::loadAllTrajectoriesFromDatabase: Empty data for id=%d", id);
            }
        }
        
        // Clean up memory allocated by PostgreSQL
        for (int i = 0; i < result->count; i++) {
            if (result->data_array[i] != NULL) {
                pfree(result->data_array[i]);
            }
        }
        pfree(result->keys);
        pfree(result->data_array);
        pfree(result->size_array);
        pfree(result);
    } else {
        elog(INFO, "TrajectoryManager::loadAllTrajectoriesFromDatabase: No trajectories found in database");
    }
    
    elog(INFO, "TrajectoryManager::loadAllTrajectoriesFromDatabase: Successfully loaded %zu trajectories", trajectoryMap.size());
    return trajectoryMap;
}

std::vector<int> TrajectoryManager::getAllTrajectoryIdsFromDatabase() {
    elog(INFO, "TrajectoryManager::getAllTrajectoryIdsFromDatabase: Loading all trajectory IDs");
    
    std::vector<int> trajectoryIds;
    
    // Use SPI to execute query for IDs only
    std::string sql = "SELECT id FROM " + std::string(TABLE_NAME) + ";";
    SPITupleTable* spi_result = pgutils.executeSQLSelect(sql.c_str());
    
    if (spi_result != NULL && spi_result->tupdesc != NULL && SPI_processed > 0) {
        for (uint32_t i = 0; i < SPI_processed; i++) {
            HeapTuple tuple = spi_result->vals[i];
            TupleDesc tupdesc = spi_result->tupdesc;
            
            bool isnull;
            Datum id_datum = SPI_getbinval(tuple, tupdesc, 1, &isnull);
            
            if (!isnull) {
                int id = DatumGetInt32(id_datum);
                trajectoryIds.push_back(id);
            }
        }
        
        elog(INFO, "TrajectoryManager::getAllTrajectoryIdsFromDatabase: Successfully loaded %zu trajectory IDs", trajectoryIds.size());
    } else {
        elog(WARNING, "TrajectoryManager::getAllTrajectoryIdsFromDatabase: No trajectory IDs found in database");
    }
    
    return trajectoryIds;
}
