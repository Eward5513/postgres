#include "create_index.h"

#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>
#include <random>

// PostgreSQL headers
extern "C" {
#include "postgres.h"
#include "utils/elog.h"
}

#include "file_sorter.h"

namespace fs = std::filesystem;

std::vector<std::string> load_data_source_files(const std::string& directory, int max_file_num)
{
    // int mesh_count = 0;
    // int pointcloud_count = 0;
    // int trajectory_count = 0;
    std::vector<std::string> filenames;
    for (const auto &entry : fs::directory_iterator(directory))
    {
        const auto &path = entry.path();
        auto filename = path.filename().string();
        filenames.push_back(directory + "/" + path.filename().string());
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
    std::sort(filenames.begin(),filenames.end());
    std::mt19937 g(42);
    std::shuffle(filenames.begin(), filenames.end(), g);
    elog(INFO, "sorting files");
    FileSorter::sortFiles(filenames);
    elog(INFO, "sorted files");
    if(filenames.size() > max_file_num){
        filenames.resize(max_file_num);
    }
    return filenames;
}