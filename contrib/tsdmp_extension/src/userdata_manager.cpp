#include "../include/safe_header.h"
#include "../include/userdata_manager.h"

void UserDataManager::clearTable() {
    auto connection = PG_pool->acquire(); // 从连接池获取连接
    pqxx::connection& C = *connection;

    pqxx::work txn(C);
    txn.exec0("DROP TABLE IF EXISTS " + std::string(TABLE_NAME) + ";");

    // 重新创建表，保留 key 和 data 列，并为 data 列创建普通索引
    txn.exec0(
        "CREATE TABLE " + std::string(TABLE_NAME) + " ("
        "key INT, "
        "lo_oid OID);"
        // "data BYTEA);"
    );

    // 在 key 列上创建一个可重复的索引
    txn.exec0(
        "CREATE INDEX idx_key"+std::string(TABLE_NAME)+" ON " + std::string(TABLE_NAME) + " (key);"
    );

    txn.commit(); // 提交事务
    PG_pool->release(connection); // 释放连接
}

void UserDataManager::writeDataToDatabase(int key, const std::vector<SpatioTemporalData>& data) {
    auto connection = PG_pool->acquire();
    pqxx::connection& C = *connection;

    pqxx::work txn(C);
    pqxx::oid oid = global_loid.fetch_add(1);
    txn.exec_params("SELECT lo_create($1);", oid);
    {
        pqxx::largeobjectaccess lo_access(txn, oid, std::ios_base::out);
        lo_access.write(reinterpret_cast<const char*>(data.data()), data.size() * sizeof(SpatioTemporalData));
    }
    txn.exec_params(
        "INSERT INTO " + std::string(TABLE_NAME) + " (key, lo_oid) VALUES ($1, $2);",
        key,
        oid);
    txn.commit();
    PG_pool->release(connection);
}



std::vector<std::vector<SpatioTemporalData>> UserDataManager::loadDataFromDatabase(
    int key) {
    std::vector<std::vector<SpatioTemporalData>> data;

    // auto connection = PG_pool->acquire(); // 从连接池获取连接
    // pqxx::nontransaction txn(*connection);

    // // 查询数据库
    // TimerClock tc;
    // pqxx::result db_result = txn.exec(
    //     "SELECT data FROM " + std::string(TABLE_NAME) + " WHERE key = " + txn.quote(key) + ";"
    // );

    // txn.commit(); // 提交事务
    // PG_pool->release(connection); // 释放连接
    // db_time = tc.milliSec();
    // if (!db_result.empty()) {
    //     // 遍历查询结果的每一行
    //     std::cout <<"db_result:"<<db_result.size()<< std::endl;
    //     for (std::int64_t i = 0;i<db_result.size();++i) {
    //         auto& row = db_result[i];
    //         std::string bin = row[0].as<std::string>();
    //         // 解析 binarystring 数据为结构体
    //         const SpatioTemporalData* buffer = reinterpret_cast<const SpatioTemporalData*>(bin.data());
    //         std::cout <<"raw size:"<<row[0].size()<< std::endl;
    //         std::cout <<"bin size:"<<bin.size()<< std::endl;
    //         size_t num_items = bin.size() / sizeof(SpatioTemporalData);
    //         size_t total_size = bin.size();
    //         size_t item_size = sizeof(SpatioTemporalData);
    //         pqxx::binarystring bin1(row[0]);
    //         std::cout <<"binarystring_size:"<<bin1.size()<< std::endl;
    //         // std::cout <<"total_size:"<<total_size<< std::endl;
    //         // std::cout <<total_size<< std::endl;
    //         // size_t raw_total_size = row[0].size();
    //         // std::cout <<"raw_total_size:"<<raw_total_size<< std::endl;

    //         if (total_size % item_size != 0) {
    //             std::cerr << "Error: The binary data size does not match the expected size for SpatioTemporalData." << std::endl;
    //             return data; // 发生错误，返回空的 data
    //         }
    
    //         auto start = std::lower_bound(
    //             buffer, buffer + num_items, min_time,
    //             [](const SpatioTemporalData& d, long long time) {
    //                 return d.time < time;
    //             }) - buffer;
    //         while (start > 0 && buffer[start].time >= min_time) {
    //             --start;
    //         }
    //         while (start < num_items && buffer[start].time < min_time) {
    //             ++start;
    //         }
    //         for (std::int64_t i = start; i < num_items; ++i) {
    //             if (buffer[i].time > max_time) {
    //                 break;
    //             }
    //             if ((buffer[i].tid & type)) {
    //                 data.push_back(buffer[i]);
    //             }
    //         }
    //     }
    // } else {
    //     throw StringException("SpatioTemporalData load empty for key " + std::to_string(key));
    // }


        auto connection = PG_pool->acquire(); // 从连接池获取连接
        pqxx::connection& C = *connection;
        pqxx::work txn(C);
        // 1. 查询指定 key 对应的所有 OID
        pqxx::result result = txn.exec_params(
            "SELECT lo_oid FROM " + std::string(TABLE_NAME) + " WHERE key = $1;", key);
        if (result.empty()) {
            return data;
            throw std::runtime_error("No data found for the specified key.");
        }
        data.reserve(result.size());
        // 2. 遍历所有 OID 并加载数据
        for (const auto& row : result) {
            pqxx::oid oid = row["lo_oid"].as<pqxx::oid>();
            pqxx::largeobjectaccess lo_access(txn, oid, std::ios_base::in);
            // 获取大对象大小
            lo_access.seek(0, std::ios_base::end); // 移动到大对象末尾
            std::streamoff size = lo_access.tell(); // 获取当前偏移量（即大对象大小）
            lo_access.seek(0, std::ios_base::beg); // 移动回大对象开头

            if (size % sizeof(SpatioTemporalData) != 0) {
                throw std::runtime_error("Invalid data size for SpatioTemporalData.");
            }

            // 读取二进制数据
            data.emplace_back();
            data.back().resize(size / sizeof(SpatioTemporalData));
            lo_access.read(reinterpret_cast<char*>(data.back().data()), size);
        }
        txn.commit();
        PG_pool->release(connection); // 释放连接
    return data;
}