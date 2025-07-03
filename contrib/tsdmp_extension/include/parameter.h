#ifndef TSDMP_PARAMETER_H
#define TSDMP_PARAMETER_H

#include <string>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <unordered_map>

inline int max_concurrent_tasks_for_read_bound_task = 80;
inline int max_concurrent_tasks_for_count_task = 80;

inline int chunk_max_level = 6;

inline std::string data_dir = "/home/zyl/zhangteng/TSDMP_buffer/data_buffer";

typedef std::int16_t user_id_t;
typedef std::int32_t file_id_t;
typedef std::int32_t point_id_t;
typedef std::int16_t foreign_key_id_t;
typedef float point_time_t;

// Global variables
extern std::uint64_t max_point_limit;

#endif // TSDMP_PARAMETER_H
