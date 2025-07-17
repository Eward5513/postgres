#ifndef METRIC_H
#define METRIC_H

#include <iostream>
#include <vector>
#include <unordered_map>
#include <string>
#include <queue>
#include <future>
#include "nlohmann/json.hpp"

// Forward declarations
class Trajectory;
class ThreadPoolWrapper;
class TimerClock;
class MetricTreeNode;
class MetricTreeLeafNode;
class MetricTreeInnerNode;

// ============================================================================
// MetricTree Node Classes
// ============================================================================

class MetricTreeNode {
public:
    int node_type = -1;
    int id;
    
    virtual ~MetricTreeNode() = default;

    // Simple virtual method for serialization
    virtual nlohmann::json to_json() const {
        return nlohmann::json{{"node_type", node_type}, {"id", id}};
    }

    // Complex function - declaration only
    static MetricTreeNode* from_json(const nlohmann::json& j);
};

class MetricTreeLeafNode : public MetricTreeNode {
public:
    std::vector<int> ids;
    
    MetricTreeLeafNode() {
        MetricTreeNode::node_type = 0;
    }
    
    // Simple serialization method
    nlohmann::json to_json() const override {
        auto j = MetricTreeNode::to_json();
        j["ids"] = ids;
        return j;
    }

    // Complex function - declaration only
    static MetricTreeLeafNode* from_json(const nlohmann::json& j);
};

class MetricTreeInnerNode : public MetricTreeNode {
public:
    Trajectory pivot;      // 中枢点
    float radius;          // 中枢点到子树的最大距离
    MetricTreeNode* left;   // 左子树指针
    MetricTreeNode* right;  // 右子树指针
    
    MetricTreeInnerNode(Trajectory& pivot) : pivot(pivot), radius(0), left(nullptr), right(nullptr) {
        MetricTreeNode::node_type = 1;
    }
    
    MetricTreeInnerNode() : radius(0), left(nullptr), right(nullptr) {
        MetricTreeNode::node_type = 1;
    }

    nlohmann::json to_json() const override {
        auto j = MetricTreeNode::to_json();
        j["pivot"] = pivot.to_json();
        j["radius"] = radius;
        j["left_id"] = left ? left->id : -1;
        j["right_id"] = right ? right->id : -1;
        return j;
    }

    // Complex functions - declarations only
    std::pair<float, float> calculate_distance(Trajectory& query);
    static MetricTreeInnerNode* from_json(const nlohmann::json& j);
};

// ============================================================================
// MetricTree Class
// ============================================================================

class MetricTree {
public:
    MetricTreeNode* root;
    int id_count = 0;
    ThreadPoolWrapper thread_pool_wrapper;
    
    // Constructors and destructor
    MetricTree();
    MetricTree(MetricTreeNode* root);
    ~MetricTree();
    
    // Complex functions - declarations only
    void deleteTree(MetricTreeNode* node);
    void saveAllNodes(MetricTreeNode* node, std::unordered_map<int, MetricTreeNode*>& node_map) const;
    std::string serialize_to_string() const;
    std::unordered_map<int, nlohmann::json> serialize_to_map() const;
    static MetricTreeNode* deserialize_from_string(std::string data);
    static MetricTreeNode* deserialize_from_map(std::unordered_map<int, nlohmann::json>& data);
    bool checkTree(MetricTreeNode* node);
    MetricTreeNode* buildTree(std::vector<int>& ids, int layer = 0);
    std::vector<std::pair<float, Trajectory>> kNearestNeighbors(Trajectory& query, int k, float approximate_ratio, float min_time, float max_time, double& db_time) const;
};

// ============================================================================
// Utility Functions
// ============================================================================

// Complex functions - declarations only
bool compareTrees(MetricTreeNode* node1, MetricTreeNode* node2);
std::string encodeMapToJSONString(const std::unordered_map<int, nlohmann::json>& map);
std::unordered_map<int, nlohmann::json> decodeJSONStringToMap(const std::string& jsonString);

// Forward declare utility functions that might be missing
Trajectory load_tracj(int id);
float trajectoryDistance(const Trajectory& t1, const Trajectory& t2);

template<typename T>
std::vector<std::pair<T, T>> split(T start, T end, int num_parts);

#endif // METRIC_H
