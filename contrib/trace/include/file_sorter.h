#ifndef TRACE_FILE_SORTER_H
#define TRACE_FILE_SORTER_H

#include <string>
#include <vector>

class FileSorter {
public:
    static void sortFiles(std::vector<std::string>& filenames);

private:
    struct FileInfo {
        std::string filename;
        std::string fileType;
        int fileNum;
    };

    static FileInfo extractFileInfo(const std::string& filename);
    static std::string extractExtension(const std::string& filename);
    static int extractNumber(const std::string& filename);
};

#endif // TRACE_FILE_SORTER_H 