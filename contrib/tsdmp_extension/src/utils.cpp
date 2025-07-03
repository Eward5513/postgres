#include <iostream>
#include <string>
#include <filesystem>
#include <boost/asio.hpp>
#include <boost/thread.hpp>

ContinuousRandomGenerator::ContinuousRandomGenerator(float min, float max)
    : gen(rd()), dist(min, max) {}

float ContinuousRandomGenerator::generate() {
    return dist(gen);
}

bool clear_folder(std::string folderPath) {
    std::cout << "Clear folder: " << folderPath << std::endl;

    namespace fs = std::filesystem;
    if (!fs::exists(folderPath)) {
        throw std::runtime_error("Error: Folder does not exist: " + folderPath);
        return false;
    }

    boost::asio::thread_pool pool;

    for (const auto& entry : fs::directory_iterator(folderPath)) {
        boost::asio::post(pool, [entry] {
            try {
                fs::remove_all(entry.path());
            } catch (const std::exception& e) {
                std::cerr << "Error removing: " << entry.path() << " - " << e.what() << std::endl;
            }
        });
    }
    pool.join();

    std::cout << "Cleared folder" << std::endl;
    return true;
}

std::vector<std::string> findFilesWithPrefix(const std::string &folderPath, const std::string &prefix)
{
    std::vector<std::string> matchedFiles;
    for (const auto &entry : fs::directory_iterator(folderPath))
    {
        if (entry.is_regular_file())
        {
            const std::string filename = entry.path().filename().string();
            if (filename.rfind(prefix, 0) == 0)
            {
                matchedFiles.push_back(filename);
            }
        }
    }
    return matchedFiles;
}

uint64_t interleaveBits(uint64_t x, uint64_t y, uint64_t z, uint64_t level) {
    // std::cout <<"level:"<<level<< std::endl;
    uint64_t result = 0;
    for (uint64_t i = 0; i < level; i++) {
        result |= ((x & 1) << (3 * i)) |
                 ((y & 1) << (3 * i + 1)) |
                 ((z & 1) << (3 * i + 2));
        x >>= 1;
        y >>= 1;
        z >>= 1;
    }
    return result;
}