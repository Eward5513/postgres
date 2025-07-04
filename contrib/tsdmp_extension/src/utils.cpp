#include "../include/utils.h"
#include <iostream>
#include <string>
#include <filesystem>
#include <boost/asio.hpp>
#include <boost/thread.hpp>

namespace fs = std::filesystem;



ContinuousRandomGenerator::ContinuousRandomGenerator(float min, float max)
    : gen(rd()), dist(min, max) {}

float ContinuousRandomGenerator::generate() {
    return dist(gen);
}

bool clear_folder(std::string folderPath) {
    std::cout << "Clear folder: " << folderPath << std::endl;

    if (!fs::exists(folderPath)) {
        std::cerr << "Error: Folder does not exist: " << folderPath << std::endl;
        return false;
    }

    try {
        // 直接清理文件，避免嵌套线程池
        for (const auto& entry : fs::directory_iterator(folderPath)) {
            try {
                fs::remove_all(entry.path());
            } catch (const std::exception& e) {
                std::cerr << "Error removing: " << entry.path() << " - " << e.what() << std::endl;
            }
        }
        std::cout << "Cleared folder" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error clearing folder: " << e.what() << std::endl;
        return false;
    }
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

std::string getFileNameFromPath(const std::string &filePath)
{
    size_t lastSlashIndex = filePath.find_last_of("/\\");
    return (lastSlashIndex != std::string::npos && lastSlashIndex < filePath.length() - 1) ? filePath.substr(lastSlashIndex + 1) : filePath;
}

ThreadPoolWrapper::ThreadPoolWrapper(std::size_t num_threads)
    : tasks_pending(0) {
    create_thread_pool(num_threads);
}

void ThreadPoolWrapper::wait_for_all_tasks(std::int64_t remain) {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [this,remain]() { return tasks_pending <= remain; });
}

void ThreadPoolWrapper::resize(std::size_t new_num_threads) {
    wait_for_all_tasks();
    create_thread_pool(new_num_threads);
}

void ThreadPoolWrapper::create_thread_pool(std::size_t num_threads) {
    thread_pool = std::make_unique<boost::asio::thread_pool>(num_threads);
}

void ThreadPoolWrapper::on_task_done() {
    std::lock_guard<std::mutex> lock(mtx);
    --tasks_pending;
    if (tasks_pending == 0) {
        cv.notify_all();
    }
}

