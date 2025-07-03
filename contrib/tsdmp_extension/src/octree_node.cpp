#include "../include/octree_node.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <cmath>

// Default constructor
OctreeNode::OctreeNode() : id(-1), pointCount(0), level(0), is_leaf(false) {}

// Parameterized constructor
OctreeNode::OctreeNode(const Bounds& b, int level) : bound(b), id(-1), pointCount(0), level(level), is_leaf(false) {
    for (int i = 0; i < 8; ++i) {
        children[i] = nullptr;
    }
}

// Destructor
OctreeNode::~OctreeNode() {
    for (auto& child : children) {
        if (child) {
            delete child;
            child = nullptr;
        }
    }
}

// Check if the node is a leaf
bool OctreeNode::isLeaf() const {
    return is_leaf;
}

// Convert the node to a leaf
void OctreeNode::convertToLeaf() {
    is_leaf = true;
    for (int i = 0; i < 8; i++) {
        if (children[i]) {
            delete children[i];
            children[i] = nullptr;
        }
    }
}

// Static function: Save the node to a file
void OctreeNode::saveNode(const OctreeNode* node, std::ofstream& out) {
    if (!node) return;
    if (node->id < 0) {
        puts("node->id < 0");
        throw std::runtime_error("node->id < 0");
    }

    // Save node basic information
    out.write(reinterpret_cast<const char*>(&node->id), sizeof(node->id));
    out.write(reinterpret_cast<const char*>(&node->pointCount), sizeof(node->pointCount));
    out.write(reinterpret_cast<const char*>(&node->level), sizeof(node->level));
    out.write(reinterpret_cast<const char*>(&node->is_leaf), sizeof(node->is_leaf));

    // Save bounding box
    out.write(reinterpret_cast<const char*>(&node->bound), sizeof(node->bound));

    // Save points indices
    int points_size = node->points.size();
    out.write(reinterpret_cast<const char*>(&points_size), sizeof(points_size));
    out.write(reinterpret_cast<const char*>(node->points.data()), points_size * sizeof(int));

    // Recursively save children
    for (int i = 0; i < 8; i++) {
        bool hasChild = (node->children[i] != nullptr);
        out.write(reinterpret_cast<const char*>(&hasChild), sizeof(hasChild));
        if (hasChild) {
            saveNode(node->children[i], out);
        }
    }
}

// Static function: Load the node from a file
OctreeNode* OctreeNode::loadNode(std::ifstream& in) {
    OctreeNode* node = new OctreeNode();

    // Load node basic information
    in.read(reinterpret_cast<char*>(&node->id), sizeof(node->id));
    in.read(reinterpret_cast<char*>(&node->pointCount), sizeof(node->pointCount));
    in.read(reinterpret_cast<char*>(&node->level), sizeof(node->level));
    in.read(reinterpret_cast<char*>(&node->is_leaf), sizeof(node->is_leaf));

    // Load bounding box
    in.read(reinterpret_cast<char*>(&node->bound), sizeof(node->bound));

    // Load points indices
    int points_size;
    in.read(reinterpret_cast<char*>(&points_size), sizeof(points_size));
    node->points.resize(points_size);
    in.read(reinterpret_cast<char*>(node->points.data()), points_size * sizeof(int));

    // Recursively load children
    for (int i = 0; i < 8; i++) {
        bool hasChild;
        in.read(reinterpret_cast<char*>(&hasChild), sizeof(hasChild));
        if (hasChild) {
            node->children[i] = loadNode(in);
        }
    }

    return node;
}

// Static function: Save the octree to a file
void OctreeNode::saveOctree(const OctreeNode* root, const std::string& filename) {
    std::ofstream out(filename, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Failed to open file for saving: " << filename << std::endl;
        return;
    }
    saveNode(root, out);
    out.close();
}

// Static function: Load the octree from a file
OctreeNode* OctreeNode::loadOctree(const std::string& filename) {
    std::ifstream in(filename, std::ios::binary);
    if (!in.is_open()) {
        std::cerr << "Failed to open file for loading: " << filename << std::endl;
        return nullptr;
    }
    OctreeNode* root = loadNode(in);
    in.close();
    return root;
}


 void AddIdForOctreeNode(OctreeNode* node,long long &id_count) {
    if (!node) return;
    if(node->is_leaf && node->id < 0){
        puts("叶子结点的id小于零-理论上不存在");
    }
    if(node->id < 0){
        node->id = id_count++;
    }

    // Recursively copy children
    for (int i = 0; i < 8; ++i) {
        AddIdForOctreeNode(node->children[i],id_count);
    }
}

 OctreeNode* copyOctreeNode(const OctreeNode* source) {
    if (!source) return nullptr; // Handle null source

    // Allocate new node and copy data
    OctreeNode* newNode = new OctreeNode();
    newNode->bound = source->bound;
    newNode->points = source->points;
    newNode->id = source->id;
    newNode->pointCount = source->pointCount;
    newNode->level = source->level;
    newNode->is_leaf = source->is_leaf;

    // Recursively copy children
    for (int i = 0; i < 8; ++i) {
        newNode->children[i] = copyOctreeNode(source->children[i]);
    }

    return newNode;
}

 bool compareOctrees(const OctreeNode* node1, const OctreeNode* node2) {
    // 如果两个节点都是空的，则它们相同
    if (!node1 && !node2) return true;

    // 如果只有一个节点为空，则它们不同
    if (!node1 || !node2) return false;

    // 比较当前节点的 id 和 bound
    if (node1->id != node2->id || node1->bound != node2->bound) return false;

    // 递归比较每个子节点
    for (int i = 0; i < 8; i++) {
        if (!compareOctrees(node1->children[i], node2->children[i])) {
            return false;
        }
    }

    // 如果所有子节点都相同，则这两个节点相同
    return true;
}

// Function to recursively collect all leaf nodes
void getLeafNodes(OctreeNode* node, std::vector< OctreeNode*>& leaves) {
    if (!node) return;
    if (node->isLeaf()) {
        leaves.push_back(node); // Add current node if it is a leaf
    } else {
        for (int i = 0; i < 8; ++i) {
            getLeafNodes(node->children[i], leaves); // Recursively add children
        }
    }
}
