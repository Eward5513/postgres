// 使用统一的安全头文件处理PostgreSQL和libintl.h冲突
#include "../include/safe_header.h"

// 项目头文件
#include "../include/data_loader.h"
#include "../include/parameter.h"
#include "../include/mesh_connection_manager.h"
#include "../include/octree_node_manager.h"
#include "../include/kdtree_node_manager.h"
#include "../include/userdata_manager.h"
#include "file_sorter.h"

// C++标准库头文件
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <random>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>

using namespace std;
using json = nlohmann::json;

namespace fs = filesystem;

// Constructor
DataLoader::DataLoader(const string& directory, int max_file_num, float sample_ratio)
    : directory(directory), max_file_num(max_file_num), sample_ratio(sample_ratio)
{
}

// Destructor
DataLoader::~DataLoader()
{
}

vector<string> DataLoader::load_data_source_files()
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
    return filenames;
}

vector<string> DataLoader::load_data()
{
    vector<string> filenames = this->load_data_source_files();
    json users_json;
    for (size_t i = 0; i < filenames.size(); i++)
    {
        users_json[filenames[i]] = i;
    }
    ofstream out_file(data_dir + "/files_user.json");
    out_file << users_json.dump(10); // 格式化输出
    out_file.close();

    create_index();
    elog(INFO, "loaded %zu files from directory '%s'", filenames.size(), this->directory.c_str());

    return filenames;  
}

int create_index()
{
    elog(INFO, "clear index table");
    std::thread t2(&OctreeNodeManager::clearTable);
    std::thread t3(&KdTreeNodeManager::clearTable);
    std::thread t4(&UserDataManager::clearTable);
    std::thread t5(&MeshConnectionManager::clearTable);
    t2.join();
    t3.join();
    t4.join();
    t5.join();
    elog(INFO, "cleared index table");
    //building();
    return 0;
}

// void building()
// {
//     TimerClock tc;
//     para_bound_and_sample();
//     build_time["doChunking_time->sampling_time"] = tc.second();
//     Bounds global_bound;
//     {
//         std::ifstream inputFile(data_dir +"/global_bound.txt");
//         if (inputFile)
//         {
//             inputFile >> global_bound;
//             std::cout << "Loaded bounds: " << global_bound << std::endl;
//         }
//     }
//     // uint64_t sampleCellNums = (int64_t) 1 << (int64_t) chunk_max_level * 3, sampleDimension = (int64_t) 1 << (int64_t) chunk_max_level;
//     // std::vector<uint64_t> originalCells;
//     // Para_preCount(const vector<string> &filenames, sample_max_level-file_block_level, global_bound, std::vector<uint64_t> &originalCells);
//     tc.tick();
//     para_sort_sample_file(global_bound);//给所有的采样数据和，按照z-order 排序
//     std::cout <<"sample_sorted_file_count:"<<sample_sorted_file_count << std::endl;
//     build_time["doChunking_time->sorting_time"] = tc.second();
//     tc.tick();
//     merge_sample_data();
//     build_time["doChunking_time->merging_time"] = tc.second();
//     tc.tick();
//     std::cout <<"build chunk"<< std::endl;
//     OctreeNode *root = building_octree_bottom_up_top_down(global_bound);// build chunk
//     std::vector<DBOctreeNode> dbNodes = convertOctreeToDB(root);
//     OctreeNodeManager::writeOctreeNodesToDatabase(-1, dbNodes);
//     if(delete_files)
//     std::remove((data_dir + "/sample_data/all_sampled_data.bin").c_str());
//     build_time["doChunking_time->chunk_constrution_time"] = tc.second();
//     if (0)
//     { // validation
//         dbNodes = OctreeNodeManager::loadOctreeNodesFromDatabase(-1);
//         std::cout << "dbNodes:" << dbNodes.size() << std::endl;
//         if (validateConversion(root, dbNodes, 0))
//         {
//             std::cout << "Validation passed: The structures are consistent.\n";
//         }
//         else
//         {
//             std::cout << "Validation failed: The structures are not consistent.\n";
//         }
//     }
//     std::cout << "create chunk time: " << tc.second() << std::endl;
//     delete root;
// }

// void para_bound_and_sample(){
//     Bounds global_bound;
//     std::vector<std::string> filenames = load_data_source_files();
//     std::thread clear1(clear_folder,data_dir + "/temp_bin_original_file");
//     std::thread clear2(clear_folder,data_dir + "/temp_bin_sample_file");
//     clear1.join();
//     clear2.join();
//     boost::asio::thread_pool pool(max_concurrent_tasks_for_read_bound_task);
//     std::cout << "The number of files :" << filenames.size() << endl;
//     std::vector<Bounds> sub_bounds(filenames.size());
//     json loaded_json;
//     {
//         std::ifstream in_file(data_dir + "/files_user.json");
//         in_file >> loaded_json;
//         in_file.close();
//     }
//     std::atomic<std::uint32_t> finished_file_counting = 0;
//     for (std::uint32_t i = 0; i < filenames.size(); ++i)
//     {
//         boost::asio::post(pool, [&loaded_json, &finished_file_counting, &filenames, i, &sub_bounds]()
//         {
//             TimerClock tc;
//             auto file_name = filenames[i];
//             sub_bounds[i] = calculate_bound_and_sampleing(file_name,i,loaded_json[file_name].get<int16_t>()); 
//             std::cout<<"bound & sample:"<<file_name<<" progress:"<<finished_file_counting++<<":"<<filenames.size()<<" time:"<<tc.second()/max_concurrent_tasks_for_read_bound_task<<"s"<<std::endl; 
//             if(delete_original_files){
//                 std::remove(file_name.c_str());
//             }
//         });
//     }
//     pool.join();
//     std::cout << "data_size:" << bounded_data_size << " file count:" << finished_file_counting << std::endl;
//     bool firstFlag = true;
//     global_bound = sub_bounds.front();
//     for (auto &bound_pair : sub_bounds)
//     {
//         global_bound = global_bound | bound_pair;
//     }
//     std::ofstream outputFile(data_dir +"/global_bound.txt");
//     if (outputFile)
//     {
//         outputFile << global_bound;
//     }
// }

// void para_sort_sample_file(const Bounds &bounds)
// {
//     clear_folder(data_dir + "/sample_data");
//     std::vector<std::string> filenames;
//     for (const auto &entry : fs::directory_iterator(data_dir + "/temp_bin_sample_file"))
//     {
//         const auto &path = entry.path();
//         filenames.push_back(data_dir + "/temp_bin_sample_file/" + path.filename().string());
//     }
//     std::uint64_t sampleCellNums = (std::int64_t)1 << (std::int64_t)chunk_max_level * 3;
//     auto sampleCells = std::vector<uint32_t>(sampleCellNums, 0);
//     boost::asio::thread_pool pool(max_concurrent_tasks_for_count_task);
//     int file_id = 0;
//     for (const auto &filename : filenames)
//     {
//         boost::asio::post(pool, [=, &bounds, &sampleCells]()
//         {
//             TimerClock tc;
//             sort_sample_file(file_id, filename, bounds, sampleCells); 
//             std::cout<<"sort sample:"<<filename<<" time:"<<tc.second()/max_concurrent_tasks_for_count_task<<"s"<<std::endl; 
//             if(delete_files)
//             std::remove(filename.c_str());
//         });
//         ++file_id;
//     }
//     pool.join();
//     std::ofstream sampleWrite(data_dir + "/sample_data/sampleCellNums.bin", std::ios::binary | std::ios::app);
//     sampleWrite.write(reinterpret_cast<const char *>(sampleCells.data()), sampleCellNums * sizeof(std::uint32_t));
//     sampleWrite.close();
//     std::cout <<"all file sorted"<< std::endl;
// }

// void merge_sample_data(){
//     std::uint64_t cur_iter_id = 0;
//     while (1)
//     {
//         auto cur_file_num = Para_domerge(cur_iter_id, true);
//         cur_file_num = (cur_file_num + 1) >> 1;
//         cur_iter_id++;
//         if (cur_file_num <= 1)
//         {
//             break;
//         }
//     }
//     std::vector<std::uint32_t> sampleCells;
//     {   
//         uint64_t sampleCellNums = (int64_t)1 << (int64_t)chunk_max_level * 3;
//         std::ifstream sampleFile(data_dir +"/sample_data/sampleCellNums.bin", std::ios::binary);
//         sampleFile.seekg(0, std::ios::end);
//         std::streamsize fileSize = sampleFile.tellg();
//         sampleFile.seekg(0, std::ios::beg);
//         if (fileSize % sizeof(uint32_t) != 0)
//         {
//             std::cout <<"fileSize:"<<fileSize<< std::endl;
//             throw StringException("数据文件大小异常");
//         }
//         std::size_t numElements = fileSize / sizeof(uint32_t);
//         if (numElements != sampleCellNums)
//         {
//             throw StringException("数据个数异常");
//         }
//         sampleCells.resize(numElements);
//         sampleFile.read(reinterpret_cast<char *>(sampleCells.data()), fileSize);
//         sampleFile.close();
//     }
//     std::ofstream new_file(data_dir +"/sample_data/all_sampled_data.bin", std::ios::binary | std::ios::app);
//     std::ifstream cur_file(data_dir +"/sample_data/" + to_string(cur_iter_id) + "_0.bin", std::ios::binary);
//     uint64_t cell_index = 0;
//     uint64_t cur_index_id;
//     uint64_t cur_index_number;

//     uint64_t validation_sample_cell_id = 0;
//     uint64_t validation_sample_cell_count = 0;
//     while (cur_file.peek() != EOF && !cur_file.eof())
//     {
//         cur_file.read(reinterpret_cast<char *>(&cur_index_id), sizeof(cur_index_id));
//         cur_file.read(reinterpret_cast<char *>(&cur_index_number), sizeof(cur_index_number));
//         cell_index += cur_index_number;
//         for (; validation_sample_cell_id <= cur_index_id; ++validation_sample_cell_id)
//         {
//             validation_sample_cell_count += sampleCells[validation_sample_cell_id];
//         }
//         if (cell_index != validation_sample_cell_count)
//         {
//             std::cout << "sample validation failed:" << validation_sample_cell_count << ":" << cell_index << std::endl;
//         }
//         for (int i = 0; i < cur_index_number; i++)
//         {
//             SpatioTemporalData point;
//             cur_file.read(reinterpret_cast<char *>(&point), sizeof(point));
//             new_file.write(reinterpret_cast<char *>(&point), sizeof(point));
//         }
//     }
//     std::cout << "merged sample data size::" << cell_index << endl;
//     std::remove(std::string(data_dir +"/sample_data/" + to_string(cur_iter_id) + "_0.bin").c_str());
    
// }

// OctreeNode *building_octree_bottom_up_top_down(Bounds bounds)
// {
//     BuildChunk build_util(max_concurrent_task_across_chunk);
//     build_util.load_sample_size();
//     std::int64_t count_sample = 0;
//     for (auto &i : build_util.sample_cell_num_in_diff_level.back())
//     {
//         count_sample += i;
//     }
//     std::cout << "prepare:" << count_sample << std::endl;
//     std::cout << "build_util.sample_cell_num_in_diff_level:" << build_util.sample_cell_num_in_diff_level.size() << std::endl;
//     std::cout << "build_util.cell_num_in_diff_level:" << build_util.cell_num_in_diff_level.size() << std::endl;

//     clear_folder(data_dir + "/chunk_data");
//     auto root = build_util.build_chunk_node_sample(bounds, 0, 0, 0, 0);//build chunk
//     build_util.do_indexing_thread_pool.wait_for_all_tasks();
//     std::cout << "chunk 节点个数:" << build_util.node_count << std::endl;
//     std::cout << "split sample size:" << build_util.split_sample_count << std::endl;
//     std::cout << "sample file point position:" << build_util.sample_file.tellg() / sizeof(SpatioTemporalData) << std::endl;
//     return root;
// }