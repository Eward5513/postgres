#ifndef TRACE_DATA_LOADER_H
#define TRACE_DATA_LOADER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <thread>
#include "utils.h"
#include "spatiotemporal_data.h"

// Add using declarations for commonly used types
using std::vector;
namespace fs = std::filesystem;

class OctreeNode;

/**
 * @brief Structure to hold processing results from calculate_bound_and_sampleing
 * 
 * This structure separates data processing from database operations to enable
 * thread-safe parallel processing with serial database writes.
 */
struct SampleResult {
    Bounds bounds;                                    // Spatial bounds of the processed data
    std::vector<std::vector<int32_t>> connections;   // Mesh face connections (for OBJ files)
    int32_t mesh_id;                                 // Mesh identifier for database writing
    
    SampleResult() : mesh_id(-1) {}              // Default constructor
    SampleResult(const Bounds& b, int32_t id) : bounds(b), mesh_id(id) {}  // Constructor for non-mesh data
};

class DataLoader{
    public:
    DataLoader(const std::string& directory, int max_file_num, float sample_ratio);
    ~DataLoader();
    std::vector<std::string> load_data();

    private:
    // Core pipeline functions (ordered by call sequence)
    void load_data_source_files();
    void build_index();
    void para_bound_and_sample();
    SampleResult calculate_bound_and_sampleing(const std::string& filename, int32_t fid, int16_t user_id);
    void para_sort_sample_file(const Bounds& bounds);
    void sort_sample_file(const std::string& filename, const Bounds& bounds, std::vector<uint32_t>& sampleCells);
    void write_sample_to_file(std::unordered_map<int64_t, std::vector<struct SpatioTemporalData>>& cellSamplePoint, int file_id);
    void merge_sample_data();
    uint64_t para_domerge(uint64_t cur_iter_id, bool sample_or_original);
    uint64_t domerge(uint64_t cur_file_id, uint64_t cur_iter, uint64_t cur_file_num, const std::string& prefix);
    
    // Indexing and tree construction functions
    OctreeNode* building_octree_bottom_up_top_down(const Bounds& bounds);
    void split_data(int id, std::vector<struct DBOctreeNode>& nodes, std::vector<struct SpatioTemporalData>& data, std::unordered_map<int, std::vector<struct SpatioTemporalData>>& result);
    // void chunk_original_data_to_db(int chunk_id, std::vector<struct SpatioTemporalData> all_data);
    void chunk_original_data_to_db(struct DBOctreeNode leaf_node);
    void split_data_to_db1(std::vector<struct DBOctreeNode>&);
    
    // Helper functions for split_data_to_db1
    void sort_original_file_by_octree(const std::string& filename, class OctreePointQuery& query_utils, std::unordered_map<int,std::unordered_map<int,std::int64_t>>& block_size, std::mutex& mutex);
    void para_sort_original_file_by_octree(const std::vector<std::string>& filenames, class OctreePointQuery& query_utils, std::unordered_map<int,std::unordered_map<int,std::int64_t>>& block_size);
    
    // Member variables
    std::string directory;
    int max_file_num;
    float sample_ratio;
    std::vector<std::string> filenames;
    std::unordered_map<std::string, double> build_time;
    
    // Statistics and synchronization
    std::atomic<std::uint64_t> bounded_data_size{0};
    std::atomic<std::uint64_t> sample_sort_count{0};
    std::atomic<std::int64_t> original_sort_count{0};
    std::atomic<std::uint64_t> sample_sorted_file_count{0};
    std::mutex count_in_cell_mutex;
    
    // Thread pool for unified parallel operations
    ThreadPoolWrapper thread_pool;
};
class BuildChunk
{
public:
    BuildChunk(int thread_num):do_indexing_thread_pool(thread_num){}
    ThreadPoolWrapper do_indexing_thread_pool;
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
    void build_octree_hierarchy();
    // const static size_t buffer_size = 1024;
    // std::vector<SpatioTemporalData> buffer;
    OctreeNode *build_chunk_node_sample(Bounds bounds, uint64_t level, uint64_t x, uint64_t y, uint64_t z);
    void split_original_data(Bounds bounds, uint64_t level, uint64_t x, uint64_t y, uint64_t z);
    // void load_original_and_sample_size();

private:
    // Private member functions for spatial indexing
    void build_chunk_spatial_index(Bounds bound, int chunk_id, std::vector<SpatioTemporalData> data);
    void para_createTreeWithLeafNode(int chunk_id, const std::vector<struct SpatioTemporalData>& dataPoints, const std::vector<OctreeNode*>& leafVector);
    void buildKdTreeForLeafNode(const OctreeNode* node, const std::vector<struct SpatioTemporalData>& dataPoints, int chunk_id);
};

// Global function declarations
void chunk_original_data_to_db(int chunk_id, std::vector<SpatioTemporalData> all_data);

#endif // TRACE_DATA_LOADER_H