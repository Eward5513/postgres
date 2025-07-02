#include "file_sorter.h"

#include <algorithm>
#include <regex>

void FileSorter::sortFiles(std::vector<std::string>& filenames) {
    std::vector<FileInfo> fileInfos;
    for (const auto& filename : filenames) {
        fileInfos.push_back(extractFileInfo(filename));
    }

    std::sort(fileInfos.begin(), fileInfos.end(), [](const FileInfo& a, const FileInfo& b) {
        if (a.fileType == "ply" && b.fileType != "ply") return true;
        if (a.fileType != "ply" && b.fileType == "ply") return false;
        if (a.fileType == "csv" && b.fileType != "csv") return true;
        if (a.fileType != "csv" && b.fileType == "csv") return false;
        if (a.fileType == "obj" && b.fileType != "obj") return true;
        if (a.fileType != "obj" && b.fileType == "obj") return false;
        return a.fileNum < b.fileNum;
    });

    filenames.clear();
    for (const auto& fileInfo : fileInfos) {
        filenames.push_back(fileInfo.filename);
    }
}

FileSorter::FileInfo FileSorter::extractFileInfo(const std::string& filename) {
    FileInfo fileInfo;
    fileInfo.fileType = extractExtension(filename);
    fileInfo.fileNum = extractNumber(filename);
    fileInfo.filename = filename;
    return fileInfo;
}

std::string FileSorter::extractExtension(const std::string& filename) {
    return filename.substr(filename.find_last_of('.') + 1);
}

int FileSorter::extractNumber(const std::string& filename) {
    std::regex regex("(\\d+)(?=\\.)");
    std::smatch match;
    if (std::regex_search(filename, match, regex)) {
        return std::stoi(match.str(0));
    }
    return 0;
} 