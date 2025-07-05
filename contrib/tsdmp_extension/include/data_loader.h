#ifndef TSDMP_DATA_LOADER_H
#define TSDMP_DATA_LOADER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <mutex>

// Forward declarations
class Bounds;
struct SpatioTemporalData;

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

#endif // TSDMP_DATA_LOADER_H