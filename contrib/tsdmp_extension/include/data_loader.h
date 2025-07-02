#ifndef TSDMP_DATA_LOADER_H
#define TSDMP_DATA_LOADER_H

#include <string>
#include <vector>

class DataLoader{
    public:
    DataLoader(const std::string& directory, int max_file_num, float sample_ratio);
    ~DataLoader();
    std::vector<std::string> load_data();

    private:
    std::vector<std::string> load_data_source_files();
    
    // Member variables
    std::string directory;
    int max_file_num;
    float sample_ratio;
};  

#endif // TSDMP_DATA_LOADER_H