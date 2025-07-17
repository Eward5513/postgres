#include "../include/metric.h"
#include "../include/spatiotemporal_data.h"
#include <algorithm>
#include <queue>
#include <future>
#include <boost/asio/thread_pool.hpp>
#include <boost/asio/post.hpp>

extern "C" {
#include "postgres.h"
#include "utils/elog.h"
}

// ============================================================================
// MetricTreeNode Implementations
// ============================================================================

MetricTreeNode* MetricTreeNode::from_json(const nlohmann::json& j) {
    int node_type = j.at("node_type").get<int>();
    if (node_type == 0) {
        return MetricTreeLeafNode::from_json(j);
    } else if (node_type == 1) {
        return MetricTreeInnerNode::from_json(j);
    } else {
        throw std::runtime_error("Unknown node type in JSON");
    }
}

// ============================================================================
// MetricTreeLeafNode Implementations
// ============================================================================

MetricTreeLeafNode* MetricTreeLeafNode::from_json(const nlohmann::json& j) {
    auto* node = new MetricTreeLeafNode();
    node->id = j.at("id").get<int>();
    node->node_type = j.at("node_type").get<int>();
    node->ids = j.at("ids").get<std::vector<int>>();
    return node;
}

// ============================================================================
// MetricTreeInnerNode Implementations
// ============================================================================

std::pair<float, float> MetricTreeInnerNode::calculate_distance(Trajectory& query) {
    std::pair<float, float> result;
    float dist = trajectoryDistance(pivot, query);
    if (dist > radius) {
        result.first = dist - radius;
        result.second = 0;
    } else {
        result.first = 0;
        result.second = radius - dist;
    }
    return result;
}

MetricTreeInnerNode* MetricTreeInnerNode::from_json(const nlohmann::json& j) {
    auto* node = new MetricTreeInnerNode();
    node->id = j.at("id").get<int>();
    node->node_type = j.at("node_type").get<int>();
    node->pivot = Trajectory::from_json(j.at("pivot"));
    node->radius = j.at("radius").get<float>();
    int left_id = j.at("left_id").get<int>();
    int right_id = j.at("right_id").get<int>();
    return node;
}

// ============================================================================
// MetricTree Implementations
// ============================================================================

MetricTree::MetricTree() : root(nullptr), id_count(0), thread_pool_wrapper(100) {
    std::vector<int> ids = TrajectoryManager::getAllTrajectoryIdsFromDatabase();
    root = buildTree(ids);
}

MetricTree::MetricTree(MetricTreeNode* root) : root(root), id_count(0), thread_pool_wrapper(thread_pool_size) {
}

MetricTree::~MetricTree() {
    deleteTree(root);
}

void MetricTree::deleteTree(MetricTreeNode* node) {
    if (node == nullptr) {
        return;
    }

    if (node->node_type == 0) { 
        // 处理叶子节点
        auto* leafNode = static_cast<MetricTreeLeafNode*>(node);
        delete leafNode;
    } 
    else if (node->node_type == 1) { 
        // 处理内部节点
        auto* innerNode = static_cast<MetricTreeInnerNode*>(node);

        if (innerNode->left != nullptr) {
            deleteTree(innerNode->left);
        }
        if (innerNode->right != nullptr) {
            deleteTree(innerNode->right);
        }

        delete innerNode;
    } 
    else {
        elog(ERROR, "Error: Unknown node type!");
    }
}

void MetricTree::saveAllNodes(MetricTreeNode* node, std::unordered_map<int, MetricTreeNode*>& node_map) const {
    if (node == nullptr) {
        return;
    }

    if (node->node_type == 0) {
        MetricTreeLeafNode* leafNode = static_cast<MetricTreeLeafNode*>(node);
        node_map[leafNode->id] = leafNode;
    }
    else if (node->node_type == 1) {
        MetricTreeInnerNode* innerNode = static_cast<MetricTreeInnerNode*>(node);
        node_map[innerNode->id] = innerNode;

        if (innerNode->left != nullptr) {
            saveAllNodes(innerNode->left, node_map);
        }
        if (innerNode->right != nullptr) {
            saveAllNodes(innerNode->right, node_map);
        }
    }
    else {
        elog(ERROR, "Error: Unknown node type!");
    }
}

std::string MetricTree::serialize_to_string() const {
    return encodeMapToJSONString(serialize_to_map());
}

std::unordered_map<int, nlohmann::json> MetricTree::serialize_to_map() const {
    std::unordered_map<int, nlohmann::json> result;
    std::unordered_map<int, MetricTreeNode*> node_map;
    saveAllNodes(root, node_map);
    for (const auto& [id, node] : node_map) {
        result[id] = node->to_json();
    }
    return result;
}

MetricTreeNode* MetricTree::deserialize_from_string(std::string data) {
    auto map_json = decodeJSONStringToMap(data);
    return deserialize_from_map(map_json);
}

MetricTreeNode* MetricTree::deserialize_from_map(std::unordered_map<int, nlohmann::json>& data) {
    std::unordered_map<int, MetricTreeNode*> node_map;
    std::unordered_map<int, std::pair<int,int>> routes;
    
    for (const auto& [id, json] : data) {
        if(json["node_type"].get<int>()) { // innernode
            node_map[id] = MetricTreeInnerNode::from_json(json);
            int left_id = json.at("left_id").get<int>();
            int right_id = json.at("right_id").get<int>();
            routes[id] = {left_id, right_id};
        } else {
            node_map[id] = MetricTreeLeafNode::from_json(json);
        }
    }
    
    std::queue<MetricTreeNode*> connecting_queue;
    connecting_queue.push(node_map[0]); // root
    
    while(!connecting_queue.empty()) {
        auto* node = connecting_queue.front();
        connecting_queue.pop();
        
        if(node->node_type == 0) {
            continue;
        }
        
        auto* inner_node = static_cast<MetricTreeInnerNode*>(node);
        std::pair<int,int> children = routes.at(inner_node->id);
        inner_node->left = node_map.at(children.first);
        inner_node->right = node_map.at(children.second);
        connecting_queue.push(inner_node->left);
        connecting_queue.push(inner_node->right);
    }
    
    return node_map[0];
}

bool MetricTree::checkTree(MetricTreeNode* node) {
    if (node == nullptr) {
        elog(ERROR, "Error: Found an empty node!");
        return false;
    }

    if (node->node_type == 0) {
        MetricTreeLeafNode* leafNode = static_cast<MetricTreeLeafNode*>(node);
        if (leafNode->ids.empty()) {
            elog(ERROR, "Error: Leaf node with empty ids!");
            return false;
        }
        return true;
    }

    if (node->node_type == 1) {
        MetricTreeInnerNode* innerNode = static_cast<MetricTreeInnerNode*>(node);
        if (innerNode->left == nullptr || innerNode->right == nullptr) {
            elog(ERROR, "Error: Inner node with empty child!");
            return false;
        }

        bool leftValid = checkTree(innerNode->left);
        bool rightValid = checkTree(innerNode->right);

        return leftValid && rightValid;
    }

    elog(ERROR, "Error: Unknown node type!");
    return false;
}

MetricTreeNode* MetricTree::buildTree(std::vector<int>& ids, int layer) {
    elog(DEBUG1, "buildTree: ids size = %zu", ids.size());
    
    if(ids.size() < 10) {
        MetricTreeLeafNode* leaf = new MetricTreeLeafNode();
        leaf->id = id_count++;
        leaf->ids = ids;
        return leaf;
    }
    
    // 选择一个中枢点作为分割点
    size_t middle_size = ids.size() / 2;
    Trajectory pivot = load_tracj(ids[middle_size]);

    {
        boost::asio::thread_pool thread_pool(100);
        std::vector<std::pair<int, float>> id_distances(ids.size());
        std::vector<std::pair<std::int64_t, std::int64_t>> task_assigned = split<std::int64_t>(0, ids.size(), 100);

        for (size_t task_id = 0; task_id < task_assigned.size(); ++task_id) {
            boost::asio::post(thread_pool, [task_id, &id_distances, &task_assigned, &pivot, &ids]() {
                for (size_t i = task_assigned[task_id].first; i < task_assigned[task_id].second; ++i) {
                    id_distances[i] = {ids[i], trajectoryDistance(pivot, load_tracj(ids[i]))};
                }
            });
        }
        thread_pool.join();
        
        std::nth_element(id_distances.begin(), id_distances.begin() + middle_size, id_distances.end(),
                        [](const std::pair<long long, double>& p1, const std::pair<long long, double>& p2) {
                            return p1.second < p2.second; // 根据距离排序
                        });

        // 步骤 3: 将排好序的 id 拷贝回原始 ids
        for (size_t i = 0; i < ids.size(); ++i) {
            ids[i] = id_distances[i].first;
        }
    }

    // 创建一个节点，存储分割信息
    MetricTreeInnerNode* inner = new MetricTreeInnerNode(pivot);
    inner->id = id_count++;
    inner->radius = (trajectoryDistance(pivot, load_tracj(ids[middle_size])) + 
                     trajectoryDistance(pivot, load_tracj(ids[middle_size-1]))) / 2;

    // 递归构建左右子树
    std::vector<int> left_ids(ids.begin(), ids.begin() + middle_size);
    std::vector<int> right_ids(ids.begin() + middle_size, ids.end());

    if(layer <= 2) {
        auto left_future = std::async(std::launch::async, [this, left_ids, layer]() mutable {
            return this->buildTree(left_ids, layer + 1);
        });

        auto right_future = std::async(std::launch::async, [this, right_ids, layer]() mutable {
            return this->buildTree(right_ids, layer + 1);
        });
        
        inner->left = left_future.get();
        inner->right = right_future.get();
    } else {
        inner->left = buildTree(left_ids, layer + 1);
        inner->right = buildTree(right_ids, layer + 1);
    }
    
    return inner;
}

std::vector<std::pair<float, Trajectory>> MetricTree::kNearestNeighbors(Trajectory& query, int k, float approximate_ratio, float min_time, float max_time, double& db_time) const {
    if(approximate_ratio < 1) {
        throw StringException("approximate_ratio must be greater than 1"); 
    }
    
    std::vector<std::pair<float, Trajectory>> result;
    std::priority_queue<std::pair<float, MetricTreeNode*>, std::vector<std::pair<float, MetricTreeNode*>>, std::greater<std::pair<float, MetricTreeNode*>>> searchQueue;
    std::priority_queue<std::pair<float, int>> resultQueue;
   
    searchQueue.push({0, root});
    
    while(!searchQueue.empty()) {
        if(resultQueue.size() >= k && searchQueue.top().first * approximate_ratio > resultQueue.top().first) {
            break;
        }   
        
        auto* node = searchQueue.top().second;
        searchQueue.pop();
        
        if(node->node_type == 0) {
            MetricTreeLeafNode* leaf_node = static_cast<MetricTreeLeafNode*>(node);
            TimerClock tc;
            auto tracj_map = TrajectoryManager::loadTrajectoriesFromDatabase(leaf_node->ids);
            db_time += tc.milliSec();
            
            for(auto i : leaf_node->ids) {
                auto traj = tracj_map.at(i);
                traj.cut_by_time(min_time, max_time);
                resultQueue.push({trajectoryDistance(query, traj), i});
                if(resultQueue.size() > k) {
                    resultQueue.pop();
                }
            }
        } else if(node->node_type == 1) {
            MetricTreeInnerNode* inner_node = static_cast<MetricTreeInnerNode*>(node);
            auto distance_pair = inner_node->calculate_distance(query);
            searchQueue.push({distance_pair.first, static_cast<MetricTreeNode*>(inner_node->left)});
            searchQueue.push({distance_pair.second, static_cast<MetricTreeNode*>(inner_node->right)});
        } else {
            throw StringException("bad node type!");
        }
    }
    
    while (!resultQueue.empty()) {
        result.push_back({resultQueue.top().first, load_tracj(resultQueue.top().second)});
        resultQueue.pop();
    }
    
    std::reverse(result.begin(), result.end());
    return result;
}

// ============================================================================
// Utility Function Implementations
// ============================================================================

bool compareTrees(MetricTreeNode* node1, MetricTreeNode* node2) {
    if (!node1 && !node2) {
        return true;
    }
    if (!node1 || !node2) {
        return false;
    }
    if (node1->node_type != node2->node_type) {
        return false;
    }
    
    if (node1->node_type == 0) {
        // Leaf nodes
        auto* leaf1 = static_cast<MetricTreeLeafNode*>(node1);
        auto* leaf2 = static_cast<MetricTreeLeafNode*>(node2);
        return leaf1->ids == leaf2->ids;
    } else if (node1->node_type == 1) {
        // Inner nodes
        auto* inner1 = static_cast<MetricTreeInnerNode*>(node1);
        auto* inner2 = static_cast<MetricTreeInnerNode*>(node2);
        return inner1->radius == inner2->radius &&
               compareTrees(inner1->left, inner2->left) &&
               compareTrees(inner1->right, inner2->right);
    }
    
    return false;
}

std::string encodeMapToJSONString(const std::unordered_map<int, nlohmann::json>& map) {
    nlohmann::json json_array = nlohmann::json::array();
    for (const auto& [key, value] : map) {
        nlohmann::json entry;
        entry["key"] = key;
        entry["value"] = value;
        json_array.push_back(entry);
    }
    return json_array.dump();
}

std::unordered_map<int, nlohmann::json> decodeJSONStringToMap(const std::string& jsonString) {
    std::unordered_map<int, nlohmann::json> result;
    nlohmann::json json_array = nlohmann::json::parse(jsonString);
    
    for (const auto& entry : json_array) {
        int key = entry.at("key").get<int>();
        nlohmann::json value = entry.at("value");
        result[key] = value;
    }
    
    return result;
}

// ============================================================================
// Template Function Implementation
// ============================================================================

template<typename T>
std::vector<std::pair<T, T>> split(T start, T end, int num_parts) {
    std::vector<std::pair<T, T>> result;
    T range = end - start;
    T part_size = range / num_parts;
    
    for (int i = 0; i < num_parts; ++i) {
        T part_start = start + i * part_size;
        T part_end = (i == num_parts - 1) ? end : start + (i + 1) * part_size;
        result.push_back({part_start, part_end});
    }
    
    return result;
}

// Explicit template instantiation
template std::vector<std::pair<std::int64_t, std::int64_t>> split<std::int64_t>(std::int64_t, std::int64_t, int); 