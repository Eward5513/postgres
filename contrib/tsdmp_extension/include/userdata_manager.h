#ifndef USERDATA_MANAGER_H
#define USERDATA_MANAGER_H

#include <vector>
#include "spatiotemporal_data.h"

class UserDataManager {
private:
    static constexpr const char* TABLE_NAME = "user_data";

public:
    static void clearTable();
    static void writeDataToDatabase(int key, const std::vector<SpatioTemporalData>& data);
    static std::vector<std::vector<SpatioTemporalData>> loadDataFromDatabase(
        int key);
};

#endif // USERDATA_MANAGER_H