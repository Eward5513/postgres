/**
 * @file metric.h
 * @brief Metric tree data structure for efficient trajectory similarity search
 * 
 * This file implements a metric tree (M-tree) data structure optimized for
 * trajectory similarity queries. The metric tree is a balanced tree structure
 * that organizes data based on distance metrics rather than coordinate comparisons,
 * making it ideal for high-dimensional similarity search problems.
 * 
 * Key components:
 * - MetricTreeNode: Base class for tree nodes
 * - MetricTreeLeafNode: Leaf nodes containing trajectory IDs
 * - MetricTreeInnerNode: Internal nodes with pivot trajectories and radii
 * - MetricTree: Main tree structure with k-NN search capabilities
 * - Utility functions for serialization and tree operations
 * 
 * The implementation supports:
 * - Dynamic trajectory insertion and deletion
 * - Approximate k-nearest neighbor search with bounded error
 * - Tree serialization/deserialization for persistence
 * - Parallel query processing for performance
 * 
 * @author Zhang Teng
 * @date 2024
 */

#ifndef METRIC_H
#define METRIC_H

#include <iostream>
#include <vector>
#include <unordered_map>
#include <string>
#include <queue>
#include <future>
#include "nlohmann/json.hpp"

#include "../include/utils.h"
#include "../include/spatiotemporal_data.h"
#include "../include/trajectory_manager.h"

// Forward declarations to avoid circular dependencies
class MetricTreeNode;
class MetricTreeLeafNode;
class MetricTreeInnerNode;

// ============================================================================
// MetricTree Node Classes - Hierarchical Structure
// ============================================================================

/**
 * @brief Abstract base class for all metric tree nodes
 * 
 * Provides the common interface for both leaf and internal nodes in the metric tree.
 * Uses polymorphism to enable unified tree traversal and operations while allowing
 * specialized behavior for different node types.
 * 
 * The node_type field distinguishes between:
 * - 0: Leaf node (MetricTreeLeafNode)
 * - 1: Internal node (MetricTreeInnerNode)
 */
class MetricTreeNode {
public:
    int node_type = -1;    ///< Node type identifier (0=leaf, 1=internal, -1=uninitialized)
    int id;                ///< Unique identifier for this node within the tree
    
    virtual ~MetricTreeNode() = default;

    /**
     * @brief Serialize node to JSON format
     * 
     * Provides basic serialization that derived classes can extend.
     * The base implementation includes common fields (node_type, id).
     * 
     * @return JSON object with node data
     */
    virtual nlohmann::json to_json() const {
        return nlohmann::json{{"node_type", node_type}, {"id", id}};
    }

    /**
     * @brief Factory method to create node from JSON data
     * 
     * Examines the node_type field to determine which concrete class to instantiate
     * and delegates the actual construction to the appropriate derived class.
     * 
     * @param j JSON object containing node data
     * @return Pointer to newly created node of appropriate type
     * @throws std::runtime_error if node_type is unknown
     */
    static MetricTreeNode* from_json(const nlohmann::json& j);
};

/**
 * @brief Leaf node containing trajectory identifiers
 * 
 * Leaf nodes are the terminal nodes of the metric tree that contain actual
 * data references (trajectory IDs). They store a collection of trajectory
 * identifiers that can be efficiently loaded and compared during search operations.
 * 
 * Leaf nodes are created when:
 * - The number of trajectories falls below a threshold (typically < 10)
 * - Maximum tree depth is reached
 * - No further meaningful partitioning is possible
 */
class MetricTreeLeafNode : public MetricTreeNode {
public:
    std::vector<int> ids;    ///< List of trajectory IDs stored in this leaf
    
    /**
     * @brief Constructor initializing node type
     */
    MetricTreeLeafNode() {
        MetricTreeNode::node_type = 0;
    }
    
    /**
     * @brief Serialize leaf node to JSON format
     * 
     * Extends base serialization to include the trajectory ID list.
     * The resulting JSON can be used for tree persistence or network transmission.
     * 
     * @return JSON object containing node type, ID, and trajectory list
     */
    nlohmann::json to_json() const override {
        auto j = MetricTreeNode::to_json();
        j["ids"] = ids;
        return j;
    }

    /**
     * @brief Create leaf node from JSON data
     * 
     * Factory method that reconstructs a leaf node from serialized JSON data.
     * Validates the node type and extracts the trajectory ID list.
     * 
     * @param j JSON object containing leaf node data
     * @return Pointer to newly created leaf node
     * @throws std::runtime_error if JSON format is invalid
     */
    static MetricTreeLeafNode* from_json(const nlohmann::json& j);
};

/**
 * @brief Internal node with pivot trajectory and covering radius
 * 
 * Internal nodes organize the tree structure by partitioning the data space
 * around a pivot trajectory. Each internal node defines a metric ball with:
 * - A pivot trajectory (center of the ball)
 * - A radius (maximum distance from pivot to any contained trajectory)
 * - Two child subtrees (left and right)
 * 
 * The tree construction algorithm ensures that:
 * - All trajectories in child subtrees are within the covering radius
 * - The pivot is chosen to minimize the covering radius
 * - The partitioning promotes balanced tree growth
 */
class MetricTreeInnerNode : public MetricTreeNode {
public:
    Trajectory pivot;             ///< Center trajectory for this node's metric ball
    float radius;                 ///< Maximum distance from pivot to any child trajectory
    MetricTreeNode* left;         ///< Left child subtree pointer
    MetricTreeNode* right;        ///< Right child subtree pointer
    
    /**
     * @brief Constructor with pivot trajectory
     * @param pivot Reference trajectory to use as the node's center
     */
    MetricTreeInnerNode(Trajectory& pivot) : pivot(pivot), radius(0), left(nullptr), right(nullptr) {
        MetricTreeNode::node_type = 1;
    }
    
    /**
     * @brief Default constructor for deserialization
     */
    MetricTreeInnerNode() : radius(0), left(nullptr), right(nullptr) {
        MetricTreeNode::node_type = 1;
    }

    /**
     * @brief Serialize internal node to JSON format
     * 
     * Includes pivot trajectory data, radius, and child node references.
     * Child nodes are referenced by ID to avoid recursive serialization.
     * 
     * @return JSON object with complete node state
     */
    nlohmann::json to_json() const override {
        auto j = MetricTreeNode::to_json();
        j["pivot"] = pivot.to_json();
        j["radius"] = radius;
        j["left_id"] = left ? left->id : -1;
        j["right_id"] = right ? right->id : -1;
        return j;
    }

    /**
     * @brief Calculate distance bounds for query pruning
     * 
     * Computes the minimum and maximum possible distances from a query trajectory
     * to any trajectory in this node's subtree. This enables efficient pruning
     * during k-NN search by eliminating subtrees that cannot contain closer results.
     * 
     * The bounds are calculated using the triangle inequality:
     * - Lower bound: max(0, |d(query,pivot) - radius|)
     * - Upper bound: d(query,pivot) + radius
     * 
     * @param query Query trajectory for distance calculation
     * @return Pair of (lower_bound, upper_bound) distances
     */
    std::pair<float, float> calculate_distance(Trajectory& query);

    /**
     * @brief Create internal node from JSON data
     * 
     * Factory method for deserializing internal nodes. Note that child pointers
     * are not immediately resolved - this requires a separate linking phase.
     * 
     * @param j JSON object containing internal node data
     * @return Pointer to newly created internal node (children unlinked)
     */
    static MetricTreeInnerNode* from_json(const nlohmann::json& j);
};

// ============================================================================
// MetricTree Main Class - Complete Tree Structure
// ============================================================================

/**
 * @brief Complete metric tree implementation for trajectory similarity search
 * 
 * The MetricTree provides efficient k-nearest neighbor search for trajectory data
 * using a balanced tree structure organized by distance metrics. It supports:
 * 
 * Features:
 * - Automatic tree construction from trajectory database
 * - Approximate k-NN search with quality guarantees
 * - Tree serialization/deserialization for persistence
 * - Parallel query processing for performance
 * - Memory-efficient storage and lazy loading
 * 
 * Performance characteristics:
 * - Construction time: O(n log n) expected
 * - Query time: O(log n) for approximate search
 * - Memory usage: O(n) for tree structure
 * 
 * The tree uses DTW (Dynamic Time Warping) distance for trajectory comparison,
 * which handles trajectories with different lengths and temporal alignment.
 */
class MetricTree {
public:
    MetricTreeNode* root;                    ///< Root node of the metric tree
    int id_count = 0;                       ///< Counter for generating unique node IDs
    ThreadPoolWrapper thread_pool_wrapper;  ///< Thread pool for parallel operations
    
    /**
     * @brief Default constructor - builds tree from database
     * 
     * Automatically loads all trajectory IDs from the database and constructs
     * a balanced metric tree. This is the most common constructor for new trees.
     */
    MetricTree();
    
    /**
     * @brief Constructor with existing root node
     * 
     * Creates a metric tree with a pre-constructed root node, typically used
     * for deserialization or when building custom tree structures.
     * 
     * @param root Pointer to the root node of an existing tree
     */
    MetricTree(MetricTreeNode* root);
    
    /**
     * @brief Destructor - cleans up all tree nodes
     * 
     * Recursively deletes all nodes in the tree to prevent memory leaks.
     * Called automatically when the MetricTree object goes out of scope.
     */
    ~MetricTree();

    // ============================================================================
    // Tree Management Operations
    // ============================================================================
    
    /**
     * @brief Recursively delete entire subtree
     * 
     * Performs post-order traversal to safely delete all nodes in a subtree.
     * Handles both leaf and internal nodes appropriately.
     * 
     * @param node Root of subtree to delete (can be nullptr)
     */
    void deleteTree(MetricTreeNode* node);
    
    /**
     * @brief Collect all nodes into a map for serialization
     * 
     * Performs tree traversal to gather all nodes indexed by their ID.
     * Used as an intermediate step in tree serialization.
     * 
     * @param node Root of subtree to collect
     * @param node_map Output map to store node pointers by ID
     */
    void saveAllNodes(MetricTreeNode* node, std::unordered_map<int, MetricTreeNode*>& node_map) const;

    // ============================================================================
    // Serialization and Persistence
    // ============================================================================
    
    /**
     * @brief Serialize entire tree to JSON string
     * 
     * Converts the complete tree structure into a JSON string suitable for
     * storage or network transmission. Preserves all tree topology and data.
     * 
     * @return JSON string representation of the tree
     */
    std::string serialize_to_string() const;
    
    /**
     * @brief Serialize tree to map of JSON objects
     * 
     * Creates a map where each node is represented as a JSON object indexed
     * by node ID. This intermediate format facilitates tree reconstruction.
     * 
     * @return Map of node ID to JSON object
     */
    std::unordered_map<int, nlohmann::json> serialize_to_map() const;
    
    /**
     * @brief Deserialize tree from JSON string
     * 
     * Reconstructs a complete metric tree from its JSON string representation.
     * Handles node creation and linking to restore tree topology.
     * 
     * @param data JSON string containing serialized tree
     * @return Pointer to root node of reconstructed tree
     */
    static MetricTreeNode* deserialize_from_string(std::string data);
    
    /**
     * @brief Deserialize tree from JSON object map
     * 
     * Reconstructs tree structure from a map of JSON objects. Performs
     * two-phase construction: node creation followed by link resolution.
     * 
     * @param data Map of node ID to JSON object
     * @return Pointer to root node of reconstructed tree
     */
    static MetricTreeNode* deserialize_from_map(std::unordered_map<int, nlohmann::json>& data);

    // ============================================================================
    // Tree Construction and Validation
    // ============================================================================
    
    /**
     * @brief Validate tree structure integrity
     * 
     * Performs comprehensive validation of tree structure including:
     * - Node type consistency
     * - Non-null child pointers for internal nodes
     * - Non-empty trajectory lists for leaf nodes
     * - Proper tree connectivity
     * 
     * @param node Root of subtree to validate
     * @return True if tree structure is valid
     */
    bool checkTree(MetricTreeNode* node);
    
    /**
     * @brief Recursively build metric tree from trajectory IDs
     * 
     * Core tree construction algorithm that partitions trajectory sets around
     * pivot points to create a balanced tree. Uses the following strategy:
     * 
     * 1. Select pivot trajectory (typically median by distance)
     * 2. Sort trajectories by distance to pivot
     * 3. Split at median distance to create balanced partitions
     * 4. Recursively construct left and right subtrees
     * 5. Create leaf nodes when partition size falls below threshold
     * 
     * @param ids Vector of trajectory IDs to organize into tree
     * @param layer Current depth in tree (for optimization decisions)
     * @return Pointer to root of constructed subtree
     */
    MetricTreeNode* buildTree(std::vector<int>& ids, int layer = 0);

    // ============================================================================
    // Query Operations
    // ============================================================================
    
    /**
     * @brief Perform k-nearest neighbor search with approximation
     * 
     * Finds the k most similar trajectories to the query using an approximate
     * algorithm that provides bounded error guarantees. The algorithm uses:
     * 
     * 1. Priority queue to explore most promising nodes first
     * 2. Distance bounds to prune unpromising subtrees
     * 3. Approximation ratio to trade accuracy for speed
     * 4. Temporal filtering to restrict search to time window
     * 
     * Approximation guarantee: All returned distances are within
     * approximate_ratio * optimal_distance of the true k-NN distances.
     * 
     * @param query Query trajectory to find neighbors for
     * @param k Number of nearest neighbors to return
     * @param approximate_ratio Quality vs speed tradeoff (≥1.0)
     * @param min_time Minimum time bound for trajectory filtering
     * @param max_time Maximum time bound for trajectory filtering  
     * @param db_time Output parameter for database access time measurement
     * @return Vector of k nearest trajectories with distances
     * @throws StringException if approximate_ratio < 1.0
     */
    std::vector<std::pair<float, Trajectory>> kNearestNeighbors(
        Trajectory& query, int k, float approximate_ratio, 
        float min_time, float max_time, double& db_time) const;
};

// ============================================================================
// Utility Functions - Tree Operations and Serialization
// ============================================================================

/**
 * @brief Compare two metric trees for structural equality
 * 
 * Performs deep comparison of tree structures including node types,
 * data contents, and tree topology. Used for testing and validation.
 * 
 * @param node1 Root of first tree
 * @param node2 Root of second tree
 * @return True if trees are structurally identical
 */
bool compareTrees(MetricTreeNode* node1, MetricTreeNode* node2);

// ============================================================================
// External Dependencies - Forward Declarations
// ============================================================================

/**
 * @brief Load trajectory from database by ID
 * 
 * External function that retrieves trajectory data from the database.
 * Implementation depends on the specific database backend used.
 * 
 * @param id Trajectory identifier
 * @return Complete trajectory object with all points
 */
 Trajectory load_tracj(int id){
    // return all_traj[id];
    auto& tm = TrajectoryManager::getInstance();
    return tm.loadTrajectoryFromDatabase(id);
}

/**
 * @brief Calculate distance between two trajectories
 * 
 * External function that computes similarity distance between trajectories.
 * Typically uses DTW (Dynamic Time Warping) for robust comparison.
 * 
 * @param t1 First trajectory
 * @param t2 Second trajectory
 * @return Distance value (lower = more similar)
 */
float trajectoryDistance(const Trajectory& t1, const Trajectory& t2);

#endif // METRIC_H
