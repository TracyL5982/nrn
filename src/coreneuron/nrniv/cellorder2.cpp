#include <stdio.h>
#include "coreneuron/nrniv/nrn_assert.h"
#include "coreneuron/nrniv/cellorder.h"

#include <vector>
#include <map>
#include <algorithm>

using namespace std;

class TNode {
  public:
  TNode(int ix);
  virtual ~TNode();
  TNode* parent;
  vector<TNode*> children;
  size_t mkhash();
  size_t hash;
  size_t treesize;
  size_t nodevec_index;
  size_t treenode_order;
  int nodeindex;
  int cellindex;
};

static bool tnode_earlier(TNode* a, TNode* b) {
  bool result = false;
  if (a->treesize < b->treesize) {// treesize dominates
    result = true;
  }else if (a->treesize == b->treesize) {
    if (a->hash < b->hash) { // if treesize same, keep identical trees together
      result = true;
    }else if (a->hash == b->hash) {
      result = a->nodeindex < b->nodeindex; // identical trees ordered by nodeindex
    }
  }
  return result;
}

static bool ptr_tnode_earlier(TNode* a, TNode* b) {
  return tnode_earlier(a, b);
}

TNode::TNode(int ix) {
  nodeindex = ix;
  cellindex = -1;
  hash = 0;
  treesize = 1;
  nodevec_index = -1;
  treenode_order = -1;
  parent = NULL;
  children.reserve(2);
}

TNode::~TNode() {} 

size_t TNode::mkhash() { // call on all nodes in leaf to root order
  // concept from http://stackoverflow.com/questions/20511347/a-good-hash-function-for-a-vector
  std::sort(children.begin(), children.end(), ptr_tnode_earlier);
  hash = children.size();
  treesize = 1;
  for (size_t i=0; i < children.size(); ++i) { // need sorted by child hash
    hash ^= children[i]->hash + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    treesize += children[i]->treesize;
  }
  return hash;  // hash of leaf nodes is 0
}

static void tree_analysis(int* parent, int nnode, int ncell, vector<TNode*>&);
static void node_interleave_order(int ncell, vector<TNode*>&);
static void admin(int ncell, vector<TNode*>& nodevec,
  int& nstride, int*& stride, int*& firstnode, int*& lastnode, int*& cellsize);
static void check(int ncell, vector<TNode*>&);
static void prtree(vector<TNode*>&);

// for cells with same size, keep identical trees together

// parent is (unpermuted)  nnode length vector of parent node indices.
// return a permutation (of length nnode) which orders cells of same
// size so that identical trees are grouped together.
// Note: cellorder[ncell:nnode] are the identify permutation.

int* node_order(int ncell, int nnode, int* parent,
  int& nstride, int*& stride, int*& firstnode, int*& lastnode, int*& cellsize
) {

  vector<TNode*> nodevec;
  if (0) prtree(nodevec); // avoid unused warning

  // nodevec[0:ncell] in increasing size, with identical trees together,
  // and otherwise nodeindex order
  tree_analysis(parent, nnode, ncell, nodevec);
  check(ncell, nodevec);

  // nodevec[ncell:nnode] cells are interleaved in nodevec[0:ncell] cell order
  node_interleave_order(ncell, nodevec);
  check(ncell, nodevec);

  for (int i=0; i < ncell; ++i) {
    TNode& nd = *nodevec[i];
    printf("%d size=%ld hash=%ld ix=%d\n", i, nd.treesize, nd.hash, nd.nodeindex);
  }

  // the permutation
  int* nodeorder = new int[nnode];
  for (int i=0; i < nnode; ++i) {
    TNode& nd = *nodevec[i];
    nodeorder[nd.nodeindex] = i;
  }

  // administrative statistics for gauss elimination
  admin(ncell, nodevec, nstride, stride, firstnode, lastnode, cellsize);

  for (size_t i =0; i < nodevec.size(); ++i) {
    delete nodevec[i];
  }

  return nodeorder;
}

void check(int ncell, vector<TNode*>& nodevec) {
  printf("check\n");
  size_t nnode = nodevec.size();
  for (size_t i=0; i < nnode; ++i) {
    nodevec[i]->nodevec_index = i;
  }
  for (int i=0; i < ncell; ++i) {
    nrn_assert(nodevec[i]->parent == NULL);
  }
  for (size_t i=ncell; i < nnode; ++i) {
    TNode& nd = *nodevec[i];
    if (nd.parent->nodevec_index >= nd.nodevec_index) {
      printf("error i=%ld nodevec_index=%ld parent=%ld\n", i, nd.nodevec_index, nd.parent->nodevec_index);
    }
    nrn_assert(nd.nodevec_index > nd.parent->nodevec_index);
  }
  
}

void prtree(vector<TNode*>& nodevec) {
  size_t nnode = nodevec.size();
  for (size_t i=0; i < nnode; ++i) {
    nodevec[i]->nodevec_index = i;
  }
  for (size_t i=0; i < nnode; ++i) {
    TNode& nd = *nodevec[i];
    printf("%ld p=%ld  c=%d o=%ld ix=%d pix=%d\n",
      i, nd.parent ? nd.parent->nodevec_index : -1,
      nd.cellindex, nd.treenode_order, nd.nodeindex,
      nd.parent ? nd.parent->nodeindex : -1);
  }
}

void tree_analysis(int* parent, int nnode, int ncell, vector<TNode*>& nodevec) {

//  vector<TNode*> nodevec;

  // create empty TNodes (knowing only their index)
  nodevec.reserve(nnode);
  for (int i=0; i < nnode; ++i) {
    nodevec.push_back(new TNode(i));
  }

  // determine the (sorted by hash) children of each node
  for (int i = nnode -1; i >= ncell; --i) {
    nodevec[i]->parent = nodevec[parent[i]];
    nodevec[i]->mkhash();
    nodevec[parent[i]]->children.push_back(nodevec[i]);
  }

  // determine hash of the cells
  for (int i = 0; i < ncell; ++i) {
    nodevec[i]->mkhash();
  }

  std::sort(nodevec.begin(), nodevec.begin() + ncell, tnode_earlier);

#if 0
  // group the cells by hash
  map<size_t, vector<TNode*> > groups;
  for (int i = 0; parent[i] == -1; ++i) {
    groups[nodevec[i]->hash].push_back(nodevec[i]);
  }
  printf("number of groups is %ld\n", groups.size());
#endif

}

#if 0
static bool contig_comp(TNode* a, TNode* b) {
  bool result = false;
  if (a->cellindex < b->cellindex) {
    result = true;
  }else if (a->cellindex == b->cellindex) {
    if (a->treenode_order < b->treenode_order) {
      result = true;
    }
  }
  return result;
}
#endif

static bool interleave_comp(TNode* a, TNode* b) {
  bool result = false;
  if (a->treenode_order < b->treenode_order) {
    result = true;
  }else if (a->treenode_order == b->treenode_order) {
    if (a->cellindex < b->cellindex) {
      result = true;
    }
  }
  return result;
}

// sort so nodevec[ncell:nnode] cell instances are contiguous. Keep the
// secondary ordering with respect to treenode_order so each cell is still a tree.

// sort so nodevec[ncell:nnode] cell instances are interleaved. Keep the
// secondary ordering with respect to treenode_order so each cell is still a tree.

void node_interleave_order(int ncell, vector<TNode*>& nodevec) {
  int* order = new int[ncell];
  for (int i=0; i < ncell; ++i) {
    nodevec[i]->cellindex = i;
    order[i] = 0;
    nodevec[i]->treenode_order = order[i]++;
  }
  for (size_t i=0; i < nodevec.size(); ++i) {
    TNode& nd = *nodevec[i];
    for (size_t j=0; j < nd.children.size(); ++j) {
      TNode* cnode = nd.children[j];
      cnode->cellindex = nd.cellindex;
      cnode->treenode_order = order[nd.cellindex]++;
    }
  }
  delete [] order;

//  std::sort(nodevec.begin() + ncell, nodevec.end(), contig_comp);
  std::sort(nodevec.begin() + ncell, nodevec.end(), interleave_comp);
  prtree(nodevec);

#if 0
  for (size_t i=0; i < nodevec.size(); ++i) {
    TNode& nd = *nodevec[i];
    printf("%ld cell=%d ix=%d\n",  i, nd.cellindex, nd.nodeindex);
  }
#endif
}

void admin(int ncell, vector<TNode*>& nodevec,
  int& nstride, int*& stride, int*& firstnode, int*& lastnode, int*& cellsize
){
  // firstnode[i] is the index of the first nonroot node of the cell
  // lastnode[i] is the index of the last node of the cell
  // cellsize is the number of nodes in the cell not counting root.
  // nstride is the maximum cell size (not counting root)
  // stride[i] is the number of cells with an ith node.
  firstnode = new int[ncell];
  lastnode = new int[ncell];
  cellsize = new int[ncell];

  for (int i = 0; i < ncell; ++i) {
    firstnode[i] = -1;
    lastnode[i] = -1;
    cellsize[i] = 0;
  }

  nstride = 0;
  for (size_t i = ncell; i < nodevec.size(); ++i) {
    TNode& nd = *nodevec[i];
    int ci = nd.cellindex;
    if (firstnode[ci] == -1) {
      firstnode[ci] = i;
    }
    lastnode[ci] = i;
    cellsize[ci] += 1;
    if (nstride < cellsize[ci]) {
      nstride = cellsize[ci];
    }
  }

  stride = new int[nstride + 1]; // in case back substitution accesses this
  for (int i=0; i <= nstride; ++i) {
    stride[i] = 0;
  }
  for (size_t i = ncell; i < nodevec.size(); ++i) {
    TNode& nd = *nodevec[i];
    stride[nd.treenode_order - 1] += 1; // -1 because treenode order includes root
  }
}

