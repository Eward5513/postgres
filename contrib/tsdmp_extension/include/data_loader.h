#ifndef TSDMP_DATA_LOADER_H
#define TSDMP_DATA_LOADER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <fstream>
#include <filesystem>
#include "utils.h"

// Add using declarations for commonly used types
using std::vector;
namespace fs = std::filesystem;

// Forward declarations
class Bounds;
struct SpatioTemporalData;
class OctreeNode;

class DataLoader{
    public:
    DataLoader(const std::string& directory, int max_file_num, float sample_ratio);
    ~DataLoader();
    std::vector<std::string> load_data();

    private:
    void load_data_source_files();
    void build_index();
    void para_bound_and_sample();
    void para_sort_sample_file(const Bounds& bounds);
    void merge_sample_data();
    uint64_t Sequential_domerge(uint64_t cur_iter_id, bool sample_or_original);
    Bounds calculate_bound_and_sampleing(const std::string& filename, int32_t fid, int16_t user_id);
    void sort_sample_file(int out_file_id, const std::string& filename, const Bounds& bounds, std::vector<uint32_t>& sampleCells);
    void write_sample_to_file(std::unordered_map<int64_t, std::vector<struct SpatioTemporalData>>& cellSamplePoint, int file_id);
    
    // Member variables
    std::string directory;
    int max_file_num;
    float sample_ratio;
    std::vector<std::string> filenames;
    std::unordered_map<std::string, double> build_time;
    
    // Statistics and synchronization
    std::atomic<std::uint64_t> bounded_data_size{0};
    std::atomic<std::uint64_t> sample_sort_count{0};
    std::atomic<std::uint64_t> sample_sorted_file_count{0};
    std::mutex file_id_mutex;
    std::mutex count_in_cell_mutex;
};
class BuildChunk
{
public:
ThreadPoolWrapper do_indexing_thread_pool;
    BuildChunk(int thread_num):do_indexing_thread_pool(thread_num){
        
    }
    std::int64_t sample_all_size;
    std::int64_t original_all_size;
    std::ifstream sample_file;
    std::ifstream original_file;
    std::vector<__uint32_t> sample_data_index;
    std::vector<std::vector<__uint64_t>> cell_num_in_diff_level;
    std::vector<std::vector<__uint32_t>> sample_cell_num_in_diff_level;
    vector<uint32_t> sampleCells;
    vector<uint64_t> originalCells;
    int node_count = 0;
    long long split_sample_count = 0;
    long long split_original_count = 0;
    void load_sample_size();
    // const static size_t buffer_size = 1024;
    // std::vector<SpatioTemporalData> buffer;
    OctreeNode *build_chunk_node_sample(Bounds bounds, uint64_t level, uint64_t x, uint64_t y, uint64_t z);
    void split_original_data(Bounds bounds, uint64_t level, uint64_t x, uint64_t y, uint64_t z);
    void load_original_and_sample_size();
};

class BinaryKVStorage {
    private:
        fs::path base_dir;
    
        // 确保目录存在
        void ensure_directory_exists(const fs::path& dir) {
            if (!fs::exists(dir)) {
                fs::create_directories(dir);
            }
        }
    
        // 获取key对应的文件路径
        fs::path get_file_path(const std::string& key) const {
            return base_dir / (key + ".bin");
        }
    
    public:
        // 构造函数，设置公共文件夹路径
        explicit BinaryKVStorage(const std::string& base_path) 
            : base_dir(base_path) {
            ensure_directory_exists(base_dir);
        }
    
        // 写入数据
        template<typename T>
        bool write(const std::string& key, const std::vector<T>& data) {
            fs::path file_path = get_file_path(key);
            
            std::ofstream out(file_path, std::ios::binary);
            if (!out.is_open()) {
                return false;
            }
    
            // 写入数据
            out.write(reinterpret_cast<const char*>(data.data()), 
                        data.size() * sizeof(T));
            
            return out.good();
        }
        // 读取数据
        template<typename T>
        std::vector<T> read(const std::string& key) {
            fs::path file_path = get_file_path(key);
            std::vector<T> result;
    
            std::ifstream in(file_path, std::ios::binary | std::ios::ate);
            if (!in.is_open()) {
                return result; // 返回空vector
            }
    
            // 获取文件大小
            std::streamsize size = in.tellg();
            in.seekg(0, std::ios::beg);
    
            // 计算元素数量
            size_t count = size / sizeof(T);
            if (count == 0) {
                return result;
            }
    
            // 读取数据
            result.resize(count);
            in.read(reinterpret_cast<char*>(result.data()), size);
            
            return result;
        }
    
        // 检查key是否存在
        bool exists(const std::string& key) const {
            return fs::exists(get_file_path(key));
        }
    
        // 删除key对应的文件
        bool remove(const std::string& key) {
            
            return fs::remove(get_file_path(key));
        }
    };

// Global function declarations
OctreeNode *building_octree_bottom_up_top_down(Bounds bounds);
void chunk_original_data_to_db(int chunk_id, std::vector<SpatioTemporalData> all_data);

#endif // TSDMP_DATA_LOADER_H