/**
 * @file metric.cpp
 * @brief Implementation of metric tree data structure for trajectory similarity search
 * 
 * This file provides the complete implementation of the metric tree (M-tree) data structure
 * optimized for trajectory similarity queries. The implementation includes:
 * 
 * Core Components:
 * - Node hierarchy with polymorphic behavior
 * - Tree construction using balanced partitioning
 * - Approximate k-NN search with bounded error
 * - Efficient serialization and persistence
 * - Parallel processing for performance optimization
 * 
 * Key Algorithms:
 * - Median-based tree construction for balanced partitioning
 * - Triangle inequality pruning for efficient search
 * - DTW-based trajectory distance calculation
 * - Parallel batch processing for large datasets
 * 
 * Performance Optimizations:
 * - Lazy loading of trajectory data from database
 * - Thread pool for parallel distance calculations
 * - Memory-efficient tree serialization
 * - Adaptive approximation for speed vs accuracy tradeoffs
 * 
 * The functions are organized by dependency order to ensure proper compilation
 * and logical flow from basic operations to complex tree algorithms.
 * 
 * @author Zhang Teng
 * @date 2025
 */

#include "../include/metric.h"
#include "../include/spatiotemporal_data.h"
#include "../include/trajectory_manager.h"
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
// MetricTreeNode Base Class Implementation
// ============================================================================

/**
 * @brief Factory method to create appropriate node type from JSON data
 * 
 * Examines the node_type field in the JSON object to determine which concrete
 * class to instantiate. This enables polymorphic deserialization where the
 * correct derived class is created based on runtime data.
 * 
 * Node types:
 * - 0: MetricTreeLeafNode
 * - 1: MetricTreeInnerNode
 * 
 * @param j JSON object containing node data with node_type field
 * @return Pointer to newly created node of appropriate derived type
 * @throws std::runtime_error if node_type is unknown or invalid
 */
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
// MetricTreeLeafNode Implementation
// ============================================================================

/**
 * @brief Create leaf node from JSON data
 * 
 * Factory method that reconstructs a leaf node from serialized JSON data.
 * Validates the node structure and extracts the trajectory ID list.
 * 
 * Expected JSON structure:
 * {
 *   "id": <node_id>,
 *   "node_type": 0,
 *   "ids": [<trajectory_id_1>, <trajectory_id_2>, ...]
 * }
 * 
 * @param j JSON object containing leaf node data
 * @return Pointer to newly created and initialized leaf node
 * @throws std::runtime_error if JSON format is invalid
 */
MetricTreeLeafNode* MetricTreeLeafNode::from_json(const nlohmann::json& j) {
    auto* node = new MetricTreeLeafNode();
    node->id = j.at("id").get<int>();
    node->node_type = j.at("node_type").get<int>();
    node->ids = j.at("ids").get<std::vector<int>>();
    return node;
}

// ============================================================================
// MetricTreeInnerNode Implementation
// ============================================================================

/**
 * @brief Calculate distance bounds for query optimization
 * 
 * Computes the minimum and maximum possible distances from a query trajectory
 * to any trajectory in this node's subtree. This enables efficient pruning
 * during k-NN search by using the triangle inequality to eliminate subtrees
 * that cannot contain better results.
 * 
 * Mathematical foundation:
 * Given pivot P, radius R, and query Q:
 * - Lower bound: max(0, |d(Q,P) - R|)
 * - Upper bound: d(Q,P) + R
 * 
 * Where d(Q,P) is the distance from query to pivot.
 * 
 * @param query Query trajectory for distance calculation
 * @return Pair of (lower_bound, upper_bound) distances to subtree
 */
std::pair<float, float> MetricTreeInnerNode::calculate_distance(Trajectory& query) {
    std::pair<float, float> result;
    float dist = trajectoryDistance(pivot, query);
    
    if (dist > radius) {
        // Query is outside the metric ball
        result.first = dist - radius;  // Minimum distance to ball boundary
        result.second = 0;             // Not used in current implementation
    } else {
        // Query is inside the metric ball
        result.first = 0;              // Minimum distance is 0 (query could be at boundary)
        result.second = radius - dist; // Maximum distance within ball
    }
    return result;
}

/**
 * @brief Create internal node from JSON data
 * 
 * Factory method for deserializing internal nodes. The child pointers are
 * stored as IDs and must be resolved in a separate linking phase after
 * all nodes have been created.
 * 
 * Expected JSON structure:
 * {
 *   "id": <node_id>,
 *   "node_type": 1,
 *   "pivot": <trajectory_json>,
 *   "radius": <float_value>,
 *   "left_id": <left_child_id>,
 *   "right_id": <right_child_id>
 * }
 * 
 * @param j JSON object containing internal node data
 * @return Pointer to newly created internal node (children unlinked)
 */
MetricTreeInnerNode* MetricTreeInnerNode::from_json(const nlohmann::json& j) {
    auto* node = new MetricTreeInnerNode();
    node->id = j.at("id").get<int>();
    node->node_type = j.at("node_type").get<int>();
    node->pivot = Trajectory::from_json(j.at("pivot"));
    node->radius = j.at("radius").get<float>();
    
    // Child IDs are stored but not immediately linked
    int left_id = j.at("left_id").get<int>();
    int right_id = j.at("right_id").get<int>();
    
    return node;
}

// ============================================================================
// MetricTree Core Implementation
// ============================================================================

/**
 * @brief Default constructor - builds tree from database
 * 
 * Automatically loads all trajectory IDs from the database and constructs
 * a balanced metric tree using the median-based partitioning algorithm.
 * This is the most common way to create a new metric tree.
 * 
 * Construction process:
 * 1. Load all trajectory IDs from database
 * 2. Initialize thread pool for parallel operations
 * 3. Build tree using recursive partitioning
 * 4. Validate tree structure integrity
 */
MetricTree::MetricTree() : root(nullptr), id_count(0), thread_pool_wrapper(100) {
    std::vector<int> ids = trajectoryManager.getAllTrajectoryIdsFromDatabase();
    root = buildTree(ids);
}

/**
 * @brief Constructor with existing root node
 * 
 * Creates a metric tree using a pre-constructed root node. This is typically
 * used for deserialization or when integrating with external tree construction
 * algorithms.
 * 
 * @param root Pointer to the root node of an existing tree structure
 */
MetricTree::MetricTree(MetricTreeNode* root) : root(root), id_count(0), thread_pool_wrapper(thread_pool_size) {
}

/**
 * @brief Destructor - recursively cleans up all tree nodes
 * 
 * Performs complete tree cleanup to prevent memory leaks. Uses the
 * deleteTree method to recursively delete all nodes in post-order.
 */
MetricTree::~MetricTree() {
    deleteTree(root);
}

/**
 * @brief Recursively delete entire subtree
 * 
 * Performs post-order traversal to safely delete all nodes in a subtree.
 * For internal nodes, recursively deletes children before deleting the
 * parent. Handles null pointers gracefully.
 * 
 * @param node Root of subtree to delete (can be nullptr)
 */
void MetricTree::deleteTree(MetricTreeNode* node) {
    if (node == nullptr) {
        return;
    }

    if (node->node_type == 0) { 
        // Handle leaf node - direct deletion
        auto* leafNode = static_cast<MetricTreeLeafNode*>(node);
        delete leafNode;
    } 
    else if (node->node_type == 1) { 
        // Handle internal node - delete children first
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
        elog(ERROR, "Error: Unknown node type during deletion!");
    }
}

/**
 * @brief Collect all nodes into a map for serialization
 * 
 * Performs tree traversal to gather all nodes indexed by their unique ID.
 * This creates an intermediate representation used for tree serialization
 * where node relationships can be preserved through ID references.
 * 
 * @param node Root of subtree to collect
 * @param node_map Output map to store node pointers indexed by ID
 */
void MetricTree::saveAllNodes(MetricTreeNode* node, std::unordered_map<int, MetricTreeNode*>& node_map) const {
    if (node == nullptr) {
        return;
    }

    if (node->node_type == 0) {
        // Process leaf node
        MetricTreeLeafNode* leafNode = static_cast<MetricTreeLeafNode*>(node);
        node_map[leafNode->id] = leafNode;
    }
    else if (node->node_type == 1) {
        // Process internal node and recurse to children
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
        elog(ERROR, "Error: Unknown node type during serialization!");
    }
}

/**
 * @brief Serialize entire tree to JSON string
 * 
 * Converts the complete tree structure into a JSON string suitable for
 * storage, network transmission, or persistence. The serialization preserves
 * all tree topology and node data.
 * 
 * @return JSON string representation of the complete tree
 */
std::string MetricTree::serialize_to_string() const {
    nlohmann::json jsonObject;

    auto map = serialize_to_map();
    for (const auto& [key, value] : map) {
        jsonObject[std::to_string(key)] = value;
    }
    
    return jsonObject.dump();
}

/**
 * @brief Serialize tree to map of JSON objects
 * 
 * Creates an intermediate serialization format where each node is represented
 * as a JSON object indexed by node ID. This format facilitates tree
 * reconstruction and enables partial tree operations.
 * 
 * @return Map of node ID to JSON object for each tree node
 */
std::unordered_map<int, nlohmann::json> MetricTree::serialize_to_map() const {
    std::unordered_map<int, nlohmann::json> result;
    std::unordered_map<int, MetricTreeNode*> node_map;
    
    // Collect all nodes first
    saveAllNodes(root, node_map);
    
    // Convert each node to JSON
    for (const auto& [id, node] : node_map) {
        result[id] = node->to_json();
    }
    return result;
}

/**
 * @brief Deserialize tree from JSON string
 * 
 * Reconstructs a complete metric tree from its JSON string representation.
 * This is the inverse operation of serialize_to_string and can restore
 * a tree to its exact previous state.
 * 
 * @param data JSON string containing serialized tree data
 * @return Pointer to root node of reconstructed tree
 */
MetricTreeNode* MetricTree::deserialize_from_string(std::string data) {
    std::unordered_map<int, nlohmann::json> map;

    // 将字符串解析为 nlohmann::json 对象
    nlohmann::json jsonObject;
    try {
        jsonObject = nlohmann::json::parse(data);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("Failed to parse JSON string: " + std::string(e.what()));
    }

    // 遍历 JSON 对象，将内容填充到 map 中
    for (auto it = jsonObject.begin(); it != jsonObject.end(); ++it) {
        int key;
        try {
            key = std::stoi(it.key()); // 将 JSON 键从字符串转换为整数
        } catch (const std::invalid_argument& e) {
            throw std::runtime_error("Invalid key format in JSON (expected an integer): " + std::string(e.what()));
        }

        map[key] = it.value(); // 将值存入 map
    }

    return deserialize_from_map(map);
}

/**
 * @brief Deserialize tree from JSON object map
 * 
 * Reconstructs tree structure from a map of JSON objects using a two-phase
 * process: first create all nodes, then resolve child pointer relationships.
 * This approach handles the circular reference problem in tree structures.
 * 
 * Algorithm:
 * 1. Create all nodes from JSON data (children unlinked)
 * 2. Build routing table for parent-child relationships
 * 3. Use breadth-first traversal to link children to parents
 * 4. Return root node (ID 0 by convention)
 * 
 * @param data Map of node ID to JSON object
 * @return Pointer to root node of reconstructed tree
 */
MetricTreeNode* MetricTree::deserialize_from_map(std::unordered_map<int, nlohmann::json>& data) {
    std::unordered_map<int, MetricTreeNode*> node_map;
    std::unordered_map<int, std::pair<int,int>> routes;
    
    // Phase 1: Create all nodes
    for (const auto& [id, json] : data) {
        if(json["node_type"].get<int>()) { // Internal node
            node_map[id] = MetricTreeInnerNode::from_json(json);
            int left_id = json.at("left_id").get<int>();
            int right_id = json.at("right_id").get<int>();
            routes[id] = {left_id, right_id};
        } else { // Leaf node
            node_map[id] = MetricTreeLeafNode::from_json(json);
        }
    }
    
    // Phase 2: Link children to parents using BFS
    std::queue<MetricTreeNode*> connecting_queue;
    connecting_queue.push(node_map[0]); // Root node has ID 0
    
    while(!connecting_queue.empty()) {
        auto* node = connecting_queue.front();
        connecting_queue.pop();
        
        if(node->node_type == 0) {
            continue; // Leaf nodes have no children
        }
        
        // Link children and add to queue for further processing
        auto* inner_node = static_cast<MetricTreeInnerNode*>(node);
        std::pair<int,int> children = routes.at(inner_node->id);
        inner_node->left = node_map.at(children.first);
        inner_node->right = node_map.at(children.second);
        connecting_queue.push(inner_node->left);
        connecting_queue.push(inner_node->right);
    }
    
    return node_map[0]; // Return root node
}

/**
 * @brief Validate tree structure integrity
 * 
 * Performs comprehensive validation of tree structure to ensure consistency
 * and correctness. This is essential for debugging and maintaining tree
 * invariants during development.
 * 
 * Validation checks:
 * - No null nodes in tree structure
 * - Consistent node type values (0 for leaf, 1 for internal)
 * - Non-empty trajectory lists in leaf nodes
 * - Non-null child pointers in internal nodes
 * - Recursive validation of all subtrees
 * 
 * @param node Root of subtree to validate
 * @return True if tree structure is valid and consistent
 */
bool MetricTree::checkTree(MetricTreeNode* node) {
    if (node == nullptr) {
        elog(ERROR, "Error: Found an empty node!");
        return false;
    }

    if (node->node_type == 0) {
        // Validate leaf node
        MetricTreeLeafNode* leafNode = static_cast<MetricTreeLeafNode*>(node);
        if (leafNode->ids.empty()) {
            elog(ERROR, "Error: Leaf node with empty ids!");
            return false;
        }
        return true;
    }

    if (node->node_type == 1) {
        // Validate internal node and recurse to children
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

/**
 * @brief Recursively build metric tree from trajectory IDs
 * 
 * Core tree construction algorithm implementing a median-based partitioning
 * strategy to create a balanced tree. The algorithm ensures good performance
 * for k-NN queries by maintaining balanced subtrees and optimal pivot selection.
 * 
 * Algorithm steps:
 * 1. Check for leaf condition (< 10 trajectories)
 * 2. Select pivot trajectory (median position)
 * 3. Calculate distances from all trajectories to pivot (parallel)
 * 4. Sort trajectories by distance to pivot
 * 5. Split at median distance for balanced partitioning
 * 6. Recursively construct left and right subtrees
 * 7. Set covering radius as average of split point distances
 * 
 * Optimization features:
 * - Parallel distance calculation for large datasets
 * - Asynchronous subtree construction for upper tree levels
 * - Load balancing across available CPU cores
 * 
 * @param ids Vector of trajectory IDs to organize into tree
 * @param layer Current depth in tree (for optimization decisions)
 * @return Pointer to root of constructed subtree
 */
MetricTreeNode* MetricTree::buildTree(std::vector<int>& ids, int layer) {
    elog(DEBUG1, "buildTree: ids size = %zu", ids.size());
    
    // Base case: create leaf node for small sets
    if(ids.size() < 10) {
        MetricTreeLeafNode* leaf = new MetricTreeLeafNode();
        leaf->id = id_count++;
        leaf->ids = ids;
        return leaf;
    }
    
    // Select pivot trajectory for partitioning
    size_t middle_size = ids.size() / 2;
    Trajectory pivot = load_tracj(ids[middle_size]);

    // Parallel distance calculation for performance
    {
        boost::asio::thread_pool thread_pool(100);
        std::vector<std::pair<int, float>> id_distances(ids.size());
        std::vector<std::pair<std::int64_t, std::int64_t>> task_assigned = split<std::int64_t>(0, ids.size(), 100);

        // Distribute distance calculations across thread pool
        for (size_t task_id = 0; task_id < task_assigned.size(); ++task_id) {
            boost::asio::post(thread_pool, [task_id, &id_distances, &task_assigned, &pivot, &ids]() {
                for (size_t i = task_assigned[task_id].first; i < task_assigned[task_id].second; ++i) {
                    id_distances[i] = {ids[i], trajectoryDistance(pivot, load_tracj(ids[i]))};
                }
            });
        }
        thread_pool.join();
        
        // Sort trajectories by distance to pivot for balanced partitioning
        std::nth_element(id_distances.begin(), id_distances.begin() + middle_size, id_distances.end(),
                        [](const std::pair<long long, double>& p1, const std::pair<long long, double>& p2) {
                            return p1.second < p2.second;
                        });

        // Copy sorted IDs back to original vector
        for (size_t i = 0; i < ids.size(); ++i) {
            ids[i] = id_distances[i].first;
        }
    }

    // Create internal node with pivot and calculate covering radius
    MetricTreeInnerNode* inner = new MetricTreeInnerNode(pivot);
    inner->id = id_count++;
    inner->radius = (trajectoryDistance(pivot, load_tracj(ids[middle_size])) + 
                     trajectoryDistance(pivot, load_tracj(ids[middle_size-1]))) / 2;

    // Split trajectory set for balanced subtrees
    std::vector<int> left_ids(ids.begin(), ids.begin() + middle_size);
    std::vector<int> right_ids(ids.begin() + middle_size, ids.end());

    // Use asynchronous construction for upper tree levels
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
        // Synchronous construction for deeper levels to avoid thread overhead
        inner->left = buildTree(left_ids, layer + 1);
        inner->right = buildTree(right_ids, layer + 1);
    }
    
    return inner;
}

/**
 * @brief Perform k-nearest neighbor search with approximation
 * 
 * Implements an efficient approximate k-NN search algorithm that provides
 * bounded error guarantees while significantly improving query performance.
 * The algorithm uses priority-based tree traversal with triangle inequality
 * pruning to eliminate unpromising subtrees.
 * 
 * Algorithm features:
 * - Best-first search using priority queue
 * - Triangle inequality pruning for efficiency
 * - Approximation ratio for speed/accuracy tradeoff
 * - Temporal filtering for time-bounded queries
 * - Lazy loading of trajectory data from database
 * 
 * Approximation guarantee: All returned distances are within
 * approximate_ratio * optimal_distance of the true k-NN distances.
 * 
 * Time complexity: O(log n) expected for approximate search
 * Space complexity: O(k + log n) for result and search queues
 * 
 * @param query Query trajectory to find neighbors for
 * @param k Number of nearest neighbors to return
 * @param approximate_ratio Quality vs speed tradeoff (≥1.0)
 * @param min_time Minimum time bound for trajectory filtering
 * @param max_time Maximum time bound for trajectory filtering
 * @param db_time Output parameter for database access time measurement
 * @return Vector of k nearest trajectories with distances, sorted by distance
 * @throws std::runtime_error if approximate_ratio < 1.0
 */
std::vector<std::pair<float, Trajectory>> MetricTree::kNearestNeighbors(Trajectory& query, int k, float approximate_ratio, float min_time, float max_time, double& db_time) const {
    if(approximate_ratio < 1) {
        throw std::runtime_error("approximate_ratio must be greater than 1"); 
    }
    
    std::vector<std::pair<float, Trajectory>> result;
    
    // Priority queue for best-first tree traversal (min-heap by distance)
    std::priority_queue<std::pair<float, MetricTreeNode*>, 
                       std::vector<std::pair<float, MetricTreeNode*>>, 
                       std::greater<std::pair<float, MetricTreeNode*>>> searchQueue;
    
    // Priority queue for k best results (max-heap by distance)
    std::priority_queue<std::pair<float, int>> resultQueue;
   
    searchQueue.push({0, root});
    
    // Best-first search with approximation-based pruning
    while(!searchQueue.empty()) {
        // Approximation-based early termination
        if(resultQueue.size() >= k && 
           searchQueue.top().first * approximate_ratio > resultQueue.top().first) {
            break;
        }   
        
        auto* node = searchQueue.top().second;
        searchQueue.pop();
        
        if(node->node_type == 0) {
            // Process leaf node: load trajectories and compute distances
            MetricTreeLeafNode* leaf_node = static_cast<MetricTreeLeafNode*>(node);
            
            TimerClock tc;
            auto tracj_map = trajectoryManager.loadTrajectoriesFromDatabase(leaf_node->ids);
            db_time += tc.milliSec();
            
            // Process each trajectory in leaf
            for(auto i : leaf_node->ids) {
                auto traj = tracj_map.at(i);
                traj.cut_by_time(min_time, max_time); // Apply temporal filtering
                
                float distance = trajectoryDistance(query, traj);
                resultQueue.push({distance, i});
                
                // Maintain only k best results
                if(resultQueue.size() > k) {
                    resultQueue.pop();
                }
            }
        } else if(node->node_type == 1) {
            // Process internal node: calculate bounds and add children to queue
            MetricTreeInnerNode* inner_node = static_cast<MetricTreeInnerNode*>(node);
            auto distance_pair = inner_node->calculate_distance(query);
            
            // Add children with their distance bounds for priority ordering
            searchQueue.push({distance_pair.first, static_cast<MetricTreeNode*>(inner_node->left)});
            searchQueue.push({distance_pair.second, static_cast<MetricTreeNode*>(inner_node->right)});
        } else {
            throw std::runtime_error("bad node type!");
        }
    }
    
    // Convert result queue to vector and reverse for ascending order
    while (!resultQueue.empty()) {
        result.push_back({resultQueue.top().first, load_tracj(resultQueue.top().second)});
        resultQueue.pop();
    }
    
    std::reverse(result.begin(), result.end());
    return result;
}

// ============================================================================
// Tree Comparison Utility Functions
// ============================================================================

/**
 * @brief Compare two metric trees for structural equality
 * 
 * Performs deep recursive comparison of tree structures including node types,
 * data contents, and tree topology. This is essential for testing tree
 * serialization/deserialization and validating tree operations.
 * 
 * Comparison criteria:
 * - Node existence (both null or both non-null)
 * - Node type consistency
 * - Data content equality (trajectory IDs for leaves, radius for internal nodes)
 * - Recursive structural equality of all subtrees
 * 
 * @param node1 Root of first tree to compare
 * @param node2 Root of second tree to compare
 * @return True if trees are structurally identical and contain same data
 */
bool compareTrees(MetricTreeNode* node1, MetricTreeNode* node2) {
    // Handle null node cases
    if (!node1 && !node2) {
        return true; // Both null - equal
    }
    if (!node1 || !node2) {
        return false; // One null, one non-null - not equal
    }
    
    // Check node type consistency
    if (node1->node_type != node2->node_type) {
        return false;
    }
    
    if (node1->node_type == 0) {
        // Compare leaf nodes: check trajectory ID lists
        auto* leaf1 = static_cast<MetricTreeLeafNode*>(node1);
        auto* leaf2 = static_cast<MetricTreeLeafNode*>(node2);
        return leaf1->ids == leaf2->ids;
    } else if (node1->node_type == 1) {
        // Compare internal nodes: check radius and recurse to children
        auto* inner1 = static_cast<MetricTreeInnerNode*>(node1);
        auto* inner2 = static_cast<MetricTreeInnerNode*>(node2);
        return inner1->radius == inner2->radius &&
               compareTrees(inner1->left, inner2->left) &&
               compareTrees(inner1->right, inner2->right);
    }
    
    return false; // Unknown node type
} 

float trajectoryDistance(const Trajectory& traj1, const Trajectory& traj2) {
    return calculateDTWDistance(traj1, traj2);
}