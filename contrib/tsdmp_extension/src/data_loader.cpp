// C++标准库头文件 - 先包含以避免宏冲突
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <random>
#include <fstream>
#include <thread>
#include <nlohmann/json.hpp>

// 只包含必要的PostgreSQL头文件
extern "C" {
#include "postgres.h"
#include "utils/elog.h"
}

// 项目头文件
#include "../include/data_loader.h"
#include "../include/parameter.h"
#include "../include/mesh_connection_manager.h"
#include "../include/octree_node_manager.h"
#include "../include/kdtree_node_manager.h"
#include "../include/userdata_manager.h"
#include "../include/utils.h"
#include "file_sorter.h"

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

vector<string> DataLoader::load_data()
{
    this->load_data_source_files();
    json users_json;
    for (size_t i = 0; i < filenames.size(); i++)
    {
        users_json[filenames[i]] = i;
    }
    ofstream out_file(data_dir + "/files_user.json");
    out_file << users_json.dump(10); // 格式化输出
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
            std::cout << "Loaded bounds: " << global_bound << std::endl;
        }
    }
    // uint64_t sampleCellNums = (int64_t) 1 << (int64_t) chunk_max_level * 3, sampleDimension = (int64_t) 1 << (int64_t) chunk_max_level;
    // std::vector<uint64_t> originalCells;
    // Para_preCount(const vector<string> &filenames, sample_max_level-file_block_level, global_bound, std::vector<uint64_t> &originalCells);
    tc.tick();
    this->para_sort_sample_file(global_bound);//给所有的采样数据和，按照z-order 排序
    std::cout <<"sample_sorted_file_count:"<<this->sample_sorted_file_count << std::endl;
    this->build_time["doChunking_time->sorting_time"] = tc.second();
    tc.tick();
    this->merge_sample_data();
    this->build_time["doChunking_time->merging_time"] = tc.second();
    tc.tick();
    std::cout <<"build chunk"<< std::endl;
//     OctreeNode *root = building_octree_bottom_up_top_down(global_bound);// build chunk
//     std::vector<DBOctreeNode> dbNodes = convertOctreeToDB(root);
//     OctreeNodeManager::writeOctreeNodesToDatabase(-1, dbNodes);
//     if(delete_files)
//     std::remove((data_dir + "/sample_data/all_sampled_data.bin").c_str());
//     this->build_time["doChunking_time->chunk_constrution_time"] = tc.second();
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
}

void DataLoader::para_bound_and_sample(){
    Bounds global_bound;
    std::thread clear1(clear_folder,data_dir + "/temp_bin_original_file");
    std::thread clear2(clear_folder,data_dir + "/temp_bin_sample_file");
    clear1.join();
    clear2.join();
    boost::asio::thread_pool pool(max_concurrent_tasks_for_read_bound_task);
    std::cout << "The number of files :" << this->filenames.size() << endl;
    std::vector<Bounds> sub_bounds(this->filenames.size());
    json loaded_json;
    {
        std::ifstream in_file(data_dir + "/files_user.json");
        in_file >> loaded_json;
        in_file.close();
    }
    std::atomic<std::uint32_t> finished_file_counting = 0;
    for (std::uint32_t i = 0; i < this->filenames.size(); ++i)
    {
        boost::asio::post(pool, [this, &loaded_json, &finished_file_counting, i, &sub_bounds]()
        {
            TimerClock tc;
            auto file_name = this->filenames[i];
            sub_bounds[i] = this->calculate_bound_and_sampleing(file_name,i,loaded_json[file_name].get<int16_t>()); 
            std::cout<<"bound & sample:"<<file_name<<" progress:"<<finished_file_counting++<<":"<<this->filenames.size()<<" time:"<<tc.second()/max_concurrent_tasks_for_read_bound_task<<"s"<<std::endl; 
            // if(delete_original_files){
            //     std::remove(file_name.c_str());
            // }
        });
    }
    pool.join();
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
                std::cout << "read bound:" << filename << ":" << count / int(1e7) << "x1e7 用时" << tc.second() << "秒" << std::endl;
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
                std::cout << "read bound:" << filename << ":" << count / int(1e7) << "x1e7 用时" << tc.second() << "秒" << std::endl;
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
        while (getline(file, line))
        {
            if (count++ >= max_point_limit)
            {
                break;
            }

            if (count % int(1e7) == 0)
            {
                std::cout << "read bound:" << filename << ":" << count / int(1e7) << "x1e7 用时" << tc.second() << "秒" << std::endl;
                tc.tick();
            }
            stringstream ss(line);
            string prefix;
            ss >> prefix;
            if (prefix == "v")
            {
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
                int32_t number;
                std::vector<int32_t> connection;
                while (ss >> number)
                {
                    connection.push_back(number);
                }
                connections.emplace_back(std::move(connection));
            }
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
            int64_t index = this->indexOfPoint(point.x, point.y, point.z, spatial_bound, chunk_max_level);
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

        if (bytes_read < buffer.size()) break;  // 文件已读取完毕
    }
}

uint64_t DataLoader::indexOfPoint(float x, float y, float z, SpatialBounds bound, int level)
{
    uint64_t x_ = 0, y_ = 0, z_ = 0;
    auto center = bound.getCenter();
    for (auto i=level;i-- > 0;)
    {
        x_ <<= 1;
        y_ <<= 1;
        z_ <<= 1;
        if (x < center.x)
        {
            bound.max.x = center.x;
        }
        else
        {
            bound.min.x = center.x;
            x_ |= 1;
        }
        if (y < center.y)
        {
            bound.max.y = center.y;
        }
        else
        {
            bound.min.y = center.y;
            y_ |= 1;
        }
        if (z < center.z)
        {
            bound.max.z = center.z;
        }
        else
        {
            bound.min.z = center.z;
            z_ |= 1;
        }
        center = bound.getCenter();
    }
    uint64_t index = interleaveBits(x_, y_, z_,level);
    return index;
}

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
                  return a.first < b.first; // 按照键升序排序
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
    cout << file_name << " " << tmpcount << endl;
    fileWrite.close();
}

void DataLoader::para_sort_sample_file(const Bounds &bounds)
{
    clear_folder(data_dir + "/sample_data");
    std::vector<std::string> filenames;
    for (const auto &entry : fs::directory_iterator(data_dir + "/temp_bin_sample_file"))
    {
        const auto &path = entry.path();
        filenames.push_back(data_dir + "/temp_bin_sample_file/" + path.filename().string());
    }
    std::uint64_t sampleCellNums = (std::int64_t)1 << (std::int64_t)chunk_max_level * 3;
    auto sampleCells = std::vector<uint32_t>(sampleCellNums, 0);
    boost::asio::thread_pool pool(max_concurrent_tasks_for_count_task);
    int file_id = 0;
    for (const auto &filename : filenames)
    {
        boost::asio::post(pool, [this, file_id, filename, &bounds, &sampleCells]()
        {
            TimerClock tc;
            this->sort_sample_file(file_id, filename, bounds, sampleCells); 
            std::cout<<"sort sample:"<<filename<<" time:"<<tc.second()/max_concurrent_tasks_for_count_task<<"s"<<std::endl; 
            // if(delete_files)
            // std::remove(filename.c_str());
        });
        ++file_id;
    }
    pool.join();
    std::ofstream sampleWrite(data_dir + "/sample_data/sampleCellNums.bin", std::ios::binary | std::ios::app);
    sampleWrite.write(reinterpret_cast<const char *>(sampleCells.data()), sampleCellNums * sizeof(std::uint32_t));
    sampleWrite.close();
    std::cout <<"all file sorted"<< std::endl;
}

void DataLoader::merge_sample_data(){
    std::uint64_t cur_iter_id = 0;
    while (1)
    {
        auto cur_file_num = Para_domerge(cur_iter_id, true);
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
            std::cout <<"fileSize:"<<fileSize<< std::endl;
            throw StringException("数据文件大小异常");
        }
        std::size_t numElements = fileSize / sizeof(uint32_t);
        if (numElements != sampleCellNums)
        {
            throw StringException("数据个数异常");
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
            std::cout << "sample validation failed:" << validation_sample_cell_count << ":" << cell_index << std::endl;
        }
        for (int i = 0; i < cur_index_number; i++)
        {
            SpatioTemporalData point;
            cur_file.read(reinterpret_cast<char *>(&point), sizeof(point));
            new_file.write(reinterpret_cast<char *>(&point), sizeof(point));
        }
    }
    std::cout << "merged sample data size::" << cell_index << endl;
    std::remove(std::string(data_dir +"/sample_data/" + to_string(cur_iter_id) + "_0.bin").c_str());
    
}

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
            cout << all_number << " " << "cur_file_id:" << cur_file_id << " " << "cur_iter:" << cur_iter << " " << "cur_file_num:" << cur_file_num << " " << next_file_name << " " << cur_file_name << " " << new_file_name << endl;
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
            cout << all_number << " " << "cur_file_id:" << cur_file_id << " " << "cur_iter:" << cur_iter << " " << "cur_file_num:" << cur_file_num << " " << next_file_name << " " << cur_file_name << " " << new_file_name << endl;
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
        cout << all_number << " " << "cur_file_id:" << cur_file_id << " " << "cur_iter:" << cur_iter << " " << "cur_file_num:" << cur_file_num << " " << next_file_name << " " << cur_file_name << " " << new_file_name << endl;
    return all_number;
}

uint64_t Para_domerge(uint64_t cur_iter_id, bool sample_or_original)
{
    std::cout << "建立合并线程池:" << cur_iter_id << std::endl;
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
    ThreadPoolWrapper thread_pool(max_concurrent_tasks_for_count_task);
    std::atomic<uint64_t> merged_count(0);
    for (uint64_t i = 0; i < sample_files.size(); i += 2)
    {
        thread_pool.post_task([&, i,cur_iter_id,file_set_size=sample_files.size()]()
                              { merged_count += domerge(i, cur_iter_id, file_set_size, prefix); });
    }
    thread_pool.wait_for_all_tasks();
    // sample_files = findFilesWithPrefix("./" + prefix, to_string(cur_iter_id) + "_");

    {
        auto tasks = split<std::int64_t>(0,sample_files.size(),max_concurrent_tasks_for_count_task * 2);
        ThreadPoolWrapper deleting_thread_pool(max_concurrent_tasks_for_count_task * 2);
        for (auto task : tasks)
        {
            deleting_thread_pool.post_task([prefix,task,&sample_files]()
            {
                for(std::int64_t i = task.first;i<task.second;++i){
                    std::string file_path = data_dir +"/" + prefix + "/" + sample_files[i];
                    std::remove(file_path.c_str()); 
                }
            });
        }
        deleting_thread_pool.wait_for_all_tasks();
    }
    std::cout << "merged it count :"<<cur_iter_id<<" - "<< merged_count << std::endl;
    return sample_files.size();
}