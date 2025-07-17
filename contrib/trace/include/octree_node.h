#ifndef OCTREENODE_H
#define OCTREENODE_H

#include <string>
#include <vector>
#include <cstddef>
#include <unordered_map>
#include "spatiotemporal_data.h"

// Forward declaration
class OctreeNodeManager;

struct OctreeNode {
    Bounds bound;                              // Bounding box of the node
    std::vector<int> points;                   // Indices of Points contained in this node
    OctreeNode* children[8] = {nullptr};       // Pointers to octants
    int id;                                    // Node ID
    int64_t pointCount;                        // Number of points in this node
    int level;                                 // Level of the node in the octree
    bool is_leaf;                              // Flag indicating whether this node is a leaf

    OctreeNode();                              // Default constructor
    OctreeNode(const Bounds& b, int level = 0); // Parameterized constructor
    ~OctreeNode();                             // Destructor

    bool isLeaf() const;                       // Check if the node is a leaf
    void convertToLeaf();                      // Convert to leaf node

    // Static functions
    static void saveNode(const OctreeNode* node, std::ofstream& out); // Save a node to a file
    static OctreeNode* loadNode(std::ifstream& in);                   // Load a node from a file
    static void saveOctree(const OctreeNode* root, const std::string& filename); // Save octree to a file
    static OctreeNode* loadOctree(const std::string& filename);      // Load octree from a file
};



 void AddIdForOctreeNode(OctreeNode* node,long long &id_count);

 OctreeNode* copyOctreeNode(const OctreeNode* source);

 bool compareOctrees(const OctreeNode* node1, const OctreeNode* node2) ;


// Function to recursively collect all leaf nodes
 void getLeafNodes(OctreeNode* node, std::vector< OctreeNode*>& leaves);

// Function to recursively collect all nodes
 void getAllNodes(OctreeNode* node, std::vector< OctreeNode*>& nodes);

// Function to encode octree nodes with sequential IDs
 void encode_Octree(OctreeNode* node);

 struct DBOctreeNode {
    SpatialBounds bound;                             
    int children[8] = {-1,-1,-1,-1,-1,-1,-1,-1,};     
    int id;//用于寻址
    int oc_id;
    int kd_id;
    bool is_leaf;
};

// DBOctreeNode point_query_octree(SpatialPoint &point,std::vector<DBOctreeNode> &nodes);

 void split_data(int id,std::vector<DBOctreeNode> &nodes, std::vector<SpatioTemporalData> &data,std::unordered_map<int,std::vector<SpatioTemporalData>> &result);
 std::vector<DBOctreeNode> convertOctreeToDB(OctreeNode* root,int chunk_id=-1);
 bool validateConversion(const OctreeNode* originalNode, const std::vector<DBOctreeNode>& dbNodes, int dbIndex);

 inline DBOctreeNode point_query_on_octree(const std::vector<DBOctreeNode> &chunk_tree, const SpatioTemporalData &point)
 {
     DBOctreeNode node = chunk_tree[0];
     while (!node.is_leaf)
     {
         const auto center = node.bound.getCenter();
         int son_id = 0;
         if (point.x < center.x)
         {
         }
         else
         {
             son_id |= 1;
         }
         if (point.y < center.y)
         {
         }
         else
         {
             son_id |= 2;
         }
         if (point.z < center.z)
         {
         }
         else
         {
             son_id |= 4;
         }
         node = chunk_tree[node.children[son_id]];
     }
     return node;
 }

 class OctreePointQuery
 {
 public:
     std::vector<DBOctreeNode> chunk_tree;
     OctreePointQuery(std::vector<DBOctreeNode> &dbNodes) : chunk_tree(dbNodes) {}
     DBOctreeNode point_query_octree(const SpatioTemporalData &point) const
     {
         return point_query_on_octree(chunk_tree, point);
     }
 };

 #endif // OCTREENODE_H
