#ifndef TSDMP_PARAMETER_H
#define TSDMP_PARAMETER_H

#include <string>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <unordered_map>

inline int max_concurrent_tasks_for_read_bound_task = 80;
inline int max_concurrent_tasks_for_count_task = 80;
inline int max_concurrent_task_across_chunk = 40;

inline int chunk_max_level = 6;
inline int max_point_per_chunk = 100000;

inline std::string data_dir = "/home/zyl/zhangteng/TSDMP_buffer/data_buffer";

typedef std::int16_t user_id_t;
typedef std::int32_t file_id_t;
typedef std::int32_t point_id_t;
typedef std::int16_t foreign_key_id_t;
typedef float point_time_t;

// Global variables
inline int max_point_limit = 1e6;

#endif // TSDMP_PARAMETER_H
