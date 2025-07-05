// C++ standard library headers - included first to avoid macro conflicts
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <random>
#include <fstream>
#include <thread>
#include <mutex>
#include <regex>
#include <stdexcept>
#include <nlohmann/json.hpp>

// Only include necessary PostgreSQL headers
extern "C" {
#include "postgres.h"
#include "utils/elog.h"
}

// Project headers
#include "../include/data_loader.h"
#include "../include/parameter.h"
#include "../include/mesh_connection_manager.h"
#include "../include/octree_node_manager.h"
#include "../include/octree_node.h"
#include "../include/kdtree_node_manager.h"
#include "../include/userdata_manager.h"
#include "../include/original_data_manager.h"
#include "../include/utils.h"
#include "../include/safe_logger.h"
#include "file_sorter.h"

using namespace std;
using json = nlohmann::json;

namespace fs = filesystem;

/**
 * @brief Constructor for DataLoader class
 * 
 * Initializes a DataLoader instance with the specified parameters for loading
 * and processing spatiotemporal data files.
 * 
 * @param directory The directory path containing data files to be loaded
 * @param max_file_num Maximum number of files to process (0 means no limit)
 * @param sample_ratio Sampling ratio for data reduction (0.0 to 1.0)
 */
DataLoader::DataLoader(const string& directory, int max_file_num, float sample_ratio)
    : directory(directory), max_file_num(max_file_num), sample_ratio(sample_ratio)
{
}

/**
 * @brief Destructor for DataLoader class
 * 
 * Cleans up resources and performs necessary cleanup operations.
 */
DataLoader::~DataLoader()
{
}

/**
 * @brief Load and prepare data source files for processing
 * 
 * This function scans the specified directory to collect all data files,
 * performs file sorting and shuffling operations, and limits the number
 * of files to process based on the max_file_num parameter.
 * 
 * Processing steps:
 * 1. Scan directory for all files
 * 2. Sort filenames alphabetically
 * 3. Shuffle filenames with a fixed seed for reproducibility
 * 4. Apply custom file sorting using FileSorter
 * 5. Limit the number of files if max_file_num is specified
 * 
 * @note Files are shuffled with seed 42 to ensure reproducible results
 * @note Supports .ply, .csv, and .obj file formats
 */
void DataLoader::load_data_source_files()
{
    // int mesh_count = 0;
    // int pointcloud_count = 0;
    // int trajectory_count = 0;
    vector<string> filenames;
    for (const auto &entry : fs::directory_iterator(this->directory))
    {
        const auto &path = entry.path();
        auto filename = path.filename().string();
        filenames.push_back(this->directory + "/" + path.filename().string());
        // if ((trajectory_count < 100 && filename.substr(filename.length() - 3) == "csv") ||
        // (mesh_count < 100 && filename.substr(filename.length() - 3) == "obj") ||
        // (pointcloud_count < 100 && filename.substr(filename.length() - 3) == "ply")){
        //     if (filename.substr(filename.length() - 3) == "csv"){
        //         ++trajectory_count;
        //     }
        //     else if (filename.substr(filename.length() - 3) == "obj"){
        //         ++mesh_count;
        //     }
        //     else if (filename.substr(filename.length() - 3) == "ply"){
        //         ++pointcloud_count;
        //     }
        //     filenames.push_back(sourceDir + "/" + filename);
        // }
    }
    sort(filenames.begin(),filenames.end());
    mt19937 g(42);
    shuffle(filenames.begin(), filenames.end(), g);
    elog(INFO, "sorting files");
    FileSorter::sortFiles(filenames);
    elog(INFO, "sorted files");
    if(filenames.size() > this->max_file_num){
        filenames.resize(this->max_file_num);
    }
    this->filenames = filenames;
}

/**
 * @brief Main function to load and process all data files
 * 
 * This is the primary entry point for data loading and processing. It coordinates
 * the entire data loading pipeline including file discovery, metadata creation,
 * database cleanup, and spatial index construction.
 * 
 * Processing pipeline:
 * 1. Load and prepare source files from directory
 * 2. Create file-to-ID mapping and save as JSON metadata
 * 3. Clear existing database tables to ensure clean state
 * 4. Build spatial indexes for efficient querying
 * 
 * @return vector<string> List of processed filenames
 * 
 * @note Creates a files_user.json file containing file-to-ID mappings
 * @note Clears all existing spatial index tables before processing
 * @throws May throw exceptions from underlying database operations
 */
vector<string> DataLoader::load_data()
{
    this->load_data_source_files();
    json users_json;
    for (size_t i = 0; i < filenames.size(); i++)
    {
        users_json[filenames[i]] = i;
    }
    ofstream out_file(data_dir + "/files_user.json");
    out_file << users_json.dump(10); // Formatted output
    out_file.close();

    elog(INFO, "loaded %zu files from directory '%s'", filenames.size(), this->directory.c_str());

    elog(INFO, "clear index table");
    OctreeNodeManager::clearTable();
    KdTreeNodeManager::clearTable();
    UserDataManager::clearTable();
    MeshConnectionManager::clearTable();
    elog(INFO, "cleared index table");
    build_index();

    return filenames;  
}

/**
 * @brief Build spatial index for efficient spatiotemporal data querying
 * 
 * This function implements a comprehensive spatial indexing pipeline that processes
 * large-scale spatiotemporal datasets. It uses a multi-stage approach to handle
 * data that may not fit in memory, employing sampling, sorting, and merging techniques.
 * 
 * Indexing pipeline:
 * 1. Parallel bounds calculation and data sampling
 * 2. Load global spatial bounds from file
 * 3. Parallel Z-order sorting of sample data
 * 4. Multi-level merging of sorted sample files
 * 5. Octree construction (currently disabled)
 * 
 * Performance characteristics:
 * - Uses parallel processing for I/O intensive operations
 * - Implements external sorting for large datasets
 * - Maintains detailed timing statistics for performance analysis
 * - Supports incremental processing to handle memory constraints
 * 
 * @note Timing information is stored in build_time map for analysis
 * @note Octree construction is currently commented out but framework exists
 * @note Global bounds are persisted to disk for reuse across sessions
 */
void DataLoader::build_index()
{
    TimerClock tc;
    this->para_bound_and_sample();
    this->build_time["doChunking_time->sampling_time"] = tc.second();
    Bounds global_bound;
    {
        std::ifstream inputFile(data_dir +"/global_bound.txt");
        if (inputFile)
        {
            inputFile >> global_bound;
            elog(INFO, "Loaded bounds from global_bound.txt");
        }
    }
    // uint64_t sampleCellNums = (int64_t) 1 << (int64_t) chunk_max_level * 3, sampleDimension = (int64_t) 1 << (int64_t) chunk_max_level;
    // std::vector<uint64_t> originalCells;
    // Para_preCount(const vector<string> &filenames, sample_max_level-file_block_level, global_bound, std::vector<uint64_t> &originalCells);
    tc.tick();
    this->para_sort_sample_file(global_bound); // Sort all sample data according to z-order
    elog(INFO, "sample_sorted_file_count: %lu", this->sample_sorted_file_count.load());
    this->build_time["doChunking_time->sorting_time"] = tc.second();
    tc.tick();
    this->merge_sample_data();
    this->build_time["doChunking_time->merging_time"] = tc.second();
    tc.tick();
    elog(INFO, "build chunk");
    OctreeNode *root = building_octree_bottom_up_top_down(global_bound);// build chunk
    std::vector<DBOctreeNode> dbNodes = convertOctreeToDB(root);
    OctreeNodeManager::writeOctreeNodesToDatabase(-1, dbNodes);
    // if(delete_files)
    // std::remove((data_dir + "/sample_data/all_sampled_data.bin").c_str());
    this->build_time["doChunking_time->chunk_constrution_time"] = tc.second();
    if (0)
    { // validation
        dbNodes = OctreeNodeManager::loadOctreeNodesFromDatabase(-1);
        logger.log("INFO: dbNodes: " + std::to_string(dbNodes.size()));
        if (validateConversion(root, dbNodes, 0))
        {
            logger.log("INFO: Validation passed: The structures are consistent.");
        }
        else
        {
            logger.log("ERROR: Validation failed: The structures are not consistent.");
        }
    }
    logger.log("INFO: create chunk time: " + std::to_string(tc.second()) + "s");
    delete root;
}

/**
 * @brief Single-threaded bounds calculation and data sampling
 * 
 * This function processes all input files sequentially to calculate
 * spatial bounds and perform data sampling. It's designed to avoid
 * multi-threading issues with database operations.
 * 
 * Processing workflow:
 * 1. Clean temporary directories for fresh processing
 * 2. Load file-to-ID mappings from metadata
 * 3. Process each file sequentially to:
 *    - Calculate individual spatial bounds
 *    - Sample data points according to sampling ratio
 *    - Write original and sampled data to temporary files
 * 4. Merge all individual bounds into global bounds
 * 5. Persist global bounds to disk for later use
 * 
 * @note Creates temporary directories for original and sampled data
 * @note Progress is logged with timing information for performance monitoring
 * @note Global bounds are saved to global_bound.txt for persistence
 */
void DataLoader::para_bound_and_sample(){
    Bounds global_bound;
    std::thread clear1(clear_folder,data_dir + "/temp_bin_original_file");
    std::thread clear2(clear_folder,data_dir + "/temp_bin_sample_file");
    clear1.join();
    clear2.join();
    elog(INFO, "The number of files: %zu", this->filenames.size());
    std::vector<Bounds> sub_bounds(this->filenames.size());
    json loaded_json;
    {
        std::ifstream in_file(data_dir + "/files_user.json");
        in_file >> loaded_json;
        in_file.close();
    }
    std::atomic<std::uint32_t> finished_file_counting = 0;
    
    // Single-threaded processing instead of parallel
    for (std::uint32_t i = 0; i < this->filenames.size(); ++i)
    {
        TimerClock tc;
        auto file_name = this->filenames[i];
        sub_bounds[i] = this->calculate_bound_and_sampleing(file_name, i, loaded_json[file_name].get<int16_t>()); 
        
        // Log progress
        std::string log_message = "INFO: bound & sample: " + file_name + 
                                 " progress: " + std::to_string(finished_file_counting++) + 
                                 "/" + std::to_string(this->filenames.size()) + 
                                 " time: " + std::to_string(tc.second()) + "s";
        elog(INFO, "%s", log_message.c_str());
        
        // if(delete_original_files){
        //     std::remove(file_name.c_str());
        // }
    }
    
    // std::cout << "data_size:" << bounded_data_size << " file count:" << finished_file_counting << std::endl;
    bool firstFlag = true;
    global_bound = sub_bounds.front();
    for (auto &bound_pair : sub_bounds)
    {
        global_bound = global_bound | bound_pair;
    }
    std::ofstream outputFile(data_dir +"/global_bound.txt");
    if (outputFile)
    {
        outputFile << global_bound;
    }
}

/**
 * @brief Calculate spatial bounds and perform data sampling for a single file
 * 
 * This function processes a single spatiotemporal data file to extract spatial bounds
 * and create a sampled subset of the data. It supports multiple file formats and
 * handles different types of spatiotemporal data including point clouds, trajectories,
 * and mesh data.
 * 
 * Supported file formats:
 * - .ply: Point cloud data with intensity values
 * - .csv: Trajectory data with speed information
 * - .obj: 3D mesh data with color information
 * 
 * Data processing workflow:
 * 1. Parse input file based on format
 * 2. Calculate spatial bounds incrementally
 * 3. Apply random sampling based on sample_ratio
 * 4. Write both original and sampled data to binary files
 * 5. Handle memory management with buffered I/O
 * 
 * @param filename Path to the input data file
 * @param fid File identifier for tracking
 * @param user_id User identifier for data ownership
 * @return Bounds Calculated spatial bounds for the file
 * 
 * @note Uses 1MB buffer for efficient file I/O operations
 * @note Implements adaptive buffering to handle large files
 * @note Progress is logged every 10 million points processed
 * @note Mesh connectivity data is stored separately in database
 */
Bounds DataLoader::calculate_bound_and_sampleing(const string &filename, file_id_t fid, user_id_t user_id)
{
    ContinuousRandomGenerator generator(0.0f, 1.0f);
    Bounds bounds;
    bool firstPoint = true;
    ifstream file(filename);
    std::vector<char> _buffer(1024 * 1024);
    file.rdbuf()->pubsetbuf(_buffer.data(), _buffer.size());
    string line;
    const auto &extension = filename.substr(filename.size() - 3);
    long long count = 0;
    TimerClock tc;

    string file_prefix = getFileNameFromPath(filename);
    std::regex number_pattern(R"(\d+)");
    std::sregex_iterator it(file_prefix.begin(), file_prefix.end(), number_pattern);
    unsigned int pid = 0;
    ofstream outfile(data_dir + "/temp_bin_original_file/" + std::to_string(fid) + ".bin", std::ios::binary);
    ofstream sample_outfile(data_dir + "/temp_bin_sample_file/" + std::to_string(fid) + ".bin", std::ios::binary);
    
    // Check if file streams are successfully created and add logging
    if (!outfile.is_open()) {
        elog(ERROR, "Failed to create original data file: %s/temp_bin_original_file/%d.bin", 
             data_dir.c_str(), fid);
        throw std::runtime_error("Cannot create original data output file");
    } else {
        elog(INFO, "Successfully created original data file: %s/temp_bin_original_file/%d.bin", 
             data_dir.c_str(), fid);
    }
    
    if (!sample_outfile.is_open()) {
        elog(ERROR, "Failed to create sample data file: %s/temp_bin_sample_file/%d.bin", 
             data_dir.c_str(), fid);
        throw std::runtime_error("Cannot create sample data output file");
    } else {
        elog(INFO, "Successfully created sample data file: %s/temp_bin_sample_file/%d.bin", 
             data_dir.c_str(), fid);
    }
    
    std::vector<SpatioTemporalData> buffer;
    std::vector<SpatioTemporalData> sample_buffer;
    auto do_point = [&](SpatioTemporalData &point)
    {
        point.user_id = user_id;
        point.fid = fid;
        point.pid = pid;
        if (firstPoint)
        {
            bounds.min = point;
            bounds.max = point;
            bounds.min.time = point.time;
            bounds.max.time = point.time;
            firstPoint = false;
        }
        else
        {
            bounds.update(point);
        }
        ++pid;
        buffer.push_back(point);
        if(generator.generate() < this->sample_ratio){
            sample_buffer.push_back(point);
        }
        if (sample_buffer.size() >= (32 * 0.25 * 1e6/max_concurrent_tasks_for_count_task))
        {
            sample_outfile.write(reinterpret_cast<const char *>(sample_buffer.data()), sizeof(SpatioTemporalData) * sample_buffer.size());
            sample_buffer.clear();
        }
        if (buffer.size() >= (32 * 0.25 * 1e6/max_concurrent_tasks_for_count_task))
        {
            outfile.write(reinterpret_cast<const char *>(buffer.data()), sizeof(SpatioTemporalData) * buffer.size());
            this->bounded_data_size += buffer.size();
            buffer.clear();
        }
    };

    if (extension == "ply")
    {
        auto point_cloud_id = fid;
        while (getline(file, line))
        {
            if (line.substr(0, 3) == "end")
                break; // Skip Header
        }
        while (getline(file, line))
        {
            if (count++ >= max_point_limit)
            {
                break;
            }
            if (count % int(1e7) == 0)
            {
                // 使用线程安全的输出函数
                std::string log_message = "INFO: read bound: " + filename + 
                                         ": " + std::to_string(count / int(1e7)) + "x1e7 processing time " + 
                                         std::to_string(tc.second()) + "s";
                logger.log(log_message);
                tc.tick();
            }
            uint8_t colorR, colorG, colorB;
            stringstream ss(line);
            float x, y, z;
            float time, intensity;
            ss >> x >> y >> z >> time >> intensity;
            auto point = SpatioTemporalData(x, y, z, time);
            point.tid = PointCloudPoint;
            point.external_data.PointCloud.intensity = intensity;
            point.foreign_key = point_cloud_id;
            do_point(point);
        }
    }
    else if (extension == "csv")
    {
        getline(file, line); // Skip Header
        while (getline(file, line))
        {
            if (count++ >= max_point_limit)
            {
                break;
            }
            if (count % int(1e7) == 0)
            {
                // 使用线程安全的输出函数
                std::string log_message = "INFO: read bound: " + filename + 
                                         ": " + std::to_string(count / int(1e7)) + "x1e7 processing time " + 
                                         std::to_string(tc.second()) + "s";
                logger.log(log_message);
                tc.tick();
            }
            stringstream ss(line);
            vector<string> values;
            string value;
            while (getline(ss, value, ','))
            {
                values.push_back(value);
            }
            int id = stoi(values[1].substr(5));
            float x = stof(values[2]);
            float y = stof(values[3]);
            float z = stof(values[4]);
            float time = stof(values[0]);
            float speed = stof(values[5]);
            auto point = SpatioTemporalData(x, y, z, time);
            point.tid = TrajectoryPoint;
            point.external_data.Trajectoy.speed = speed;
            point.foreign_key = id;
            do_point(point);
        }
    }
    else if (extension == "obj")
    {
        auto meshid = fid;
        std::vector<std::vector<int32_t>> connections;
        size_t vertex_count = 0;
        size_t face_count = 0;
        
        while (getline(file, line))
        {
            if (count++ >= max_point_limit)
            {
                break;
            }

            if (count % int(1e7) == 0)
            {
                // 使用线程安全的输出函数
                std::string log_message = "INFO: read bound: " + filename + 
                                         ": " + std::to_string(count / int(1e7)) + "x1e7 processing time " + 
                                         std::to_string(tc.second()) + "s";
                logger.log(log_message);
                tc.tick();
            }
            stringstream ss(line);
            string prefix;
            ss >> prefix;
            if (prefix == "v")
            {
                vertex_count++;
                float x, y, z;
                uint8_t colorR;
                uint8_t colorG;
                uint8_t colorB;
                ss >> x >> y >> z >> colorR >> colorG >> colorB;
                auto point = SpatioTemporalData(x, y, z, 0);
                point.tid = MeshPoint;
                point.external_data.Mesh.colorR = colorR;
                point.external_data.Mesh.colorG = colorG;
                point.external_data.Mesh.colorB = colorB;
                point.foreign_key = meshid;
                do_point(point);
            }
            else if (prefix == "f")
            {
                face_count++;
                int32_t number;
                std::vector<int32_t> connection;
                while (ss >> number)
                {
                    connection.push_back(number);
                }
                connections.emplace_back(std::move(connection));
            }
        }
        
        // 记录.obj文件处理统计信息
        std::string log_message = "INFO: OBJ file processed: " + filename + 
                                 " - vertices=" + std::to_string(vertex_count) + 
                                 ", faces=" + std::to_string(face_count) + 
                                 ", connections=" + std::to_string(connections.size()) + 
                                 ", meshid=" + std::to_string(meshid);
        logger.log(log_message);
        
        // 如果连接数据很大，发出警告
        if (connections.size() > 100000) {
            std::string warning_message = "WARNING: Large mesh detected in " + filename + 
                                        " - " + std::to_string(connections.size()) + " connections";
            logger.log(warning_message);
        }
        
        MeshConnectionManager::writeDataToDatabase(meshid, connections);
    }
    if (buffer.size() > 0)
    {
        outfile.write(reinterpret_cast<const char *>(buffer.data()), sizeof(SpatioTemporalData) * buffer.size());
        this->bounded_data_size += buffer.size();
        buffer.clear();
    }
    if (sample_buffer.size() > 0)
    {
        sample_outfile.write(reinterpret_cast<const char *>(sample_buffer.data()), sizeof(SpatioTemporalData) * sample_buffer.size());
        sample_buffer.clear();
    }
    return bounds;
}

/**
 * @brief Sort sample data from a single file using Z-order indexing
 * 
 * This function processes a single sample file to perform spatial sorting using
 * Z-order (Morton order) indexing. It reads the file in chunks to manage memory
 * usage and groups spatially nearby points together for efficient access patterns.
 * 
 * Z-order sorting algorithm:
 * 1. Read sample data in manageable chunks
 * 2. Calculate Z-order index for each point based on spatial position
 * 3. Group points by their Z-order index
 * 4. Update global cell count statistics
 * 5. Write sorted groups to output files
 * 
 * Memory management:
 * - Uses adaptive buffer sizing based on concurrent task count
 * - Processes data in chunks to avoid memory overflow
 * - Thread-safe operations with mutex protection
 * 
 * @param out_file_id Output file identifier for sorted data
 * @param filename Input sample file path
 * @param bounds Global spatial bounds for index calculation
 * @param sampleCells Reference to cell count array for statistics
 * 
 * @note Buffer size is dynamically adjusted based on concurrent tasks
 * @note Uses thread-safe operations for file ID generation and cell counting
 * @note Implements chunked reading to handle large files efficiently
 */
void DataLoader::sort_sample_file(int out_file_id, const string &filename, const Bounds &bounds, vector<uint32_t> &sampleCells)
{
    auto spatial_bound = bounds.to_spatial_bound();
    std::ifstream infile(filename, std::ios::binary);
    if (!infile) {
        throw std::runtime_error("Open file failed!");
    }
    std::vector<SpatioTemporalData> buffer(4 * 64 * 1024 * 32/max_concurrent_tasks_for_count_task);

    while (infile) {
        std::unordered_map<std::int64_t, std::vector<SpatioTemporalData>> sorted_sample_data;
        infile.read(reinterpret_cast<char*>(buffer.data()), buffer.size() * sizeof(SpatioTemporalData));
        size_t bytes_read = infile.gcount();
        size_t num_elements_read = bytes_read / sizeof(SpatioTemporalData);
        this->sample_sort_count += num_elements_read;
        for (size_t i = 0; i < num_elements_read; ++i) {
            auto &point = buffer[i];
            int64_t index = indexOfPoint(point.x, point.y, point.z, spatial_bound, chunk_max_level);
            sorted_sample_data[index].emplace_back(point);
        }

        std::int64_t sample_file_id = 0;
        {
            lock_guard<mutex> lock(this->file_id_mutex);
            sample_file_id = this->sample_sorted_file_count++;
        }
        {
            lock_guard<mutex> lock(this->count_in_cell_mutex);
            for(auto &i:sorted_sample_data){
                sampleCells[i.first] += i.second.size();
            }
        }
        this->write_sample_to_file(sorted_sample_data, sample_file_id);

        if (bytes_read < buffer.size()) break;  // File has been read completely
    }
}

/**
 * @brief Write sorted sample data to binary file
 * 
 * This function takes a map of spatially grouped sample points and writes them
 * to a binary file in sorted order. The output format is optimized for efficient
 * merging operations in subsequent processing stages.
 * 
 * Output file format:
 * - For each spatial cell: [cell_index][point_count][point_data...]
 * - Cell indices are sorted in ascending order
 * - Binary format for efficient I/O operations
 * 
 * Processing steps:
 * 1. Convert map to vector for sorting
 * 2. Filter out empty cells
 * 3. Sort by cell index (Z-order)
 * 4. Write header and data for each cell
 * 5. Log statistics for monitoring
 * 
 * @param cellSamplePoint Map of cell indices to point lists
 * @param file_id Unique identifier for output file naming
 * 
 * @note Skips empty cells to optimize storage
 * @note Uses binary format for efficient storage and reading
 * @note Logs total point count for verification
 */
void DataLoader::write_sample_to_file(std::unordered_map<int64_t, vector<SpatioTemporalData>> &cellSamplePoint, int file_id)
{
    std::vector<std::pair<int64_t, vector<SpatioTemporalData>>> sorted_array;
    for(auto &i:cellSamplePoint){
        if(i.second.empty()){
            continue;
        }
        sorted_array.push_back(i);
    }
    if(sorted_array.empty()){
        return;
    }
    std::sort(sorted_array.begin(), sorted_array.end(), 
              [](const std::pair<int64_t, vector<SpatioTemporalData>>& a, const std::pair<int64_t, vector<SpatioTemporalData>>& b) {
                  return a.first < b.first; // Sort by key in ascending order
              });
    auto file_name = data_dir + "/sample_data/0_" + to_string(file_id) + ".bin";
    std::ofstream fileWrite(file_name, std::ios::binary | std::ios::app);
    uint64_t tmpcount = 0;
    for (auto &pointPair : sorted_array)
    {
        vector<SpatioTemporalData> &samplePointList = pointPair.second;
        uint64_t count_in_cell = samplePointList.size();
        tmpcount += count_in_cell;
        fileWrite.write(reinterpret_cast<const char *>(&pointPair.first), sizeof(pointPair.first));
        fileWrite.write(reinterpret_cast<const char *>(&count_in_cell), sizeof(count_in_cell));
        fileWrite.write(reinterpret_cast<const char *>(samplePointList.data()), count_in_cell * sizeof(SpatioTemporalData));
    }
    // 使用线程安全的输出函数
    std::string log_message = "INFO: " + file_name + " " + std::to_string(tmpcount);
    logger.log(log_message);
    fileWrite.close();
}

/**
 * @brief Parallel sorting of all sample files using Z-order indexing
 * 
 * This function orchestrates the parallel sorting of all sample files created
 * during the sampling phase. It implements a distributed sorting strategy to
 * handle large datasets that don't fit in memory.
 * 
 * Parallel sorting strategy:
 * 1. Clean output directory for fresh processing
 * 2. Discover all sample files from temporary directory
 * 3. Initialize cell count array for statistics
 * 4. Process each file in parallel using thread pool
 * 5. Aggregate cell statistics across all files
 * 6. Save global cell count statistics
 * 
 * Output organization:
 * - Creates sorted files in /sample_data/ directory
 * - Each file contains spatially grouped data
 * - Global cell statistics saved for merging phase
 * 
 * @param bounds Global spatial bounds for index calculation
 * 
 * @note Uses thread pool for efficient parallel processing
 * @note Cell count array size is 2^(chunk_max_level * 3) for 3D space
 * @note Statistics are persisted for validation in merging phase
 * @note Progress is logged with timing information per file
 */
void DataLoader::para_sort_sample_file(const Bounds &bounds)
{
    std::string sample_dir = data_dir+"/sample_data";
    clear_folder(sample_dir);
    std::vector<std::string> filenames;
    for (const auto &entry : fs::directory_iterator(data_dir + "/temp_bin_sample_file"))
    {
        const auto &path = entry.path();
        filenames.push_back(data_dir + "/temp_bin_sample_file/" + path.filename().string());
    }
    std::uint64_t sampleCellNums = (std::int64_t)1 << (std::int64_t)chunk_max_level * 3;
    auto sampleCells = std::vector<uint32_t>(sampleCellNums, 0);
    int file_id = 0;
    for (const auto &filename : filenames)
    {
        TimerClock tc;
        this->sort_sample_file(file_id, filename, bounds, sampleCells); 
        // Log progress
        std::string log_message = "INFO: sort sample: " + filename + 
                                 " time: " + std::to_string(tc.second()) + "s";
        elog(INFO, "%s", log_message.c_str());
        // if(delete_files)
        // std::remove(filename.c_str());
        ++file_id;
    }
    std::ofstream sampleWrite(sample_dir + "/sampleCellNums.bin", std::ios::binary | std::ios::app);
    sampleWrite.write(reinterpret_cast<const char *>(sampleCells.data()), sampleCellNums * sizeof(std::uint32_t));
    sampleWrite.close();
    elog(INFO, "all file sorted");
}

/**
 * @brief Merge all sorted sample files into a single globally sorted file
 * 
 * This function implements a multi-level external merge sort to combine all
 * sorted sample files into a single globally sorted file. It uses a tournament-style
 * merging approach to handle large datasets efficiently.
 * 
 * Merging algorithm:
 * 1. Perform iterative pairwise merging until one file remains
 * 2. Each iteration reduces file count by half
 * 3. Load and validate cell count statistics
 * 4. Copy final merged data to output file
 * 5. Validate data integrity during the process
 * 
 * Data validation:
 * - Compares expected vs actual point counts
 * - Verifies file size consistency
 * - Logs warnings for any inconsistencies
 * 
 * Output:
 * - Creates all_sampled_data.bin with globally sorted sample data
 * - Removes intermediate files to save disk space
 * - Maintains Z-order sorting for spatial locality
 * 
 * @throws std::runtime_error If data file validation fails
 * 
 * @note Uses external merge sort to handle datasets larger than memory
 * @note Validates data integrity throughout the process
 * @note Final output is suitable for octree construction
 */
void DataLoader::merge_sample_data(){
    std::uint64_t cur_iter_id = 0;
    while (1)
    {
        auto cur_file_num = this->Sequential_domerge(cur_iter_id, true);
        cur_file_num = (cur_file_num + 1) >> 1;
        cur_iter_id++;
        if (cur_file_num <= 1)
        {
            break;
        }
    }
    std::vector<std::uint32_t> sampleCells;
    {   
        uint64_t sampleCellNums = (int64_t)1 << (int64_t)chunk_max_level * 3;
        std::ifstream sampleFile(data_dir +"/sample_data/sampleCellNums.bin", std::ios::binary);
        sampleFile.seekg(0, std::ios::end);
        std::streamsize fileSize = sampleFile.tellg();
        sampleFile.seekg(0, std::ios::beg);
        if (fileSize % sizeof(uint32_t) != 0)
        {
            elog(ERROR, "fileSize: %lld", (long long)fileSize);
            throw std::runtime_error("Data file size exception");
        }
        std::size_t numElements = fileSize / sizeof(uint32_t);
        if (numElements != sampleCellNums)
        {
            throw std::runtime_error("Data count exception");
        }
        sampleCells.resize(numElements);
        sampleFile.read(reinterpret_cast<char *>(sampleCells.data()), fileSize);
        sampleFile.close();
    }
    std::ofstream new_file(data_dir +"/sample_data/all_sampled_data.bin", std::ios::binary | std::ios::app);
    std::ifstream cur_file(data_dir +"/sample_data/" + to_string(cur_iter_id) + "_0.bin", std::ios::binary);
    uint64_t cell_index = 0;
    uint64_t cur_index_id;
    uint64_t cur_index_number;

    uint64_t validation_sample_cell_id = 0;
    uint64_t validation_sample_cell_count = 0;
    while (cur_file.peek() != EOF && !cur_file.eof())
    {
        cur_file.read(reinterpret_cast<char *>(&cur_index_id), sizeof(cur_index_id));
        cur_file.read(reinterpret_cast<char *>(&cur_index_number), sizeof(cur_index_number));
        cell_index += cur_index_number;
        for (; validation_sample_cell_id <= cur_index_id; ++validation_sample_cell_id)
        {
            validation_sample_cell_count += sampleCells[validation_sample_cell_id];
        }
        if (cell_index != validation_sample_cell_count)
        {
            elog(WARNING, "sample validation failed: %lu:%lu", validation_sample_cell_count, cell_index);
        }
        for (int i = 0; i < cur_index_number; i++)
        {
            SpatioTemporalData point;
            cur_file.read(reinterpret_cast<char *>(&point), sizeof(point));
            new_file.write(reinterpret_cast<char *>(&point), sizeof(point));
        }
    }
    elog(INFO, "merged sample data size: %lu", cell_index);
    std::remove(std::string(data_dir +"/sample_data/" + to_string(cur_iter_id) + "_0.bin").c_str());
    
}

OctreeNode *building_octree_bottom_up_top_down(Bounds bounds)
{
    BuildChunk build_util(max_concurrent_task_across_chunk);
    build_util.load_sample_size();
    std::int64_t count_sample = 0;
    for (auto &i : build_util.sample_cell_num_in_diff_level.back())
    {
        count_sample += i;
    }
    logger.log("INFO: prepare: " + std::to_string(count_sample));
    logger.log("INFO: build_util.sample_cell_num_in_diff_level: " + std::to_string(build_util.sample_cell_num_in_diff_level.size()));
    logger.log("INFO: build_util.cell_num_in_diff_level: " + std::to_string(build_util.cell_num_in_diff_level.size()));

    clear_folder(data_dir + "/chunk_data");
    auto root = build_util.build_chunk_node_sample(bounds, 0, 0, 0, 0);//build chunk
    build_util.do_indexing_thread_pool.wait_for_all_tasks();
    logger.log("INFO: chunk node count: " + std::to_string(build_util.node_count));
    logger.log("INFO: split sample size: " + std::to_string(build_util.split_sample_count));
    logger.log("INFO: sample file point position: " + std::to_string(build_util.sample_file.tellg() / sizeof(SpatioTemporalData)));
    return root;
}

/**
 * @brief Merge two sorted files into a single sorted file
 * 
 * This function implements the core pairwise merging logic for external merge sort.
 * It handles merging of two sorted spatiotemporal data files while maintaining
 * Z-order sorting and managing memory efficiently through buffered I/O.
 * 
 * Merging algorithm:
 * - Two-way merge similar to merge sort
 * - Compares cell indices to maintain global ordering
 * - Handles cases where files have same or different cell indices
 * - Uses buffered I/O to manage memory usage
 * 
 * Special cases:
 * 1. Single file (odd count): Simply rename to next iteration
 * 2. Two files: Perform standard two-way merge
 * 3. Same cell index: Combine point counts and merge data
 * 
 * @param cur_file_id Current file ID being processed (even numbers)
 * @param cur_iter Current iteration level (0-based)
 * @param cur_file_num Total number of files in current iteration
 * @param prefix Directory prefix for file organization
 * @return uint64_t Total number of points processed
 * 
 * @note File naming follows pattern: {iteration}_{file_id}.bin
 * @note Output files go to next iteration: {iteration+1}_{file_id/2}.bin
 * @note Uses 1KB buffer for efficient memory usage
 */
uint64_t domerge(uint64_t cur_file_id, uint64_t cur_iter, uint64_t cur_file_num, std::string prefix)
{
    // string cur_file_name = "";
    // string next_file_name = "";
    // string new_file_name = "";
    // prefix
    string new_file_name = data_dir +"/" + prefix + "/" + to_string(cur_iter + 1) + "_" + to_string(cur_file_id >> 1) + ".bin";
    string cur_file_name = data_dir +"/" + prefix + "/" + to_string(cur_iter) + "_" + to_string(cur_file_id) + ".bin";
    string next_file_name = data_dir +"/" + prefix + "/" + to_string(cur_iter) + "_" + to_string(cur_file_id + 1) + ".bin";
    bool to_print = true;
    uint64_t all_number = 0;
    const size_t buffer_size = 1024;
    std::vector<SpatioTemporalData> buffer(buffer_size);
    if (cur_file_id == cur_file_num - 1)
    {
        int64_t cur_index_id;
        uint64_t cur_index_number;
        std::ifstream cur_file(cur_file_name, std::ios::binary);
        cur_file.read(reinterpret_cast<char *>(&cur_index_id), sizeof(cur_index_id));
        cur_file.read(reinterpret_cast<char *>(&cur_index_number), sizeof(cur_index_number));

        while (true)
        {
            all_number += cur_index_number;
            size_t points_read = 0;
            while (points_read < cur_index_number)
            {
                size_t points_to_read = std::min(buffer_size, static_cast<size_t>(cur_index_number - points_read));
                cur_file.read(reinterpret_cast<char *>(buffer.data()), sizeof(SpatioTemporalData) * points_to_read);
                for (size_t i = 0; i < points_to_read; ++i)
                {
                    const SpatioTemporalData &point = buffer[i];
                }
                points_read += points_to_read;
            }

            if (cur_file.peek() != EOF && !cur_file.eof())
            {
                cur_file.read(reinterpret_cast<char *>(&cur_index_id), sizeof(cur_index_id));
                cur_file.read(reinterpret_cast<char *>(&cur_index_number), sizeof(cur_index_number));
            }
            else
            {
                break;
            }
        }
        if (to_print)
        {
            // 使用线程安全的输出函数
            std::string log_message = "INFO: " + std::to_string(all_number) + 
                                     " cur_file_id:" + std::to_string(cur_file_id) + 
                                     " cur_iter:" + std::to_string(cur_iter) + 
                                     " cur_file_num:" + std::to_string(cur_file_num) + 
                                     " next:" + next_file_name + 
                                     " cur:" + cur_file_name + 
                                     " new:" + new_file_name;
            logger.log(log_message);
        }
        std::rename(cur_file_name.c_str(), new_file_name.c_str());
        return all_number;
    }
    std::ifstream cur_file(cur_file_name, std::ios::binary);
    std::ifstream next_file(next_file_name, std::ios::binary);
    std::ofstream new_file(new_file_name, std::ios::binary | std::ios::app);
    int64_t cur_index_id;
    uint64_t cur_index_number;
    int64_t next_index_id;
    uint64_t next_index_number;
    cur_file.read(reinterpret_cast<char *>(&cur_index_id), sizeof(cur_index_id));
    cur_file.read(reinterpret_cast<char *>(&cur_index_number), sizeof(cur_index_number));
    next_file.read(reinterpret_cast<char *>(&next_index_id), sizeof(next_index_id));
    next_file.read(reinterpret_cast<char *>(&next_index_number), sizeof(next_index_number));
    bool file1_end = false;
    bool file2_end = false;
    while (cur_file.peek() != EOF && !cur_file.eof() && next_file.peek() != EOF && !next_file.eof())
    {
        if (cur_index_id < next_index_id)
        {
            new_file.write(reinterpret_cast<const char *>(&cur_index_id), sizeof(cur_index_id));
            new_file.write(reinterpret_cast<const char *>(&cur_index_number), sizeof(cur_index_number));
            all_number += cur_index_number;

            // for (int i = 0; i < cur_index_number; i++)
            // {
            //     SpatioTemporalData point;
            //     cur_file.read(reinterpret_cast<char *>(&point), sizeof(point));
            //     new_file.write(reinterpret_cast<char *>(&point), sizeof(point));
            // }

            size_t points_read = 0;
            while (points_read < cur_index_number)
            {
                size_t points_to_read = std::min(buffer_size, static_cast<size_t>(cur_index_number - points_read));
                cur_file.read(reinterpret_cast<char *>(buffer.data()), sizeof(SpatioTemporalData) * points_to_read);
                new_file.write(reinterpret_cast<char *>(buffer.data()), sizeof(SpatioTemporalData) * points_to_read);
                points_read += points_to_read;
            }
            if (cur_file.peek() != EOF && !cur_file.eof())
            {
                cur_file.read(reinterpret_cast<char *>(&cur_index_id), sizeof(cur_index_id));
                cur_file.read(reinterpret_cast<char *>(&cur_index_number), sizeof(cur_index_number));
            }
            else
            {
                file1_end = true;
            }
        }
        else if (cur_index_id > next_index_id)
        {
            new_file.write(reinterpret_cast<const char *>(&next_index_id), sizeof(next_index_id));
            new_file.write(reinterpret_cast<const char *>(&next_index_number), sizeof(next_index_number));
            all_number += next_index_number;
            // for (int i = 0; i < next_index_number; i++)
            // {
            //     SpatioTemporalData point;
            //     next_file.read(reinterpret_cast<char *>(&point), sizeof(point));
            //     new_file.write(reinterpret_cast<char *>(&point), sizeof(point));
            // }

            size_t points_read = 0;
            while (points_read < next_index_number)
            {
                size_t points_to_read = std::min(buffer_size, static_cast<size_t>(next_index_number - points_read));
                next_file.read(reinterpret_cast<char *>(buffer.data()), sizeof(SpatioTemporalData) * points_to_read);
                new_file.write(reinterpret_cast<char *>(buffer.data()), sizeof(SpatioTemporalData) * points_to_read);
                points_read += points_to_read;
            }

            if (next_file.peek() != EOF && !next_file.eof())
            {
                next_file.read(reinterpret_cast<char *>(&next_index_id), sizeof(next_index_id));
                next_file.read(reinterpret_cast<char *>(&next_index_number), sizeof(next_index_number));
            }
            else
            {
                file2_end = true;
            }
        }
        else if (cur_index_id == next_index_id)
        {
            new_file.write(reinterpret_cast<const char *>(&cur_index_id), sizeof(cur_index_id));
            uint64_t new_index_number = cur_index_number + next_index_number;
            all_number += new_index_number;
            new_file.write(reinterpret_cast<const char *>(&new_index_number), sizeof(new_index_number));
            // for (int i = 0; i < cur_index_number; i++)
            // {
            //     SpatioTemporalData point;
            //     cur_file.read(reinterpret_cast<char *>(&point), sizeof(point));
            //     new_file.write(reinterpret_cast<char *>(&point), sizeof(point));
            // }


            // for (int i = 0; i < next_index_number; i++)
            // {
            //     SpatioTemporalData point;
            //     next_file.read(reinterpret_cast<char *>(&point), sizeof(point));
            //     new_file.write(reinterpret_cast<char *>(&point), sizeof(point));
            // }

            size_t points_read = 0;
            while (points_read < cur_index_number)
            {
                size_t points_to_read = std::min(buffer_size, static_cast<size_t>(cur_index_number - points_read));
                cur_file.read(reinterpret_cast<char *>(buffer.data()), sizeof(SpatioTemporalData) * points_to_read);
                new_file.write(reinterpret_cast<char *>(buffer.data()), sizeof(SpatioTemporalData) * points_to_read);
                points_read += points_to_read;
            }
            points_read = 0;
            while (points_read < next_index_number)
            {
                size_t points_to_read = std::min(buffer_size, static_cast<size_t>(next_index_number - points_read));
                next_file.read(reinterpret_cast<char *>(buffer.data()), sizeof(SpatioTemporalData) * points_to_read);
                new_file.write(reinterpret_cast<char *>(buffer.data()), sizeof(SpatioTemporalData) * points_to_read);
                points_read += points_to_read;
            }

            if (cur_file.peek() != EOF && !cur_file.eof())
            {
                cur_file.read(reinterpret_cast<char *>(&cur_index_id), sizeof(cur_index_id));
                cur_file.read(reinterpret_cast<char *>(&cur_index_number), sizeof(cur_index_number));
            }
            else
            {
                file1_end = true;
            }
            if (next_file.peek() != EOF && !next_file.eof())
            {
                next_file.read(reinterpret_cast<char *>(&next_index_id), sizeof(next_index_id));
                next_file.read(reinterpret_cast<char *>(&next_index_number), sizeof(next_index_number));
            }
            else
            {
                file2_end = true;
            }
        }
        if (file2_end || file1_end)
        {
            break;
        }
    }
    if (file2_end && file1_end)
    {
        if (to_print)
        {
            // Log merge progress
            std::string log_message = "INFO: " + std::to_string(all_number) + 
                                     " cur_file_id:" + std::to_string(cur_file_id) + 
                                     " cur_iter:" + std::to_string(cur_iter) + 
                                     " cur_file_num:" + std::to_string(cur_file_num) + 
                                     " next:" + next_file_name + 
                                     " cur:" + cur_file_name + 
                                     " new:" + new_file_name;
            elog(INFO, "%s", log_message.c_str());
        }
        return all_number;
    }
    if (file2_end)
    {
        while (true)
        {
            all_number += cur_index_number;
            new_file.write(reinterpret_cast<const char *>(&cur_index_id), sizeof(cur_index_id));
            new_file.write(reinterpret_cast<const char *>(&cur_index_number), sizeof(cur_index_number));
            for (int i = 0; i < cur_index_number; i++)
            {
                SpatioTemporalData point;
                cur_file.read(reinterpret_cast<char *>(&point), sizeof(point));
                new_file.write(reinterpret_cast<char *>(&point), sizeof(point));
            }
            if (cur_file.peek() != EOF && !cur_file.eof())
            {
                cur_file.read(reinterpret_cast<char *>(&cur_index_id), sizeof(cur_index_id));
                cur_file.read(reinterpret_cast<char *>(&cur_index_number), sizeof(cur_index_number));
            }
            else
            {
                break;
            }
        }
    }
    if (file1_end)
    {
        while (true)
        {
            all_number += next_index_number;
            new_file.write(reinterpret_cast<const char *>(&next_index_id), sizeof(next_index_id));
            new_file.write(reinterpret_cast<const char *>(&next_index_number), sizeof(next_index_number));
            for (int i = 0; i < next_index_number; i++)
            {
                SpatioTemporalData point;
                next_file.read(reinterpret_cast<char *>(&point), sizeof(point));
                new_file.write(reinterpret_cast<char *>(&point), sizeof(point));
            }
            if (next_file.peek() != EOF && !next_file.eof())
            {
                next_file.read(reinterpret_cast<char *>(&next_index_id), sizeof(next_index_id));
                next_file.read(reinterpret_cast<char *>(&next_index_number), sizeof(next_index_number));
            }
            else
            {
                break;
            }
        }
    }
    if (to_print)
    {
        // Log merge progress
        std::string log_message = "INFO: " + std::to_string(all_number) + 
                                 " cur_file_id:" + std::to_string(cur_file_id) + 
                                 " cur_iter:" + std::to_string(cur_iter) + 
                                 " cur_file_num:" + std::to_string(cur_file_num) + 
                                 " next:" + next_file_name + 
                                 " cur:" + cur_file_name + 
                                 " new:" + new_file_name;
        elog(INFO, "%s", log_message.c_str());
    }
    return all_number;
}

/**
 * @brief Sequential merge operation for one iteration of external merge sort
 * 
 * This function orchestrates sequential merging of multiple file pairs in a single
 * iteration of the external merge sort algorithm. It performs merging and cleanup
 * operations sequentially to avoid database threading issues.
 * 
 * Sequential merge strategy:
 * 1. Discover all files for current iteration
 * 2. Process adjacent file pairs (i, i+1) sequentially
 * 3. Execute merges one by one 
 * 4. Clean up processed files sequentially
 * 5. Return file count for next iteration planning
 * 
 * @param cur_iter_id Current iteration identifier (0-based)
 * @param sample_or_original Flag to choose between sample_data or original_data
 * @return uint64_t Number of files processed (for next iteration planning)
 * 
 * @note Merges files in pairs: (0,1) -> 0, (2,3) -> 1, etc.
 * @note Sequential processing avoids database threading conflicts
 * @note Works with both sample and original data directories
 */
uint64_t DataLoader::Sequential_domerge(uint64_t cur_iter_id, bool sample_or_original)
{
    elog(INFO, "Sequential merge iteration: %lu", cur_iter_id);
    string prefix;
    if (sample_or_original)
    {
        prefix = "sample_data";
    }
    else
    {
        prefix = "original_data";
    }
    auto sample_files = findFilesWithPrefix(data_dir +"/" + prefix, to_string(cur_iter_id) + "_");
    std::atomic<uint64_t> merged_count(0);
    for (uint64_t i = 0; i < sample_files.size(); i += 2)
    {
        merged_count += domerge(i, cur_iter_id, sample_files.size(), prefix);
    }
    // sample_files = findFilesWithPrefix("./" + prefix, to_string(cur_iter_id) + "_");

    {
        // Clean up processed files sequentially
        for(std::int64_t i = 0; i < sample_files.size(); ++i){
            std::string file_path = data_dir +"/" + prefix + "/" + sample_files[i];
            std::remove(file_path.c_str()); 
        }
    }
    elog(INFO, "merged it count: %lu - %lu", cur_iter_id, merged_count.load());
    return sample_files.size();
}

void BuildChunk::load_sample_size()
{
    uint64_t sampleCellNums = (int64_t)1 << (int64_t)chunk_max_level * 3, sampleDimension = (int64_t)1 << (int64_t)chunk_max_level;
    {
        std::ifstream sampleFile(data_dir +"/sample_data/sampleCellNums.bin", std::ios::binary);
        sampleFile.seekg(0, std::ios::end);
        std::streamsize fileSize = sampleFile.tellg();
        sampleFile.seekg(0, std::ios::beg);
        if (fileSize % sizeof(uint32_t) != 0)
        {
            throw std::runtime_error("Data file size exception");
        }
        std::size_t numElements = fileSize / sizeof(uint32_t);
        if (numElements != sampleCellNums)
        {
            throw std::runtime_error("Data count exception");
        }
        sampleCells.resize(numElements);
        sampleFile.read(reinterpret_cast<char *>(sampleCells.data()), fileSize);
        sampleFile.close();
    }

    node_count = 0;
    long long sample_size = 0;
    sample_file = std::ifstream(data_dir +"/sample_data/all_sampled_data.bin", std::ios::binary);
    sample_file.seekg(0, std::ios::end);
    std::streamsize fileSize = sample_file.tellg();
    sample_size = fileSize / sizeof(SpatioTemporalData);
    sample_all_size = sample_size;
    logger.log("INFO: sample file size: " + std::to_string(sample_size));
    sample_file.seekg(0, std::ios::beg);

    auto copied_sampleCells = sampleCells;
    while (copied_sampleCells.size() >= 1)
    {
        sample_cell_num_in_diff_level.push_back(copied_sampleCells);
        size_t newSize = copied_sampleCells.size() / 8;
        if (newSize == 0)
        {
            break;
        }
        size_t count = 0;
        std::vector<__uint32_t> mergedCells(newSize);
        for (size_t i = 0; i < newSize; ++i)
        {
            __uint32_t mergedValue = 0;
            for (size_t j = i * 8; j < (i + 1) * 8; ++j)
            {
                mergedValue += copied_sampleCells[j];
            }
            count += mergedValue;
            mergedCells[i] = mergedValue;
        }
        logger.log("INFO: level size: " + std::to_string(newSize) + " count: " + std::to_string(count));
        copied_sampleCells = std::move(mergedCells);
    }
    std::reverse(sample_cell_num_in_diff_level.begin(), sample_cell_num_in_diff_level.end());
}

// void BuildChunk::split_original_data(Bounds bounds, uint64_t level, uint64_t x, uint64_t y, uint64_t z)
// {
//     auto id = node_count++;
//     uint64_t cell_id = interleaveBits(x, y, z,chunk_max_level);
//     auto pointCount = sample_cell_num_in_diff_level[level][cell_id];
//     if (pointCount <= max_point_per_chunk || level >= chunk_max_level)
//     {
//         // original data
//         int64_t chunk_original_data_size = cell_num_in_diff_level[level][cell_id];
//         std::vector<SpatioTemporalData> chunk_data(chunk_original_data_size);
//         original_file.read(reinterpret_cast<char *>(chunk_data.data()), chunk_original_data_size * sizeof(SpatioTemporalData));
//         // std::ofstream file("./chunk_original_data/chunk_data_" + std::to_string(id) + ".bin", std::ios::binary);
//         // int64_t remaining = chunk_original_data_size;
//         // while (remaining > 0)
//         // {
//         //     size_t to_read = std::min(static_cast<int64_t>(buffer_size), remaining);
            
//         //     file.write(reinterpret_cast<char *>(buffer.data()), to_read * sizeof(SpatioTemporalData));
//         //     remaining -= to_read;
//         //     split_original_count += to_read;
//         // }
//         // file.close();
//         do_indexing_thread_pool.post_task(
//             [chunk_data = std::move(chunk_data),id](){
//                 chunk_original_data_to_db(id, chunk_data);
//             }
//         );
//         do_indexing_thread_pool.wait_for_all_tasks(max_concurrent_task_across_chunk * 2);
//         std::cout << " leaf info: " << " id:" << id << " level:" << level << " x:" << x << " y:" << y << " z:" << z << " size:" << chunk_original_data_size << std::endl;
//         return;
//     }
//     for (int i = 0; i < 8; ++i)
//     {
//         int bitx = (i >> 0) & 1;
//         int bity = (i >> 1) & 1;
//         int bitz = (i >> 2) & 1;
//         int new_x = (x << 1) | bitx;
//         int new_y = (y << 1) | bity;
//         int new_z = (z << 1) | bitz;
//         auto center = bounds.getCenter();
//         Bounds sub_bound = bounds;
//         if (bitx == 0)
//         {
//             sub_bound.max.x = center.x;
//         }
//         else
//         {
//             sub_bound.min.x = center.x;
//         }
//         if (bity == 0)
//         {
//             sub_bound.max.y = center.y;
//         }
//         else
//         {
//             sub_bound.min.y = center.y;
//         }

//         if (bitz == 0)
//         {
//             sub_bound.max.z = center.z;
//         }
//         else
//         {
//             sub_bound.min.z = center.z;
//         }
//         split_original_data(sub_bound, level + 1, new_x, new_y, new_z);
//     }
// }

void doIndexing(Bounds bound, int chunk_id, std::vector<SpatioTemporalData> data, int octree_max_level, int max_point_per_leaf, int leaf_num)
{
    TimerClock tc;
    OctreeBuilder octree_builder(max_point_per_leaf);
    octree_builder.dataPoints = std::move(data);
    if (octree_builder.dataPoints.size() == 0)
    {
        std::vector<DBOctreeNode> dbNodes(1);
        dbNodes[0].bound = bound.to_spatial_bound();
        dbNodes[0].is_leaf = true;
        dbNodes[0].id = 0;
        dbNodes[0].oc_id = chunk_id;
        dbNodes[0].kd_id = 0;
        OctreeNodeManager::writeOctreeNodesToDatabase(chunk_id, dbNodes);
        auto tree = std::vector<DBKdtreeNode>(0);
        BinaryKVStorage bs(data_dir+"/kdtree");
        // KdTreeNodeManager::writeKdTreeNodesToDatabase(chunk_id, dbNodes[0].id, tree);
        bs.write("kd_"+std::to_string(chunk_id)+"_"+std::to_string(dbNodes[0].id),tree);
        // 建立这里的原因是防止采样数据没有采样到，但是原始数据里面又这样的数据而报错
        return;
    }
    index_loading_num += octree_builder.dataPoints.size();
    tc.tick();
    OctreeNode *octree_root = octree_builder.buildOctree(bound);//memory pointer
    encode_Octree(octree_root);
    std::vector<DBOctreeNode> dbNodes = convertOctreeToDB(octree_root, chunk_id);//pointer to vector
    OctreeNodeManager::writeOctreeNodesToDatabase(chunk_id, dbNodes);
    tc.tick();
    vector<OctreeNode *> leafVector;
    getLeafNodes(octree_root, leafVector);
    tc.tick();
    para_createTreeWithLeafNode(chunk_id, octree_builder.dataPoints, leafVector);
    ++finihsed_octree;
    // std::cout << "build in chunk:" << finihsed_octree << ":" << leaf_num << "\r";
    delete octree_root;
}

OctreeNode *BuildChunk::build_chunk_node_sample(Bounds bounds, uint64_t level, uint64_t x, uint64_t y, uint64_t z)
{
    uint64_t cell_id = interleaveBits(x, y, z,chunk_max_level);
    OctreeNode *node = new OctreeNode(bounds, level);
    node->id = node_count++;
    node->pointCount = sample_cell_num_in_diff_level[level][cell_id];
    if (node->pointCount <= max_point_per_chunk || level >= chunk_max_level)
    {
        node->is_leaf = true;
        {
            std::vector<SpatioTemporalData> chunk_data(node->pointCount);
            sample_file.read(reinterpret_cast<char *>(chunk_data.data()), node->pointCount * sizeof(SpatioTemporalData));
            split_sample_count += node->pointCount;
            // Comment out parallel implementation and use serial instead
            // do_indexing_thread_pool.post_task(
            //     [chunk_data = std::move(chunk_data),bound=node->bound,chunk_id = node->id](){
            //         doIndexing(bound,chunk_id,std::move(chunk_data),octree_max_level, max_point_per_leaf,-1);
            //     }
            // );
            // do_indexing_thread_pool.wait_for_all_tasks(max_concurrent_task_across_chunk * 2);
            
            // Serial implementation
            doIndexing(node->bound, node->id, std::move(chunk_data), octree_max_level, max_point_per_leaf, -1);
        }
        logger.log("INFO: leaf info: id:" + std::to_string(node->id) + " level:" + std::to_string(level) + " x:" + std::to_string(x) + " y:" + std::to_string(y) + " z:" + std::to_string(z) + " size:" + std::to_string(node->pointCount));
        return node;
    }
    for (int i = 0; i < 8; ++i)
    {
        int bitx = (i >> 0) & 1;
        int bity = (i >> 1) & 1;
        int bitz = (i >> 2) & 1;
        int new_x = (x << 1) | bitx;
        int new_y = (y << 1) | bity;
        int new_z = (z << 1) | bitz;
        auto center = bounds.getCenter();
        Bounds sub_bound = bounds;
        if (bitx == 0)
        {
            sub_bound.max.x = center.x;
        }
        else
        {
            sub_bound.min.x = center.x;
        }
        if (bity == 0)
        {
            sub_bound.max.y = center.y;
        }
        else
        {
            sub_bound.min.y = center.y;
        }

        if (bitz == 0)
        {
            sub_bound.max.z = center.z;
        }
        else
        {
            sub_bound.min.z = center.z;
        }
        node->children[i] = build_chunk_node_sample(sub_bound, level + 1, new_x, new_y, new_z);
    }
    return node;
}

void split_data(int id,std::vector<DBOctreeNode> &nodes, std::vector<SpatioTemporalData> &data,std::unordered_map<int,std::vector<SpatioTemporalData>> &result) {
    auto &node = nodes[id];
    if(node.is_leaf){
        result[node.id] = std::move(data);
        return;
    }
    auto center = node.bound.getCenter();
    std::vector<SpatioTemporalData> sub_data[8];
    for(auto &point:data){
        uint64_t childIndex = 0;
        if (point.x < center.x){childIndex |= 0;} 
        else{childIndex |= 1;}
        if (point.y < center.y) {childIndex |= 0;}
        else{childIndex |= 2;}
        if (point.z < center.z) {childIndex |= 0;}
        else{childIndex |= 4;}
        sub_data[childIndex].push_back(point);
    }
    for(int i = 0;i<8;++i){
        if(node.children[i] == -1){
            if(sub_data[i].size() > 0){
                logger.log("ERROR: This occurs because there are no sample points but original data exists");
            }
            continue;
        }
        split_data(node.children[i],nodes,sub_data[i],result);
    }
}

void chunk_original_data_to_db(int chunk_id, std::vector<SpatioTemporalData> all_data)
{
    std::vector<DBOctreeNode> ocNodes = OctreeNodeManager::loadOctreeNodesFromDatabase(chunk_id);
    size_t data_count = 0;
    long long in_this_file_read_count = 0;
    std::unordered_map<int, std::vector<SpatioTemporalData>> to_db_data; // octree leaf id --> data
    auto write_points_to_db = [&](int octreeid, int kdtreeid, std::vector<SpatioTemporalData> &data)
    {
        std::sort(data.begin(), data.end(), [](const SpatioTemporalData &a, const SpatioTemporalData &b)
                  { return a.time < b.time; });
        originalDataManager.writeOriginalDataToDatabase(octreeid, kdtreeid, data);
    };

    split_data(0, ocNodes, all_data, to_db_data);

    for (auto &i : to_db_data)
    {
        in_this_file_read_count += i.second.size();
    }
    for (auto &i : to_db_data)
    {
        write_points_to_db(chunk_id, i.first, i.second);
    }
}

// void chunk_original_data_to_db(DBOctreeNode leaf_node)
// {
//     std::vector<DBOctreeNode> ocNodes = OctreeNodeManager::loadOctreeNodesFromDatabase(leaf_node.id);
//     size_t data_count = 0;
//     std::vector<SpatioTemporalData> all_data;
//     {
//         auto original_file = std::ifstream(data_dir + "/chunk_original_data/" + std::to_string(leaf_node.id) + ".bin", std::ios::binary);
//         if (!original_file.is_open()) {
//             return;
//         }
//         original_file.seekg(0, std::ios::end);
//         size_t file_size = original_file.tellg();
//         original_file.seekg(0, std::ios::beg);
//         data_count = file_size / sizeof(SpatioTemporalData);
//         logger.log("INFO: leaf_node.id:" + std::to_string(leaf_node.id) + "  data_count:" + std::to_string(data_count));
//         all_data.resize(data_count);
//         original_file.read(reinterpret_cast<char *>(all_data.data()), file_size);
//         original_file.close();
//         if(delete_files)
//         std::remove((data_dir + "/chunk_original_data/" + std::to_string(leaf_node.id) + ".bin").c_str());
//     }
//     long long in_this_file_read_count = 0;
//     std::unordered_map<int, std::vector<SpatioTemporalData>> to_db_data; // octree leaf id --> data
//     auto write_points_to_db = [&](int octreeid, int kdtreeid, std::vector<SpatioTemporalData> &data)
//     {
//         std::sort(data.begin(), data.end(), [](const SpatioTemporalData &a, const SpatioTemporalData &b)
//                   { return a.time < b.time; });
//         originalDataManager.writeOriginalDataToDatabase(octreeid, kdtreeid, data);
//     };

//     split_data(0, ocNodes, all_data, to_db_data);

//     for (auto &i : to_db_data)
//     {
//         in_this_file_read_count += i.second.size();
//     }
//     for (auto &i : to_db_data)
//     {
//         write_points_to_db(leaf_node.id, i.first, i.second);
//     }
// }


void BuildChunk::load_original_and_sample_size(){
    uint64_t sampleCellNums = (int64_t)1 << (int64_t)chunk_max_level * 3, sampleDimension = (int64_t)1 << (int64_t)chunk_max_level;
    {
        sampleCells.resize(sampleCellNums);
        originalCells.resize(sampleCellNums);
        std::ifstream originalFile(data_dir +"/sample_data/originalCellNums.bin", std::ios::binary);
        originalFile.seekg(0, std::ios::end);
        std::streamsize fileSize = originalFile.tellg();
        originalFile.seekg(0, std::ios::beg);
        if (fileSize % sizeof(uint64_t) != 0)
        {
            throw std::runtime_error("Data file size exception");
        }
        std::size_t numElements = fileSize / sizeof(uint64_t);
        if (numElements != sampleCellNums)
        {
            throw std::runtime_error("Data file size exception");
        }
        originalCells.resize(numElements);
        originalFile.read(reinterpret_cast<char *>(originalCells.data()), fileSize);
        originalFile.close();
    }
    {
        std::ifstream sampleFile(data_dir +"/sample_data/sampleCellNums.bin", std::ios::binary);
        sampleFile.seekg(0, std::ios::end);
        std::streamsize fileSize = sampleFile.tellg();
        sampleFile.seekg(0, std::ios::beg);
        if (fileSize % sizeof(uint32_t) != 0)
        {
            throw std::runtime_error("Data file size exception");
        }
        std::size_t numElements = fileSize / sizeof(uint32_t);
        if (numElements != sampleCellNums)
        {
            throw std::runtime_error("Data file size exception");
        }
        sampleCells.resize(numElements);
        sampleFile.read(reinterpret_cast<char *>(sampleCells.data()), fileSize);
        sampleFile.close();
    }

    node_count = 0;
    long long sample_size = 0;
    sample_file = std::ifstream(data_dir +"/sample_data/all_sampled_data.bin", std::ios::binary);
    sample_file.seekg(0, std::ios::end);
    std::streamsize fileSize = sample_file.tellg();
    sample_size = fileSize / sizeof(SpatioTemporalData);
    sample_all_size = sample_size;
    logger.log("INFO: sample file size: " + std::to_string(sample_size));
    sample_file.seekg(0, std::ios::beg);

    original_file = std::ifstream(data_dir +"/original_data/all_data.bin", std::ios::binary);
    original_file.seekg(0, std::ios::end);
    fileSize = original_file.tellg();
    sample_size = fileSize / sizeof(SpatioTemporalData);
    original_all_size = sample_size;
    logger.log("INFO: data file size: " + std::to_string(sample_size));
    original_file.seekg(0, std::ios::beg);

    auto copied_sampleCells = sampleCells;
    while (copied_sampleCells.size() >= 1)
    {
        sample_cell_num_in_diff_level.push_back(copied_sampleCells);
        size_t newSize = copied_sampleCells.size() / 8;
        if (newSize == 0)
        {
            break;
        }
        size_t count = 0;
        std::vector<__uint32_t> mergedCells(newSize);
        for (size_t i = 0; i < newSize; ++i)
        {
            __uint32_t mergedValue = 0;
            for (size_t j = i * 8; j < (i + 1) * 8; ++j)
            {
                mergedValue += copied_sampleCells[j];
            }
            count += mergedValue;
            mergedCells[i] = mergedValue;
        }
        logger.log("INFO: sample level size: " + std::to_string(newSize) + " count: " + std::to_string(count));
        copied_sampleCells = std::move(mergedCells);
    }
    auto copied_originalCells = originalCells;
    while (copied_originalCells.size() >= 1)
    {
        cell_num_in_diff_level.push_back(copied_originalCells);
        size_t newSize = copied_originalCells.size() / 8;
        if (newSize == 0)
        {
            break;
        }
        size_t count = 0;
        std::vector<__uint64_t> mergedCells(newSize);
        for (size_t i = 0; i < newSize; ++i)
        {
            __uint32_t mergedValue = 0;
            for (size_t j = i * 8; j < (i + 1) * 8; ++j)
            {
                mergedValue += copied_originalCells[j];
            }
            count += mergedValue;
            mergedCells[i] = mergedValue;
        }
        logger.log("INFO: original level size: " + std::to_string(newSize) + " count: " + std::to_string(count));
        copied_originalCells = std::move(mergedCells);
    }

    assert(cell_num_in_diff_level.size() == sample_cell_num_in_diff_level.size());
    for (int i = 0; i < cell_num_in_diff_level.size(); ++i)
    {
        assert(cell_num_in_diff_level[i].size() == sample_cell_num_in_diff_level[i].size());
    }
    std::reverse(cell_num_in_diff_level.begin(), cell_num_in_diff_level.end());
    std::reverse(sample_cell_num_in_diff_level.begin(), sample_cell_num_in_diff_level.end());
}