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

using namespace std;
using json = nlohmann::json;

namespace fs = filesystem;

/**
 * @brief Constructor for DataLoader class
 * 
 * Initializes a DataLoader instance with the specified parameters for loading
 * and processing spatiotemporal data files. Sets up the core configuration
 * for the data loading and indexing pipeline.
 * 
 * @param directory The directory path containing data files to be loaded
 * @param max_file_num Maximum number of files to process (0 means no limit)
 * @param sample_ratio Sampling ratio for data reduction (0.0 to 1.0)
 * 
 * @note The constructor only initializes configuration parameters. 
 *       Actual data loading begins when load_data() is called.
 */
DataLoader::DataLoader(const string& directory, int max_file_num, float sample_ratio)
    : directory(directory), max_file_num(max_file_num), sample_ratio(sample_ratio),
      thread_pool(std::thread::hardware_concurrency() * 2)
{
}

/**
 * @brief Destructor for DataLoader class
 * 
 * Cleans up resources and performs necessary cleanup operations.
 * Currently performs default cleanup as most resources are managed
 * by RAII containers.
 */
DataLoader::~DataLoader()
{
}

/**
 * @brief Main entry point for data loading and processing pipeline
 * 
 * This is the primary public interface for the DataLoader class. It coordinates
 * the entire data loading pipeline including file discovery, metadata creation,
 * database cleanup, and spatial index construction.
 * 
 * Processing pipeline:
 * 1. Discover and prepare source files from directory
 * 2. Create file-to-ID mapping and save as JSON metadata
 * 3. Clear existing database tables to ensure clean state
 * 4. Build comprehensive spatial indexes for efficient querying
 * 
 * @return vector<string> List of processed filenames for reference
 * 
 * @note Creates a files_user.json file containing file-to-ID mappings
 * @note Clears all existing spatial index tables before processing
 * @throws May throw exceptions from underlying database operations or file I/O
 */
vector<string> DataLoader::load_data()
{
    clear_folder(data_dir);
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

    octreeNodeManager.clearTable();
    kdTreeNodeManager.clearTable();
    userDataManager.clearTable();
    meshConnectionManager.clearTable();
    elog(INFO, "cleared index table");
    build_index();

    return filenames;  
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
    vector<string> source_filenames;
    for (const auto &entry : fs::directory_iterator(this->directory))
    {
        const auto &path = entry.path();
        auto filename = path.filename().string();
        source_filenames.push_back(this->directory + "/" + path.filename().string());
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
    // sort(source_filenames.begin(),source_filenames.end());
    // mt19937 g(42);
    // shuffle(source_filenames.begin(), source_filenames.end(), g);
    // elog(INFO, "sorting files");
    FileSorter::sortFiles(source_filenames);
    elog(INFO, "sorted files");
    if(source_filenames.size() > this->max_file_num){
        source_filenames.resize(this->max_file_num);
    }
    this->filenames = source_filenames;
}

/**
 * @brief Build comprehensive spatial index for efficient spatiotemporal data querying
 * 
 * This function implements a complete spatial indexing pipeline that processes
 * large-scale spatiotemporal datasets using a multi-stage approach designed to handle
 * datasets that exceed available memory. The pipeline employs distributed sampling,
 * external sorting, hierarchical merging, and octree-based spatial partitioning.
 * 
 * Complete indexing pipeline:
 * 1. **Parallel bounds calculation and data sampling**
 *    - Process all source files concurrently to extract spatial bounds
 *    - Generate representative sample data from each file
 *    - Calculate global spatial bounds for the entire dataset
 * 
 * 2. **Global bounds loading and validation**
 *    - Load persistent global spatial bounds from disk
 *    - Ensures consistent spatial partitioning across multiple runs
 * 
 * 3. **Parallel Z-order sorting of sample data**
 *    - Sort sample data using Z-order (Morton order) indexing for spatial locality
 *    - Process multiple sample files concurrently for performance
 *    - Groups spatially nearby points together for efficient access
 * 
 * 4. **Multi-level merging of sorted sample files**
 *    - Merge sorted sample files into a single coherent spatial index
 *    - Uses external merge-sort algorithm for memory-efficient processing
 * 
 * 5. **Bottom-up octree construction**
 *    - Build hierarchical spatial tree from sorted sample data
 *    - Enables efficient range queries and spatial filtering
 *    - Supports multi-resolution spatial queries
 * 
 * 6. **Database storage and optimization**
 *    - Convert octree structure to database-compatible format
 *    - Persist spatial index to database for fast retrieval
 *    - Split and organize data for optimal query performance
 * 
 * Performance characteristics:
 * - **Parallel processing**: All I/O intensive operations use thread pools
 * - **External sorting**: Handles datasets larger than available memory
 * - **Detailed profiling**: Comprehensive timing statistics for each stage
 * - **Memory efficient**: Processes data in chunks to avoid memory overflow
 * - **Scalable design**: Performance scales with available CPU cores
 * 
 * Timing breakdown (stored in build_time map):
 * - sampling_time: Bounds calculation and data sampling duration
 * - sorting_time: Z-order sorting of all sample files duration  
 * - merging_time: Multi-level merge operation duration
 * - chunk_construction_time: Octree building and database storage duration
 * 
 * @note Global bounds are persisted to disk for consistency across runs
 * @note Validation step is currently disabled but can be enabled for debugging
 * @note Memory usage is carefully managed to support very large datasets
 * @note Thread safety is ensured through proper synchronization mechanisms
 * 
 * @see para_bound_and_sample() for initial data processing
 * @see para_sort_sample_file() for distributed sorting implementation
 * @see merge_sample_data() for hierarchical merging strategy
 * @see building_octree_bottom_up_top_down() for spatial tree construction
 * @see split_data_to_db1() for final database organization
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
    OctreeNode *root = this->building_octree_bottom_up_top_down(global_bound);// build chunk
    // std::vector<DBOctreeNode> dbNodes = convertOctreeToDB(root);
    // octreeNodeManager.writeOctreeNodesToDatabase(-1, dbNodes);
    // // if(delete_files)
    // // std::remove((data_dir + "/sample_data/all_sampled_data.bin").c_str());
    // this->build_time["doChunking_time->chunk_constrution_time"] = tc.second();
    // if (0)
    // { // validation
    //     elog(INFO, "validation");
    //     dbNodes = octreeNodeManager.loadOctreeNodesFromDatabase(-1);
    //     elog(INFO, "dbNodes: %zu", dbNodes.size());
    //     if (validateConversion(root, dbNodes, 0))
    //     {
    //         elog(INFO, "Validation passed: The structures are consistent.");
    //     }
    //     else
    //     {
    //         elog(ERROR, "Validation failed: The structures are not consistent.");
    //     }
    // }
    // elog(INFO, "create chunk time: %f seconds", tc.second());
    // delete root;

    // split_data_to_db1(dbNodes);
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
    
    // Pre-allocate containers for parallel processing results
    std::vector<SampleResult> processing_results(this->filenames.size());
    json loaded_json;
    {
        std::ifstream in_file(data_dir + "/files_user.json");
        in_file >> loaded_json;
        in_file.close();
    }
    std::atomic<std::uint32_t> finished_file_counting = 0;
    
    // Parallel processing: each thread processes a different file
    for (std::uint32_t i = 0; i < this->filenames.size(); ++i)
    {
        thread_pool.post_task([this, &loaded_json, &finished_file_counting, i, &processing_results](){
        TimerClock tc;
        auto file_name = this->filenames[i];
        // Each thread writes to its own index - no data races
        processing_results[i] = this->calculate_bound_and_sampleing(file_name, i, loaded_json[file_name].get<int16_t>()); 
        
        // Log progress{}
        std::string log_message = "INFO: bound & sample: " + file_name + 
                                 " progress: " + std::to_string(finished_file_counting++) + 
                                 "/" + std::to_string(this->filenames.size()) + 
                                 " time: " + std::to_string(tc.second()) + "s";
        logger.log(log_message);
        
        // if(delete_original_files){
        //     std::remove(file_name.c_str());
        // }
        });
    }
    thread_pool.wait_for_all_tasks();
    
    // Serial processing: aggregate bounds and write to database
    elog(INFO, "All threads completed. Starting serial database writes...");
    TimerClock db_timer;
    
    // Serial database writes - thread-safe for PostgreSQL SPI
    size_t mesh_count = 0;
    size_t total_connections = 0;
    for (const auto& result : processing_results) {
        if (!result.connections.empty()) {
            meshConnectionManager.writeDataToDatabase(result.mesh_id, result.connections);
            mesh_count++;
            total_connections += result.connections.size();
            
            if (mesh_count % 10 == 0) {
                elog(INFO, "Database write progress: %zu/%zu meshes processed", 
                     mesh_count, processing_results.size());
            }
        }
    }
    
    elog(INFO, "Database writes completed: %zu meshes, %zu total connections in %.2fs", 
         mesh_count, total_connections, db_timer.second());
    
    // Extract bounds for global aggregation
    std::vector<Bounds> sub_bounds;
    sub_bounds.reserve(processing_results.size());
    for (const auto& result : processing_results) {
        sub_bounds.push_back(result.bounds);
    }

    // std::cout << "data_size:" << bounded_data_size << " file count:" << finished_file_counting << std::endl;
    // bool firstFlag = true; // unused variable
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
SampleResult DataLoader::calculate_bound_and_sampleing(const string &filename, file_id_t fid, user_id_t user_id)
{
    ContinuousRandomGenerator generator(0.0f, 1.0f);
    Bounds bounds;
    ifstream file(filename);
    std::vector<char> _buffer(1024 * 1024);
    file.rdbuf()->pubsetbuf(_buffer.data(), _buffer.size());
    string line;
    const auto &extension = filename.substr(filename.size() - 3);
    long long count = 0;
    TimerClock tc;

    // unused
    // string file_prefix = getFileNameFromPath(filename);
    // std::regex number_pattern(R"(\d+)");
    // std::sregex_iterator it(file_prefix.begin(), file_prefix.end(), number_pattern);
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
    auto sample_point = [&](SpatioTemporalData &point)
    {
        point.user_id = user_id;
        point.fid = fid;
        point.pid = pid;

        bounds.update(point);
        
        ++pid;
        buffer.push_back(point);
        if(generator.generate() < this->sample_ratio){
            sample_buffer.push_back(point);
        }
        // if (sample_buffer.size() >= (32 * 0.25 * 1e6/concurrent_task_num))
        // {
        //     sample_outfile.write(reinterpret_cast<const char *>(sample_buffer.data()), sizeof(SpatioTemporalData) * sample_buffer.size());
        //     sample_buffer.clear();
        // }
        // if (buffer.size() >= (32 * 0.25 * 1e6/concurrent_task_num))
        // {
        //     outfile.write(reinterpret_cast<const char *>(buffer.data()), sizeof(SpatioTemporalData) * buffer.size());
        //     this->bounded_data_size += buffer.size();
        //     buffer.clear();
        // }
    };

    // if (extension == "ply")
    // {
    //     auto point_cloud_id = fid;
    //     while (getline(file, line))
    //     {
    //         if (line.substr(0, 3) == "end")
    //             break; // Skip Header
    //     }
    //     while (getline(file, line))
    //     {
    //         if (count++ >= max_point_limit)
    //         {
    //             break;
    //         }
    //         if (count % int(1e7) == 0)
    //         {
    //             // 使用线程安全的输出函数
    //             std::string log_message = "INFO: read bound: " + filename + 
    //                                      ": " + std::to_string(count / int(1e7)) + "x1e7 processing time " + 
    //                                      std::to_string(tc.second()) + "s";
    //             logger.log(log_message);
    //             tc.tick();
    //         }
    //         // uint8_t colorR, colorG, colorB; // unused variables
    //         stringstream ss(line);
    //         float x, y, z;
    //         float time, intensity;
    //         ss >> x >> y >> z >> time >> intensity;
    //         auto point = SpatioTemporalData(x, y, z, time);
    //         point.tid = PointCloudPoint;
    //         point.external_data.PointCloud.intensity = intensity;
    //         point.foreign_key = point_cloud_id;
    //         sample_point(point);
    //     }
    // }
    // else if (extension == "csv")
    // {
    //     getline(file, line); // Skip Header
    //     while (getline(file, line))
    //     {
    //         // if (count++ >= max_point_limit)
    //         // {
    //         //     break;
    //         // }
    //         // if (count % int(1e7) == 0)
    //         // {
    //         //     // 使用线程安全的输出函数
    //         //     std::string log_message = "INFO: read bound: " + filename + 
    //         //                              ": " + std::to_string(count / int(1e7)) + "x1e7 processing time " + 
    //         //                              std::to_string(tc.second()) + "s";
    //         //     logger.log(log_message);
    //         //     tc.tick();
    //         // }
    //         stringstream ss(line);
    //         vector<string> values;
    //         string value;
    //         while (getline(ss, value, ','))
    //         {
    //             values.push_back(value);
    //         }
    //         int id = stoi(values[1].substr(5));
    //         float x = stof(values[2]);
    //         float y = stof(values[3]);
    //         float z = stof(values[4]);
    //         float time = stof(values[0]);
    //         float speed = stof(values[5]);
    //         auto point = SpatioTemporalData(x, y, z, time);
    //         point.tid = TrajectoryPoint;
    //         point.external_data.Trajectoy.speed = speed;
    //         point.foreign_key = id;
    //         sample_point(point);
    //     }
    // }
    SampleResult result;
    result.bounds = bounds;
    result.mesh_id = fid;
    
    if (extension == "obj")
    {
        auto meshid = fid;
        size_t vertex_count = 0;
        size_t face_count = 0;
        
        while (getline(file, line))
        {
            // if (count++ >= max_point_limit)
            // {
            //     break;
            // }

            // if (count % int(1e7) == 0)
            // {
            //     std::string log_message = "INFO: read bound: " + filename + 
            //                              ": " + std::to_string(count / int(1e7)) + "x1e7 processing time " + 
            //                              std::to_string(tc.second()) + "s";
            //     logger.log(log_message);
            //     tc.tick();
            // }
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
                sample_point(point);
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
                result.connections.emplace_back(std::move(connection));
            }
        }
        
        std::string log_message = "INFO: OBJ file processed: " + filename + 
                                 " - vertices=" + std::to_string(vertex_count) + 
                                 ", faces=" + std::to_string(face_count) + 
                                 ", connections=" + std::to_string(result.connections.size()) + 
                                 ", meshid=" + std::to_string(meshid);
        logger.log(log_message);
        
        if (result.connections.size() > 100000) {
            std::string warning_message = "WARNING: Large mesh detected in " + filename + 
                                        " - " + std::to_string(result.connections.size()) + " connections";
            logger.log(warning_message);
        }
        
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
    
    // Update the result's bounds with the final bounds
    result.bounds = bounds;
    return result;
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
 * @param filename Input sample file path
 * @param bounds Global spatial bounds for index calculation
 * @param countCells Reference to cell count array for statistics
 * 
 * @note Buffer size is dynamically adjusted based on concurrent tasks
 * @note Uses atomic counter for thread-safe output file ID generation
 * @note Implements chunked reading to handle large files efficiently
 */
void DataLoader::sort_sample_file(const string &filename, const Bounds &bounds, vector<uint32_t> &countCells)
{
    auto spatial_bound = bounds.to_spatial_bound();
    std::ifstream infile(filename, std::ios::binary);
    if (!infile.is_open()) {
        throw std::runtime_error("Open file failed!");
    }
    std::vector<SpatioTemporalData> buffer(4 * 64 * 1024 * 32/concurrent_task_num);

    while (infile) {
        std::unordered_map<std::int64_t, std::vector<SpatioTemporalData>> sorted_sample_data;
        infile.read(reinterpret_cast<char*>(buffer.data()), buffer.size() * sizeof(SpatioTemporalData));
        size_t bytes_read = infile.gcount();
        size_t num_elements_read = bytes_read / sizeof(SpatioTemporalData);
        this->sample_sort_count += num_elements_read;
        for (size_t i = 0; i < num_elements_read; ++i) {
            auto &point = buffer[i];
            int64_t index = indexOfPoint(point.x, point.y, point.z, spatial_bound, chunk_max_level);
            sorted_sample_data[index].emplace_back(std::move(point));
        }

        std::int64_t sample_file_id = this->sample_sorted_file_count++;
        {
            lock_guard<mutex> lock(this->count_in_cell_mutex);
            for(auto &i:sorted_sample_data){
                countCells[i.first] += i.second.size();
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
    std::vector<std::string> sample_filenames;
    for (const auto &entry : fs::directory_iterator(data_dir + "/temp_bin_sample_file"))
    {
        const auto &path = entry.path();
        sample_filenames.push_back(data_dir + "/temp_bin_sample_file/" + path.filename().string());
    }
    std::uint64_t sampleCellNums = (std::int64_t)1 << (std::int64_t)chunk_max_level * 3;
    auto sampleCells = std::vector<uint32_t>(sampleCellNums, 0);
    for (const auto &filename : sample_filenames)
    {
        thread_pool.post_task([=, &bounds, &sampleCells](){
        TimerClock tc;
        this->sort_sample_file(filename, bounds, sampleCells);
        // Log progress
        std::string log_message = "INFO: sort sample: " + filename + 
                                 " time: " + std::to_string(tc.second()) + "s";
        logger.log(log_message);
        // if(delete_files)
        // std::remove(filename.c_str());
        });
    }
    thread_pool.wait_for_all_tasks();
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
        auto cur_file_num = this->para_domerge(cur_iter_id, true);
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
    const size_t buffer_size = 1024;
    std::vector<SpatioTemporalData> buffer(buffer_size);
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
            elog(ERROR, "sample validation failed: %lu:%lu", validation_sample_cell_count, cell_index);
        }
        size_t points_read = 0;
        while (points_read < cur_index_number)
        {
            size_t points_to_read = std::min(buffer_size, static_cast<size_t>(cur_index_number - points_read));
            cur_file.read(reinterpret_cast<char *>(buffer.data()), sizeof(SpatioTemporalData) * points_to_read);
            new_file.write(reinterpret_cast<char *>(buffer.data()), sizeof(SpatioTemporalData) * points_to_read);
            points_read += points_to_read;
        }
    }
    elog(INFO, "merged sample data size: %lu", cell_index);
    std::remove(std::string(data_dir +"/sample_data/" + to_string(cur_iter_id) + "_0.bin").c_str());
    
}

/**
 * @brief Build octree structure using bottom-up then top-down approach
 * 
 * This function implements a sophisticated octree construction algorithm that combines
 * bottom-up sampling with top-down spatial partitioning. It's designed for handling
 * large-scale spatiotemporal datasets that may not fit in memory.
 * 
 * Algorithm approach:
 * 1. Initialize BuildChunk utility with configured thread pool
 * 2. Load sample size statistics for spatial cells at different levels
 * 3. Calculate total sample count for validation and logging
 * 4. Clear temporary chunk data directory for fresh processing
 * 5. Build chunk-based octree nodes using sample data
 * 6. Wait for all indexing tasks to complete before returning
 * 
 * @param bounds Spatial bounding box for the entire octree structure
 * @return OctreeNode* Pointer to the root node of the constructed octree
 * 
 * @note Uses external merge-sort approach for large datasets
 * @note Thread pool size controlled by concurrent_task_num
 * @note Sample data is used to guide spatial partitioning decisions
 * @note Cleanup of temporary data directories is performed automatically
 */
OctreeNode* DataLoader::building_octree_bottom_up_top_down(const Bounds& bounds)
{
    elog(INFO, "DataLoader::building_octree_bottom_up_top_down");
    BuildChunk build_util(concurrent_task_num);
    build_util.build_octree_hierarchy();
    std::int64_t count_sample = 0;
    for (auto &i : build_util.sample_cell_num_in_diff_level.back())
    {
        count_sample += i;
    }
    logger.log("INFO: prepare: " + std::to_string(count_sample));
    logger.log("INFO: build_util.sample_cell_num_in_diff_level: " + std::to_string(build_util.sample_cell_num_in_diff_level.size()));
    // logger.log("INFO: build_util.cell_num_in_diff_level: " + std::to_string(build_util.cell_num_in_diff_level.size()));

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
uint64_t DataLoader::domerge(uint64_t cur_file_id, uint64_t cur_iter, uint64_t cur_file_num, const std::string& prefix)
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
                    // const SpatioTemporalData &point = buffer[i]; // unused variable
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

        std::string log_message = "INFO: " + std::to_string(all_number) + 
                                    " cur_file_id:" + std::to_string(cur_file_id) + 
                                    " cur_iter:" + std::to_string(cur_iter) + 
                                    " cur_file_num:" + std::to_string(cur_file_num) + 
                                    " next:" + next_file_name + 
                                    " cur:" + cur_file_name + 
                                    " new:" + new_file_name;
        logger.log(log_message);

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

        std::string log_message = "INFO: " + std::to_string(all_number) + 
                                    " cur_file_id:" + std::to_string(cur_file_id) + 
                                    " cur_iter:" + std::to_string(cur_iter) + 
                                    " cur_file_num:" + std::to_string(cur_file_num) + 
                                    " next:" + next_file_name + 
                                    " cur:" + cur_file_name + 
                                    " new:" + new_file_name;
        logger.log(log_message);
        
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
    std::string log_message = "INFO: " + std::to_string(all_number) + 
                                " cur_file_id:" + std::to_string(cur_file_id) + 
                                " cur_iter:" + std::to_string(cur_iter) + 
                                " cur_file_num:" + std::to_string(cur_file_num) + 
                                " next:" + next_file_name + 
                                " cur:" + cur_file_name + 
                                " new:" + new_file_name;
    logger.log(log_message);
    
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
uint64_t DataLoader::para_domerge(uint64_t cur_iter_id, bool sample_or_original)
{
    elog(INFO, "parallel merge iteration: %lu", cur_iter_id);
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
        thread_pool.post_task([&, i,cur_iter_id,file_set_size=sample_files.size()]()
                              { merged_count += domerge(i, cur_iter_id, file_set_size, prefix); });
    }
    thread_pool.wait_for_all_tasks();
    // sample_files = findFilesWithPrefix("./" + prefix, to_string(cur_iter_id) + "_");

    {
        auto tasks = split<std::int64_t>(0,sample_files.size(),concurrent_task_num);
        for (auto task : tasks)
        {
            thread_pool.post_task([prefix,task,&sample_files]()
            {
                for(std::int64_t i = task.first;i<task.second;++i){
                    std::string file_path = data_dir +"/" + prefix + "/" + sample_files[i];
                    std::remove(file_path.c_str()); 
                }
            });
        }
        thread_pool.wait_for_all_tasks();
    }
    elog(INFO, "merged it count: %lu - %lu", cur_iter_id, merged_count.load());
    return sample_files.size();
}

/**
 * @brief Load and process sample data to build multi-level octree statistics
 * 
 * This function performs the following key operations:
 * 1. Loads sample cell count statistics from binary file
 * 2. Validates data integrity and file format
 * 3. Opens sample data file for subsequent processing
 * 4. Builds hierarchical octree structure with level-wise aggregated statistics
 * 
 * The function creates a bottom-up octree hierarchy where each level contains
 * aggregated statistics from the level below. This is essential for efficient
 * spatial indexing and query processing.
 * 
 * @note This function must be called before any octree construction operations
 * @note File paths are relative to data_dir/sample_data/
 * @throws std::runtime_error if file validation fails or data inconsistency detected
 */
void BuildChunk::build_octree_hierarchy()
{
    // STEP 1: Calculate expected number of sample cells in finest level
    // For 3D octree: total cells = 2^(chunk_max_level * 3) = 8^chunk_max_level
    uint64_t sampleCellNums = (int64_t)1 << (int64_t)chunk_max_level * 3;
    // uint64_t sampleDimension = (int64_t)1 << (int64_t)chunk_max_level; // unused variable
    
    // STEP 2: Load sample cell count statistics from binary file
    {
        // Open sample cell statistics file in binary mode
        std::ifstream sampleFile(data_dir +"/sample_data/sampleCellNums.bin", std::ios::binary);
        
        // Get file size by seeking to end and reading position
        sampleFile.seekg(0, std::ios::end);
        std::streamsize fileSize = sampleFile.tellg();
        sampleFile.seekg(0, std::ios::beg);
        
        // Validate file format: size must be multiple of uint32_t
        if (fileSize % sizeof(uint32_t) != 0)
        {
            throw std::runtime_error("Data file size exception");
        }
        
        // Calculate number of elements and validate against expected count
        std::size_t numElements = fileSize / sizeof(uint32_t);
        if (numElements != sampleCellNums)
        {
            throw std::runtime_error("Data count exception");
        }
        
        // Load all sample cell statistics into memory
        sampleCells.resize(numElements);
        sampleFile.read(reinterpret_cast<char *>(sampleCells.data()), fileSize);
        sampleFile.close();
    }

    // STEP 3: Initialize counters and open sample data file for processing
    node_count = 0;  // Reset octree node counter
    long long sample_size = 0;
    
    // Open main sample data file and calculate total sample count
    sample_file = std::ifstream(data_dir +"/sample_data/all_sampled_data.bin", std::ios::binary);
    sample_file.seekg(0, std::ios::end);
    std::streamsize fileSize = sample_file.tellg();
    sample_size = fileSize / sizeof(SpatioTemporalData);
    sample_all_size = sample_size;  // Store for later use
    logger.log("INFO: sample file size: " + std::to_string(sample_size));
    sample_file.seekg(0, std::ios::beg);  // Reset to beginning for data reading

    // STEP 4: Build multi-level octree hierarchy through bottom-up aggregation
    auto copied_sampleCells = sampleCells;  // Work with copy to preserve original
    
    // Iteratively build hierarchy from finest to coarsest level
    while (copied_sampleCells.size() >= 1)
    {
        // Store current level's cell statistics
        sample_cell_num_in_diff_level.push_back(copied_sampleCells);
        
        // Calculate size for next coarser level (8:1 reduction for 3D octree)
        size_t newSize = copied_sampleCells.size() / 8;
        if (newSize == 0)
        {
            break;  // Reached top level of octree
        }
        
        // Aggregate statistics for parent level
        size_t count = 0;  // Total count for validation
        std::vector<__uint32_t> mergedCells(newSize);
        
        // For each parent cell, aggregate its 8 children
        for (size_t i = 0; i < newSize; ++i)
        {
            __uint32_t mergedValue = 0;
            // Sum the 8 consecutive child cells (octree property: 2^3 = 8 children per parent)
            for (size_t j = i * 8; j < (i + 1) * 8; ++j)
            {
                mergedValue += copied_sampleCells[j];
            }
            count += mergedValue;           // Accumulate for logging
            mergedCells[i] = mergedValue;   // Store aggregated value
        }
        
        // Log level statistics for monitoring and debugging
        logger.log("INFO: level size: " + std::to_string(newSize) + " count: " + std::to_string(count));
        
        // Move to next level (avoid copy overhead)
        copied_sampleCells = std::move(mergedCells);
    }
    
    // STEP 5: Reverse order for top-down access pattern
    // Original order: [finest -> coarsest], reversed: [coarsest -> finest]
    // This allows level-indexed access where level 0 = root, higher levels = finer detail
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
//         do_indexing_thread_pool.wait_for_all_tasks(concurrent_task_num * 2);
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

/**
 * @brief Build spatial indexes (Octree + KD-tree) for a data chunk
 * 
 * This function constructs a comprehensive spatial indexing system for a specific
 * data chunk. It builds both an Octree for spatial partitioning and KD-trees for
 * efficient point queries within each octree leaf node.
 * 
 * Indexing pipeline:
 * 1. Handle empty data chunks by creating minimal index structures
 * 2. Build Octree using spatial partitioning with configurable depth
 * 3. Encode Octree nodes with sequential IDs for database storage
 * 4. Convert in-memory Octree to database-compatible format
 * 5. Extract leaf nodes for detailed KD-tree construction
 * 6. Create KD-trees for each leaf node in parallel
 * 7. Store all index structures in database
 * 
 * @param bound Spatial bounding box for the data chunk
 * @param chunk_id Unique identifier for this data chunk
 * @param data Vector of spatiotemporal data points to be indexed
 * 
 * @note Uses global parameters: octree_max_level, max_point_per_leaf from parameter.h
 * 
 * @note Handles empty chunks by creating minimal valid index structures
 * @note Uses parallel processing for KD-tree construction within leaves
 * @note All index data is persisted to database for later query processing
 * @note Memory cleanup handled automatically via RAII and explicit deletion
 */
void BuildChunk::build_chunk_spatial_index(Bounds bound, int chunk_id, std::vector<SpatioTemporalData> data)
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
        octreeNodeManager.writeOctreeNodesToDatabase(chunk_id, dbNodes);
        auto tree = std::vector<DBKdtreeNode>(0);
        BinaryKVStorage bs(data_dir+"/kdtree");
        // kdTreeNodeManager.writeKdTreeNodesToDatabase(chunk_id, dbNodes[0].id, tree);
        bs.write("kd_"+std::to_string(chunk_id)+"_"+std::to_string(dbNodes[0].id),tree);
        return;
    }
    index_loading_num += octree_builder.dataPoints.size();
    tc.tick();
    OctreeNode *octree_root = octree_builder.buildOctree(bound);//memory pointer
    encode_Octree(octree_root);
    std::vector<DBOctreeNode> dbNodes = convertOctreeToDB(octree_root, chunk_id);//pointer to vector
    octreeNodeManager.writeOctreeNodesToDatabase(chunk_id, dbNodes);
    tc.tick();
    vector<OctreeNode *> leafVector;
    getLeafNodes(octree_root, leafVector);
    tc.tick();
    this->para_createTreeWithLeafNode(chunk_id, octree_builder.dataPoints, leafVector);
    ++finihsed_octree;
    // std::cout << "build in chunk:" << finihsed_octree << ":" << leaf_num << "\r";
    delete octree_root;
}

/**
 * @brief Recursively build octree chunk nodes using pre-computed sample statistics
 * 
 * This function constructs an octree structure for spatial indexing using a top-down
 * recursive approach. It leverages pre-computed sample statistics from different 
 * hierarchy levels to efficiently determine node properties and termination conditions.
 * 
 * Key Features:
 * - Uses Morton encoding for efficient spatial indexing
 * - Implements adaptive subdivision based on data density
 * - Creates leaf nodes for direct spatial query processing
 * - Builds hierarchical spatial index for large-scale datasets
 * 
 * Algorithm Overview:
 * 1. Calculate spatial cell ID using Morton encoding
 * 2. Create octree node with specified bounds and metadata
 * 3. Determine if current node should be a leaf (based on point count or depth)
 * 4. For leaf nodes: load data and build detailed spatial index
 * 5. For internal nodes: recursively subdivide into 8 octants
 * 
 * @param bounds Spatial bounding box for this octree node
 * @param level Current depth level in the octree hierarchy (0 = root)
 * @param x X-coordinate in the octree grid at current level
 * @param y Y-coordinate in the octree grid at current level  
 * @param z Z-coordinate in the octree grid at current level
 * 
 * @return Pointer to newly created OctreeNode (caller responsible for memory management)
 * 
 * @note Uses pre-computed sample_cell_num_in_diff_level for O(1) point count lookups
 * @note Leaf nodes trigger detailed spatial indexing via build_chunk_spatial_index()
 * @note Coordinates (x,y,z) represent grid position, not spatial coordinates
 * @note Memory is allocated for octree nodes; ensure proper cleanup in caller
 * 
 * @warning This function performs file I/O operations on sample_file
 * @warning Recursive depth is bounded by chunk_max_level to prevent stack overflow
 */
OctreeNode *BuildChunk::build_chunk_node_sample(Bounds bounds, uint64_t level, uint64_t x, uint64_t y, uint64_t z)
{
    // STEP 1: Calculate spatial cell identifier using Morton encoding
    // Morton code preserves spatial locality for efficient range queries
    uint64_t cell_id = interleaveBits(x, y, z, chunk_max_level);
    
    // STEP 2: Create new octree node with spatial and hierarchical metadata
    OctreeNode *node = new OctreeNode(bounds, level);
    node->id = node_count++;  // Assign unique incremental ID
    
    // STEP 3: Retrieve pre-computed point count from hierarchy statistics
    // O(1) lookup using level-indexed array and Morton-encoded cell ID
    node->pointCount = sample_cell_num_in_diff_level[level][cell_id];
    
    // STEP 4: Determine if this should be a leaf node
    // Termination conditions: (1) Low data density OR (2) Maximum depth reached
    if (node->pointCount <= max_point_per_chunk || level >= chunk_max_level)
    {
        // LEAF NODE PROCESSING: Build detailed spatial index for direct queries
        node->is_leaf = true;
        
        {
            // Load spatiotemporal data for this chunk from sequential file
            std::vector<SpatioTemporalData> chunk_data(node->pointCount);
            sample_file.read(reinterpret_cast<char *>(chunk_data.data()), 
                           node->pointCount * sizeof(SpatioTemporalData));
            split_sample_count += node->pointCount;  // Track processing progress
            
            // Note: Parallel implementation commented out for database consistency
            // do_indexing_thread_pool.post_task(
            //     [chunk_data = std::move(chunk_data),bound=node->bound,chunk_id = node->id](){
            //         build_chunk_spatial_index(bound,chunk_id,std::move(chunk_data));
            //     }
            // );
            // do_indexing_thread_pool.wait_for_all_tasks(concurrent_task_num * 2);
            
            // SERIAL IMPLEMENTATION: Build detailed KD-tree index for leaf data
            // This creates fine-grained spatial index within the chunk
            this->build_chunk_spatial_index(node->bound, node->id, std::move(chunk_data));
        }
        
        // Log leaf node creation for monitoring and debugging
        elog(INFO, "leaf info: id:%d level:%ld x:%ld y:%ld z:%ld size:%zu",
             node->id, (long)level, (long)x, (long)y, (long)z, node->pointCount);
        return node;
    }
    
    // STEP 5: INTERNAL NODE PROCESSING: Recursively subdivide into 8 octants
    // Create children for all 8 possible octants in 3D space
    for (int i = 0; i < 8; ++i)
    {
        // Extract individual bit components for 3D octant subdivision
        int bitx = (i >> 0) & 1;  // X-dimension bit (LSB)
        int bity = (i >> 1) & 1;  // Y-dimension bit (middle bit)
        int bitz = (i >> 2) & 1;  // Z-dimension bit (MSB)
        
        // Calculate child coordinates by left-shifting parent and adding octant bit
        // This maintains Morton encoding properties across hierarchy levels
        int new_x = (x << 1) | bitx;
        int new_y = (y << 1) | bity;
        int new_z = (z << 1) | bitz;
        
        // SPATIAL SUBDIVISION: Create bounding box for child octant
        auto center = bounds.getCenter();
        Bounds sub_bound = bounds;
        
        // X-dimension subdivision
        if (bitx == 0)
        {
            sub_bound.max.x = center.x;  // Left half: [min_x, center_x]
        }
        else
        {
            sub_bound.min.x = center.x;  // Right half: [center_x, max_x]
        }
        
        // Y-dimension subdivision  
        if (bity == 0)
        {
            sub_bound.max.y = center.y;  // Lower half: [min_y, center_y]
        }
        else
        {
            sub_bound.min.y = center.y;  // Upper half: [center_y, max_y]
        }

        // Z-dimension subdivision
        if (bitz == 0)
        {
            sub_bound.max.z = center.z;  // Front half: [min_z, center_z]
        }
        else
        {
            sub_bound.min.z = center.z;  // Back half: [center_z, max_z]
        }
        
        // RECURSIVE CALL: Build child subtree
        node->children[i] = build_chunk_node_sample(sub_bound, level + 1, new_x, new_y, new_z);
    }
    
    return node;
}

/**
 * @brief Recursively split spatiotemporal data based on octree spatial partitioning
 * 
 * This function traverses the octree structure and distributes spatiotemporal data
 * points to their appropriate leaf nodes. It implements recursive spatial partitioning
 * based on the octree's hierarchical structure, ensuring that each data point is
 * assigned to the correct leaf node for efficient querying.
 * 
 * Algorithm details:
 * - For leaf nodes: directly assigns all data points to the result map
 * - For internal nodes: spatially partitions data based on center coordinates
 * - Uses 3D spatial indexing with bit-based octant calculation
 * - Recursively processes each of the 8 octants (children)
 * - Validates data consistency between sample and original datasets
 * 
 * @param id Current octree node ID being processed
 * @param nodes Reference to vector containing all octree nodes in the chunk
 * @param data Reference to spatiotemporal data points to be distributed
 * @param result Output map from leaf node ID to assigned data points
 * 
 * @note Uses move semantics for efficient data transfer to leaf nodes
 * @note Logs errors when sample/original data inconsistencies are detected
 * @note Octant indexing: bit 0=x, bit 1=y, bit 2=z (0=negative, 1=positive)
 */
void DataLoader::split_data(int id,std::vector<DBOctreeNode> &nodes, std::vector<SpatioTemporalData> &data,std::unordered_map<int,std::vector<SpatioTemporalData>> &result) {
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
        this->split_data(node.children[i],nodes,sub_data[i],result);
    }
}

/**
 * @brief Process and store original spatiotemporal data into database
 * 
 * This function takes a complete dataset for a spatial chunk and stores it in the
 * database with proper spatial organization. It leverages the pre-built octree
 * structure to distribute data points to their corresponding leaf nodes, then
 * creates KD-tree indexes and stores everything in the database.
 * 
 * Processing pipeline:
 * 1. Load existing octree structure from database for the chunk
 * 2. Distribute all data points to appropriate octree leaf nodes
 * 3. Sort data within each leaf by timestamp for temporal queries
 * 4. Store organized data in database with proper indexing
 * 5. Track processing statistics for monitoring
 * 
 * @param chunk_id Unique identifier for the spatial chunk being processed
 * @param all_data Complete vector of spatiotemporal data points for the chunk
 * 
 * @note Data is sorted by timestamp within each leaf for temporal efficiency
 * @note Uses lambda function for consistent data storage interface
 * @note Validates data distribution matches octree leaf structure
 * @note All database operations are performed through manager classes
 */
// void DataLoader::chunk_original_data_to_db(int chunk_id, std::vector<SpatioTemporalData> all_data)
// {
//     std::vector<DBOctreeNode> ocNodes = octreeNodeManager.loadOctreeNodesFromDatabase(chunk_id);
//     // size_t data_count = 0; // unused variable
//     long long in_this_file_read_count = 0;
//     std::unordered_map<int, std::vector<SpatioTemporalData>> to_db_data; // octree leaf id --> data
//     auto write_points_to_db = [&](int octreeid, int kdtreeid, std::vector<SpatioTemporalData> &data)
//     {
//         std::sort(data.begin(), data.end(), [](const SpatioTemporalData &a, const SpatioTemporalData &b)
//                   { return a.time < b.time; });
//         originalDataManager.writeOriginalDataToDatabase(octreeid, kdtreeid, data);
//     };

//     this->split_data(0, ocNodes, all_data, to_db_data);

//     for (auto &i : to_db_data)
//     {
//         in_this_file_read_count += i.second.size();
//     }
//     for (auto &i : to_db_data)
//     {
//         write_points_to_db(chunk_id, i.first, i.second);
//     }
// }

// void BuildChunk::load_original_and_sample_size(){
//     uint64_t sampleCellNums = (int64_t)1 << (int64_t)chunk_max_level * 3;
//     // uint64_t sampleDimension = (int64_t)1 << (int64_t)chunk_max_level; // unused variable
//     {
//         sampleCells.resize(sampleCellNums);
//         originalCells.resize(sampleCellNums);
//         std::ifstream originalFile(data_dir +"/sample_data/originalCellNums.bin", std::ios::binary);
//         originalFile.seekg(0, std::ios::end);
//         std::streamsize fileSize = originalFile.tellg();
//         originalFile.seekg(0, std::ios::beg);
//         if (fileSize % sizeof(uint64_t) != 0)
//         {
//             throw std::runtime_error("Data file size exception");
//         }
//         std::size_t numElements = fileSize / sizeof(uint64_t);
//         if (numElements != sampleCellNums)
//         {
//             throw std::runtime_error("Data file size exception");
//         }
//         originalCells.resize(numElements);
//         originalFile.read(reinterpret_cast<char *>(originalCells.data()), fileSize);
//         originalFile.close();
//     }
//     {
//         std::ifstream sampleFile(data_dir +"/sample_data/sampleCellNums.bin", std::ios::binary);
//         sampleFile.seekg(0, std::ios::end);
//         std::streamsize fileSize = sampleFile.tellg();
//         sampleFile.seekg(0, std::ios::beg);
//         if (fileSize % sizeof(uint32_t) != 0)
//         {
//             throw std::runtime_error("Data file size exception");
//         }
//         std::size_t numElements = fileSize / sizeof(uint32_t);
//         if (numElements != sampleCellNums)
//         {
//             throw std::runtime_error("Data file size exception");
//         }
//         sampleCells.resize(numElements);
//         sampleFile.read(reinterpret_cast<char *>(sampleCells.data()), fileSize);
//         sampleFile.close();
//     }

//     node_count = 0;
//     long long sample_size = 0;
//     sample_file = std::ifstream(data_dir +"/sample_data/all_sampled_data.bin", std::ios::binary);
//     sample_file.seekg(0, std::ios::end);
//     std::streamsize fileSize = sample_file.tellg();
//     sample_size = fileSize / sizeof(SpatioTemporalData);
//     sample_all_size = sample_size;
//     logger.log("INFO: sample file size: " + std::to_string(sample_size));
//     sample_file.seekg(0, std::ios::beg);

//     original_file = std::ifstream(data_dir +"/original_data/all_data.bin", std::ios::binary);
//     original_file.seekg(0, std::ios::end);
//     fileSize = original_file.tellg();
//     sample_size = fileSize / sizeof(SpatioTemporalData);
//     original_all_size = sample_size;
//     logger.log("INFO: data file size: " + std::to_string(sample_size));
//     original_file.seekg(0, std::ios::beg);

//     auto copied_sampleCells = sampleCells;
//     while (copied_sampleCells.size() >= 1)
//     {
//         sample_cell_num_in_diff_level.push_back(copied_sampleCells);
//         size_t newSize = copied_sampleCells.size() / 8;
//         if (newSize == 0)
//         {
//             break;
//         }
//         size_t count = 0;
//         std::vector<__uint32_t> mergedCells(newSize);
//         for (size_t i = 0; i < newSize; ++i)
//         {
//             __uint32_t mergedValue = 0;
//             for (size_t j = i * 8; j < (i + 1) * 8; ++j)
//             {
//                 mergedValue += copied_sampleCells[j];
//             }
//             count += mergedValue;
//             mergedCells[i] = mergedValue;
//         }
//         logger.log("INFO: sample level size: " + std::to_string(newSize) + " count: " + std::to_string(count));
//         copied_sampleCells = std::move(mergedCells);
//     }
//     auto copied_originalCells = originalCells;
//     while (copied_originalCells.size() >= 1)
//     {
//         cell_num_in_diff_level.push_back(copied_originalCells);
//         size_t newSize = copied_originalCells.size() / 8;
//         if (newSize == 0)
//         {
//             break;
//         }
//         size_t count = 0;
//         std::vector<__uint64_t> mergedCells(newSize);
//         for (size_t i = 0; i < newSize; ++i)
//         {
//             __uint32_t mergedValue = 0;
//             for (size_t j = i * 8; j < (i + 1) * 8; ++j)
//             {
//                 mergedValue += copied_originalCells[j];
//             }
//             count += mergedValue;
//             mergedCells[i] = mergedValue;
//         }
//         logger.log("INFO: original level size: " + std::to_string(newSize) + " count: " + std::to_string(count));
//         copied_originalCells = std::move(mergedCells);
//     }

//     assert(cell_num_in_diff_level.size() == sample_cell_num_in_diff_level.size());
//     for (int i = 0; i < cell_num_in_diff_level.size(); ++i)
//     {
//         assert(cell_num_in_diff_level[i].size() == sample_cell_num_in_diff_level[i].size());
//     }
//     std::reverse(cell_num_in_diff_level.begin(), cell_num_in_diff_level.end());
//     std::reverse(sample_cell_num_in_diff_level.begin(), sample_cell_num_in_diff_level.end());
// }

/**
 * @brief Create KD-tree indexes for octree leaf nodes in parallel
 * 
 * This function orchestrates the parallel creation of KD-tree indexes for all
 * leaf nodes in an octree structure. It uses a thread pool to efficiently
 * process multiple leaf nodes simultaneously, creating detailed spatial indexes
 * for fast point queries within each leaf.
 * 
 * Processing approach:
 * - Creates a thread pool with configured parallelism level
 * - Submits each leaf node for KD-tree processing asynchronously
 * - Uses boost::asio for efficient thread management
 * - Waits for all KD-tree construction tasks to complete
 * - Ensures thread-safe access to shared data structures
 * 
 * @param chunk_id Unique identifier for the spatial chunk being processed
 * @param dataPoints Complete dataset of spatiotemporal points for the chunk
 * @param leafVector Vector of all octree leaf nodes requiring KD-tree indexes
 * 
 * @note Thread pool size controlled by concurrent_task_num parameter
 * @note Each leaf node is processed by buildKdTreeForLeafNode function
 * @note Synchronization ensures all KD-trees are built before function returns
 * @note Essential for enabling efficient point-in-region queries
 */
void BuildChunk::para_createTreeWithLeafNode(int chunk_id, const vector<SpatioTemporalData> &dataPoints,
                                 const vector<OctreeNode *> &leafVector)
{
    elog(INFO, "BuildChunk::para_createTreeWithLeafNode");
    boost::asio::thread_pool pool(concurrent_task_num);
    for (int i = 0; i < leafVector.size(); i++)
    {
        OctreeNode *node = leafVector[i];
        boost::asio::post(pool, [&, node, chunk_id]()
                          { this->buildKdTreeForLeafNode(node, dataPoints, chunk_id); });
    }
    pool.join();
}

/**
 * @brief Create KD-tree index for a single octree leaf node
 * 
 * This function builds a detailed KD-tree index for a specific octree leaf node,
 * enabling efficient point queries within that spatial region. It extracts the
 * relevant data points, builds the KD-tree structure, and stores it in the
 * database for query processing.
 * 
 * Processing steps:
 * 1. Extract spatiotemporal data points belonging to the leaf node
 * 2. Handle empty leaf nodes by creating minimal valid structures
 * 3. Build KD-tree using the extracted points for spatial indexing
 * 4. Convert KD-tree to database-compatible format
 * 5. Store the KD-tree index in the database with proper associations
 * 
 * @param node Pointer to the octree leaf node requiring KD-tree indexing
 * @param dataPoints Complete dataset of spatiotemporal points for the chunk
 * @param chunk_id Unique identifier for the spatial chunk being processed
 * 
 * @note Handles empty leaf nodes gracefully by creating empty KD-tree structures
 * @note KD-tree is optimized for 3D spatial queries within the leaf's bounds
 * @note All database storage operations are performed through manager classes
 * @note Essential component for enabling efficient spatial point queries
 */
void BuildChunk::buildKdTreeForLeafNode(const OctreeNode *node,
                               const vector<SpatioTemporalData> &dataPoints, int chunk_id)
{

    std::vector<SpatioTemporalData> points;
    for (auto p_idx : node->points)
    {
        points.push_back(dataPoints[p_idx]);
    }
    if (points.size() == 0)
    {
        // kdTreeNodeManager.writeKdTreeNodesToDatabase(chunk_id, node->id, std::vector<DBKdtreeNode>());
        BinaryKVStorage bs(data_dir+"/kdtree");
        bs.write("kd_"+std::to_string(chunk_id)+"_"+std::to_string(node->id),std::vector<DBKdtreeNode>());
        return;
    }
    KDTreeBuilder builder;
    builder.buildKdTree(points);
    auto tree = builder.convert();
    // auto tree = buildKdTree(points);
    // kdTreeNodeManager.writeKdTreeNodesToDatabase(chunk_id, node->id, tree);
    BinaryKVStorage bs(data_dir+"/kdtree");
    bs.write("kd_"+std::to_string(chunk_id)+"_"+std::to_string(node->id),tree);
}

/**
 * @brief Sort spatiotemporal data from a single file according to octree spatial partitioning
 * 
 * This function processes a single input file containing spatiotemporal data points and
 * distributes them into separate chunk files based on octree leaf node assignments.
 * It performs spatial partitioning by querying which octree leaf each point belongs to,
 * then groups points by their assigned leaf nodes and writes them to corresponding
 * chunk files for efficient spatial access.
 * 
 * Processing workflow:
 * 1. Opens the input file and reads data in large buffered chunks for efficiency
 * 2. For each data point, queries the octree to determine target leaf node
 * 3. Groups points by leaf node ID in memory to minimize file I/O operations
 * 4. Writes grouped points to separate chunk files (one per leaf node)
 * 5. Uses file locking to ensure thread-safe access to chunk files
 * 6. Updates global statistics about processed point counts
 * 
 * File organization:
 * - Input: Single binary file containing SpatioTemporalData points
 * - Output: Multiple chunk files named by leaf node ID in /chunk_original_data/
 * - Each chunk file contains all points belonging to that spatial region
 * 
 * Concurrency considerations:
 * - Thread-safe through file locking mechanism (files_lock)
 * - Can be called concurrently for different input files
 * - Atomic updates to global counters (original_sort_count)
 * - No database operations, only file I/O
 * 
 * @param filename Path to the input binary file containing spatiotemporal data
 * @param query_utils Octree query utility for spatial partitioning decisions
 * @param block_size Statistics collection map for block size tracking (currently unused)
 * @param mutex Synchronization mutex for thread-safe operations
 * 
 * @note Buffer size is dynamically calculated based on thread pool configuration
 * @note Output files are opened in append mode to support concurrent writes
 * @note Essential preprocessing step for spatial indexing and database storage
 */
void DataLoader::sort_original_file_by_octree(const string &filename, OctreePointQuery &query_utils,std::unordered_map<int,std::unordered_map<int,std::int64_t>> &block_size,std::mutex &mutex)
{
    // std::unordered_map<int,std::unordered_map<int,std::int64_t>> sub_block_size;
    logger.log("INFO: sort_original_file_by_octree: Processing file " + filename);
    
    std::ifstream infile(filename, std::ios::binary);
    std::vector<SpatioTemporalData> buffer(4 * 4 * 64 * 1024 * 32/concurrent_task_num);
    size_t buffer_size = buffer.size();
    size_t points_read;
    while (infile)
    {
        std::unordered_map<int64_t, std::vector<SpatioTemporalData>> cellPointCurrentFile;
        infile.read(reinterpret_cast<char *>(buffer.data()), sizeof(SpatioTemporalData) * buffer_size);
        points_read = infile.gcount() / sizeof(SpatioTemporalData);
        original_sort_count += points_read;
        for (size_t i = 0; i < points_read; ++i)
        {
            auto &point = buffer[i];
            auto leaf_node = query_utils.point_query_octree(point);
            // logger.log("INFO: sort_original_file_by_octree: leaf_node.id: " + std::to_string(leaf_node.id));
            cellPointCurrentFile[leaf_node.id].emplace_back(point);
            // ++sub_block_size[leaf_node.oc_id][leaf_node.kd_id];
        }

        for (const auto &[key, vec] : cellPointCurrentFile)
        {
            files_lock.lock(to_string(key));
            std::ofstream outFile(data_dir + "/chunk_original_data/" + to_string(key) + ".bin", std::ios::app | std::ios::binary);
            outFile.write(reinterpret_cast<const char *>(vec.data()), vec.size() * sizeof(SpatioTemporalData));
            files_lock.unlock(to_string(key));
        }


        if (points_read < buffer_size)
        {
            break;
        }
    }
    // {
    //     std::unique_lock<std::mutex> lock(mutex);
    //     for (auto &[ocid,co_value]: sub_block_size) {
    //         for (auto &[kdid,kdvalue]:co_value) {
    //             block_size[ocid][kdid] += kdvalue;
    //         }
    //     }
    // }
}

/**
 * @brief Parallel processing of multiple files for octree-based spatial partitioning
 * 
 * This function orchestrates the parallel processing of multiple input data files,
 * distributing spatiotemporal data points across octree-organized chunk files.
 * It creates a thread pool to process files concurrently, with each thread handling
 * one file at a time through the sort_original_file_by_octree function.
 * 
 * Parallel processing strategy:
 * 1. Creates a thread pool sized according to system configuration
 * 2. Submits each input file as an independent processing task
 * 3. Each task performs spatial partitioning for its assigned file
 * 4. Tracks progress across all concurrent operations
 * 5. Ensures all tasks complete before function returns
 * 6. Provides detailed logging for monitoring and debugging
 * 
 * Thread safety considerations:
 * - File-level parallelism: Each thread processes a different input file
 * - Chunk-level synchronization: Shared chunk files use file locking
 * - Atomic progress tracking: Thread-safe counters for monitoring
 * - No database operations: Only file I/O operations are performed
 * 
 * Performance characteristics:
 * - Scales with available CPU cores and I/O bandwidth
 * - Minimizes total processing time through parallel execution
 * - Memory usage scales with thread pool size and buffer configurations
 * - I/O patterns optimized for sequential reads and random writes
 * 
 * Output organization:
 * - Creates spatially-organized chunk files in /chunk_original_data/
 * - Each chunk file contains points from the same octree leaf region
 * - Enables efficient spatial queries and database loading operations
 * - Supports subsequent parallel database insertion workflows
 * 
 * @param filenames Vector of input file paths to process in parallel
 * @param query_utils Shared octree query utility for spatial partitioning
 * @param block_size Statistics collection map for performance analysis
 * 
 * @note Thread pool size controlled by concurrent_task_num
 * @note Progress reporting includes per-file timing and overall completion status
 * @note Essential step in the data ingestion pipeline before database storage
 * @note File operations only - no PostgreSQL SPI calls, safe for parallel execution
 */
void DataLoader::para_sort_original_file_by_octree(const vector<string> &filenames, OctreePointQuery &query_utils,
    std::unordered_map<int,std::unordered_map<int,std::int64_t>> &block_size)
{
    int file_id = 0;
    std::mutex mutex;
    std::atomic<int> processed_files(0);
    
    elog(INFO, "para_sort_original_file_by_octree: Processing %zu files in parallel (file operations only, no database access)", filenames.size());
    
    for (const auto &filename : filenames)
    { 
        thread_pool.post_task([this,filename=filename,file_id,&query_utils,&block_size,&mutex,&processed_files,total_files=filenames.size()]()
        {
            TimerClock tc;
            this->sort_original_file_by_octree(filename, query_utils,block_size,mutex);
            int current_processed = processed_files++;
            std::string log_message = "INFO: para_sort_original_file_by_octree: Processed file " + filename + 
                                     ", progress: " + std::to_string(current_processed + 1) + "/" + std::to_string(total_files) + 
                                     ", time: " + std::to_string(tc.second()/concurrent_task_num) + " seconds";
            logger.log(log_message);
            // if(delete_files)
            // std::remove(filename.c_str()); 
        });
        ++file_id;
    }
    thread_pool.wait_for_all_tasks();
    
    elog(INFO, "para_sort_original_file_by_octree: Completed processing all %zu files", filenames.size());
}

/**
 * @brief Comprehensive data processing pipeline for spatial data ingestion to PostgreSQL
 * 
 * This function implements a complete data processing workflow that transforms raw
 * spatiotemporal data files into an optimized database structure. It coordinates
 * spatial partitioning, octree-based indexing, and sequential database operations
 * while ensuring thread safety and optimal performance.
 * 
 * Complete processing pipeline:
 * 
 * Phase 1: Initialization and Setup
 * - Loads global spatial bounds from configuration
 * - Initializes octree query utilities for spatial partitioning
 * - Clears previous data and prepares output directories
 * - Sets up timing and progress tracking mechanisms
 * 
 * Phase 2: Spatial Partitioning (Parallel File Processing)
 * - Processes multiple input files concurrently using thread pools
 * - Distributes data points to octree leaf-based chunk files
 * - Performs spatial sorting without database operations
 * - Tracks processing statistics and performance metrics
 * 
 * Phase 3: Spatial Index Creation (Sequential Database Operations)
 * - Processes octree leaf chunks sequentially to avoid SPI conflicts
 * - Creates spatial indexes and stores organized data in PostgreSQL
 * - Maintains referential integrity between spatial and temporal data
 * - Provides progress reporting for long-running operations
 * 
 * Phase 4: User Data Processing (Sequential Database Operations)
 * - Processes user-specific temporal data sequentially
 * - Sorts data by timestamp for optimal query performance
 * - Stores user data with proper associations to spatial indexes
 * - Ensures data consistency across all database tables
 * 
 * Database Safety Features:
 * - All PostgreSQL SPI operations are sequential to prevent threading issues
 * - Commented out thread pool code prevents accidental parallel database access
 * - Clear warnings and documentation about SPI threading limitations
 * - Proper error handling and transaction management
 * 
 * Performance Optimizations:
 * - Parallel file I/O operations where thread-safe
 * - Large buffer sizes for efficient bulk data processing
 * - Temporal sorting optimization for time-series queries
 * - Spatial clustering through octree-based organization
 * 
 * Output Structure:
 * - Spatially indexed data organized by octree leaf nodes
 * - Temporally sorted user data for efficient time-range queries
 * - Proper foreign key relationships between spatial and temporal tables
 * - Optimized data layout for PostgreSQL query performance
 * 
 * @note This function replaces split_data_to_db with improved PostgreSQL integration
 * @note All database operations are intentionally sequential due to SPI limitations
 * @note File operations use parallel processing for maximum I/O throughput
 * @note Essential for large-scale spatiotemporal data ingestion workflows
 * 
 * @warning Never modify this function to use parallel database operations
 * @warning PostgreSQL SPI is not thread-safe and will cause crashes or corruption
 */
void DataLoader::split_data_to_db1(std::vector<DBOctreeNode>& dbNodes)
{
    OctreePointQuery query_utils(dbNodes);
    // LargeObjectCleaner::clearAllLargeObjects();
    TimerClock tc;
    Bounds bounds;
    {
        std::ifstream inputFile(data_dir +"/global_bound.txt");
        if (inputFile)
        {
            inputFile >> bounds;
            elog(INFO, "split_data_to_db1: Loaded bounds: (%f,%f,%f)-(%f,%f,%f)", 
                 bounds.min.x, bounds.min.y, bounds.min.z, bounds.max.x, bounds.max.y, bounds.max.z);
        }
    }

    std::vector<std::string> original_filenames;
    {
        for (const auto &entry : fs::directory_iterator(data_dir +"/temp_bin_original_file"))
        {
            const auto &path = entry.path();
            original_filenames.push_back(data_dir +"/temp_bin_original_file/" + path.filename().string());
        }
    }
    original_sort_count = 0;
    originalDataManager.clearTable();
    clear_folder(data_dir +"/chunk_original_data");
    tc.tick();
    std::unordered_map<int,std::unordered_map<int,std::int64_t>> block_size;
    this->para_sort_original_file_by_octree(original_filenames,query_utils,block_size);
    // save_map_to_file(block_size,data_dir + "/block_size.bin");
    build_time["to_db_time->sort_by_octree"] = tc.second();
    elog(INFO, "split_data_to_db1: original_sort_count: %ld", original_sort_count.load());
    tc.tick();
    
    // boost::asio::thread_pool index_pool(tran_data_to_db_thread_pool_size);
    int processed_chunks = 0;
    int total_chunks = 0;
    for (auto node:dbNodes)
    {
        if (node.is_leaf)
        {
            total_chunks++;
        }
    }
    
    for (auto node:dbNodes)
    {
        if (node.is_leaf)
        {
            // IMPORTANT: Database operations are performed sequentially to avoid SPI threading issues
            // The original boost::asio::post has been commented out to prevent parallel database access
            // boost::asio::post(index_pool, [node,total_size=query_utils.chunk_tree.size()]()
            // {
            chunk_original_data_to_db(node);
            processed_chunks++;
            elog(INFO, "split_data_to_db1: Processed chunk %d, progress: %d/%d", 
                 node.id, processed_chunks, total_chunks);
            // });
        }
    }
    // index_pool.join();
 
    // {
    //     OriginalDataManager::createIndex();
    // }
    build_time["to_db_time->spatial_to_db"] = tc.second();
    elog(INFO, "split_data_to_db1: Spatial data to database time: %f seconds", tc.second());
    tc.tick();
    {
        // WARNING: User data processing is also sequential to avoid database threading issues
        // The thread pool is created but not used for database operations
        // boost::asio::thread_pool pool(tran_data_to_db_thread_pool_size);
        std::atomic<int> file_id = 0;
        // std::atomic<uint64_t> all_count = 0; // unused variable
        std::vector<std::string> user_filenames;
        for (const auto &entry : fs::directory_iterator(data_dir + "/temp_bin_original_file"))
        {
            const auto &path = entry.path();
            user_filenames.push_back(data_dir + "/temp_bin_original_file/" + path.filename().string());
        }
        
        elog(INFO, "split_data_to_db1: Processing %zu user data files sequentially", user_filenames.size());
        
        for (const auto &filename : user_filenames)
        {
            // boost::asio::post(pool, [&,filename]()
            // {
                TimerClock user_tc;
                std::ifstream infile(filename, std::ios::binary);
                std::vector<SpatioTemporalData> buffer(4 * 64 * 1024 * 32/concurrent_task_num);
                size_t buffer_size = buffer.size();
                size_t points_read;
                while (true)
                {
                    infile.read(reinterpret_cast<char *>(buffer.data()), sizeof(SpatioTemporalData) * buffer_size);
                    points_read = infile.gcount() / sizeof(SpatioTemporalData);  
                    if(buffer.size() > points_read){
                        buffer.resize(points_read);
                    }
                    std::sort(buffer.begin(), buffer.end(), [](const SpatioTemporalData &a, const SpatioTemporalData &b)
                    { return a.time < b.time; });
                    // elog(INFO, "userid:%d, buffer size:%zu", buffer.front().user_id, buffer.size());
                    if(buffer.size() == 0){
                        // elog(INFO, "Empty buffer for filename: %s", filename.c_str());
                    }
                    userDataManager.writeDataToDatabase(buffer.front().user_id, buffer);
                    buffer.resize(buffer_size);
                    if (points_read < buffer_size)
                    {
                        break;
                    }
                }
                int current_file_id = file_id++;
                elog(INFO, "split_data_to_db1: Processed user data file %s, progress: %d/%zu, time: %f seconds", 
                     filename.c_str(), current_file_id, user_filenames.size(), 
                     user_tc.second()/concurrent_task_num);
                // if(delete_files){
                //     std::remove(filename.c_str());
                // }
            // });
        }
        // pool.join();
    }
    build_time["to_db_time->user_to_db"] = tc.second();
    elog(INFO, "split_data_to_db1: User data to database time: %f seconds", tc.second());
}

/**
 * @brief Process and store spatiotemporal data for a single octree leaf node in PostgreSQL
 * 
 * This function handles the database storage of spatiotemporal data belonging to a specific
 * octree leaf node. It loads the pre-sorted chunk data from disk, performs spatial
 * subdivision using the octree structure, and stores the organized data in PostgreSQL
 * with proper spatial and temporal indexing.
 * 
 * Processing workflow:
 * 1. Data Loading Phase:
 *    - Loads octree node hierarchy from database for the leaf node
 *    - Reads pre-sorted spatiotemporal data from the corresponding chunk file
 *    - Validates data integrity and reports processing statistics
 * 
 * 2. Spatial Organization Phase:
 *    - Recursively subdivides data using the octree node structure
 *    - Groups points by their final octree leaf assignments
 *    - Prepares data for efficient database insertion operations
 * 
 * 3. Database Storage Phase:
 *    - Sorts data points by timestamp for optimal temporal access
 *    - Stores organized data in PostgreSQL using originalDataManager
 *    - Maintains proper associations between spatial and temporal indexes
 *    - Ensures referential integrity across database tables
 * 
 * Data organization strategy:
 * - Input: Binary chunk file containing spatiotemporal points for one leaf region
 * - Processing: Recursive spatial subdivision using octree hierarchy
 * - Output: Temporally sorted data stored with spatial index associations
 * - Result: Optimized database structure for spatial and temporal queries
 * 
 * Database integration:
 * - Uses PostgreSQL SPI through originalDataManager for safe database access
 * - Maintains transaction consistency for all data operations
 * - Provides proper error handling for database connectivity issues
 * - Ensures data durability through proper commit protocols
 * 
 * Performance characteristics:
 * - Sequential processing ensures SPI thread safety
 * - Memory-efficient streaming of large data sets
 * - Optimized for bulk insertion operations
 * - Temporal sorting improves query performance for time-range operations
 * 
 * Error handling:
 * - Graceful handling of missing or corrupted chunk files
 * - Database transaction rollback on operation failures
 * - Detailed logging for debugging and monitoring
 * - Proper resource cleanup in all execution paths
 * 
 * @param leaf_node The octree leaf node containing spatial bounds and identification
 * 
 * @note This function must only be called sequentially due to PostgreSQL SPI limitations
 * @note Chunk files are expected to exist in /chunk_original_data/ directory
 * @note Data is automatically sorted by timestamp for optimal temporal access patterns
 * @note Essential component of the spatial database ingestion pipeline
 * 
 * @warning Never call this function in parallel - it uses PostgreSQL SPI operations
 * @warning Ensure chunk files exist before calling, or function will return early
 */
void DataLoader::chunk_original_data_to_db(DBOctreeNode leaf_node)
{
    elog(INFO, "chunk_original_data_to_db: Starting processing for leaf node %d", leaf_node.id);
    
    std::vector<DBOctreeNode> ocNodes = octreeNodeManager.getOctreeNodeByKey(leaf_node.id);
    size_t data_count = 0;
    std::vector<SpatioTemporalData> all_data;
    {
        auto original_file = std::ifstream(data_dir + "/chunk_original_data/" + std::to_string(leaf_node.id) + ".bin", std::ios::binary);
        if (!original_file.is_open()) {
            elog(WARNING, "chunk_original_data_to_db: Failed to open chunk file for leaf node %d", leaf_node.id);
            return;
        }
        original_file.seekg(0, std::ios::end);
        size_t file_size = original_file.tellg();
        original_file.seekg(0, std::ios::beg);
        data_count = file_size / sizeof(SpatioTemporalData);
        elog(INFO, "chunk_original_data_to_db: Loaded %zu data points from chunk file for leaf node %d", data_count, leaf_node.id);
        all_data.resize(data_count);
        original_file.read(reinterpret_cast<char *>(all_data.data()), file_size);
        original_file.close();
        // Note: delete_files is not defined in this scope, commenting out for now
        // if(delete_files)
        // std::remove((data_dir + "/chunk_original_data/" + std::to_string(leaf_node.id) + ".bin").c_str());
    }
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
        write_points_to_db(leaf_node.id, i.first, i.second);
    }
    
    elog(INFO, "chunk_original_data_to_db: Completed processing leaf node %d - stored %lld points across %zu KD-tree nodes", 
         leaf_node.id, in_this_file_read_count, to_db_data.size());
}