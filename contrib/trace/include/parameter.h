#ifndef TRACE_PARAMETER_H
#define TRACE_PARAMETER_H

#include <string>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <thread>

inline int concurrent_task_num = std::thread::hardware_concurrency()*2;

inline int chunk_max_level = 5;
inline int max_point_per_chunk = 100;
inline int octree_max_level = 12;

inline std::string data_dir = "/home/zyl/zhangteng/TRACE_buffer/data_buffer";

typedef std::int16_t user_id_t;
typedef std::int32_t file_id_t;
typedef std::int32_t point_id_t;
typedef std::int16_t foreign_key_id_t;
typedef float point_time_t;
inline int max_point_per_leaf = 400;

// Global variables
inline int max_point_limit = 1e6;

#endif // TRACE_PARAMETER_H
