/*********************                                                        */
/*! \file slicer.h
 ** \verbatim
 ** Original author: lianah
 ** Major contributors: none
 ** Minor contributors (to current version): none
 ** This file is part of the CVC4 prototype.
 ** Copyright (c) 2009, 2010, 2011  The Analysis of Computer Systems Group (ACSys)
 ** Courant Institute of Mathematical Sciences
 ** New York University
 ** See the file COPYING in the top-level source directory for licensing
 ** information.\endverbatim
 **
 ** \brief Bitvector theory.
 **
 ** Bitvector theory.
 **/

#include "theory/bv/slicer.h"
#include "util/integer.h"
#include "util/utility.h"
#include "theory/bv/theory_bv_utils.h"
#include "theory/rewriter.h"

using namespace CVC4;
using namespace CVC4::theory;
using namespace CVC4::theory::bv;
using namespace std; 

void Base::decomposeNode(TNode node, std::vector<Node>& decomp) {
  Debug("bv-slicer") << "Base::decomposeNode " << node << "\n with base" << debugPrint() << endl;
  Index low = 0;
  Index high = utils::getSize(node) - 1; 
  if (node.getKind() == kind::BITVECTOR_EXTRACT) {
    low = utils::getExtractLow(node);
    high = utils::getExtractHigh(node);
    node = node[0];
  }
  Index index = low; 
  for (Index i = low; i <= high; ++i) {
    if (isCutPoint(i)) {
      // make sure the extract is pushed down before concat
      Node slice = Rewriter::rewrite(utils::mkExtract(node, i, index));
      index = i + 1;
      decomp.push_back(slice);
      Debug("bv-slicer") << slice <<" "; 
    }
  }
  Debug("bv-slicer") << endl; 
}

/** 
 * Adds a cutPoint at Index i and splits the corresponding Splinter
 * 
 * @param i the index where the cut point will be introduced
 * @param sp the splinter pointer corresponding to the splinter to be sliced
 * @param low_splinter the resulting bottom part of the splinter
 * @param top_splinter the resulting top part of the splinter
 */
void Slice::split(Index i, SplinterPointer& sp, Splinter*& low_splinter, Splinter*& top_splinter) {
  Debug("bv-slicer") << "Slice::split " << this->debugPrint() << "\n"; 
  Assert (!d_base.isCutPoint(i));
  d_base.sliceAt(i);
  
  Splinter* s = NULL;
  Slice::const_iterator it = begin();
  bool lt, gt; 
  for (; it != end(); ++it) {
    lt = (it->second)->getHigh() >= i;
    gt = (it->second)->getLow() <= i;
    if (gt && lt) {
      s = it->second;
      break; 
    }
  }
  
  Assert (s != NULL);

  sp = s->getPointer();
  Index low = s->getLow();
  Index high = s->getHigh();
  // creating the two splinter fragments 
  low_splinter = new Splinter(i, low);
  top_splinter = new Splinter(high, i+1);
  
  addSplinter(low, low_splinter);
  addSplinter(i+1, top_splinter);
  Debug("bv-slicer") << "          to " << this->debugPrint() << "\n"; 
} 
 
void Slice::addSplinter(Index i, Splinter* sp) {
  Assert (i == sp->getLow() && sp->getHigh() < d_bitwidth);

  if (i != 0) {
    d_base.sliceAt(i - 1);
  }
  d_base.debugPrint(); 
  // free the memory associated with the previous splinter
  if (d_splinters.find(i) != d_splinters.end()) {
    delete d_splinters[i];
  }
  d_splinters[i] = sp;
}

std::string Slice::debugPrint() {
  std::ostringstream os;
  os << d_base.debugPrint(); 
  os << "{ ";
  for (Slice::const_iterator it = begin(); it != end(); ++it) {
    Splinter* s = (*it).second;
    os << "[" << s->getLow() << ":" << s->getHigh() <<"]"; 
    Assert ((*it).first == s->getLow());
    Assert (s->getLow() == 0 || d_base.isCutPoint(s->getLow() - 1)); 
    SplinterPointer sp = s->getPointer(); 
    if (s->getPointer() != Undefined) {
      os << "->" << sp.debugPrint(); 
    }
    os << " "; 
  }
  os << "}";
  return os.str(); 
}

void SliceBlock::computeBlockBase(std::vector<RootId>& queue)  {
  Debug("bv-slicer") << "SliceBlock::computeBlockBase for block" << d_rootId << endl;
  Debug("bv-slicer") << this->debugPrint() << endl; 

  // at this point d_base has all the cut points in the individual slices
  for (unsigned row = 0; row < d_block.size(); ++row) {
    Slice* slice = d_block[row];
    const Base base = slice->getBase();
    Base new_cut_points = base.diffCutPoints(d_base);
    // use the cut points from the base to split the current slice
    for (unsigned i = 0; i < d_bitwidth; ++i) {
      if (new_cut_points.isCutPoint(i) && i != d_bitwidth - 1) {
        Debug("bv-slicer") << "    adding cut point at " << i << " for row " << row << endl; 
        // split this slice (this updates the slice's base)
        Splinter* bottom, *top = NULL;
        SplinterPointer sp;
        slice->split(i, sp, bottom, top);
        Assert (bottom != NULL && top != NULL); 
        Assert (i >= bottom->getLow()); 
        if (sp != Undefined) {
          unsigned delta = i - bottom->getLow();   
          // if we do need to split something else split it
          Debug("bv-slicer") <<"    must split " << sp.debugPrint(); 
          Slice* other_slice = d_slicer->getSlice(sp);
          Splinter* s = d_slicer->getSplinter(sp);
          Index cutPoint = s->getLow() + delta; 
          Splinter* new_bottom = new Splinter(cutPoint, s->getLow());
          Splinter* new_top = new Splinter(s->getHigh(), cutPoint + 1);
          new_bottom->setPointer(SplinterPointer(d_rootId, row, bottom->getLow()));
          new_top->setPointer(SplinterPointer(d_rootId, row, top->getLow()));

          other_slice->addSplinter(new_bottom->getLow(), new_bottom);
          other_slice->addSplinter(new_top->getLow(), new_top); 
          
          bottom->setPointer(SplinterPointer(sp.term, sp.row, new_bottom->getLow()));
          top->setPointer(SplinterPointer(sp.term, sp.row, new_top->getLow()));
          // update base for block
          d_slicer->getSliceBlock(sp)->sliceBaseAt(cutPoint);
          // add to queue of blocks that have changed base
          Debug("bv-slicer") << "    adding block to queue: " << sp.term << endl; 
          queue.push_back(sp.term); 
        }
      }
    }
  }

  Debug("bv-slicer") << "base computed: " << d_rootId << endl;
  Debug("bv-slicer") << this->debugPrint() << endl;
  Debug("bv-slicer") << "SliceBlock::computeBlockBase done. \n"; 

}

std::string SliceBlock::debugPrint() {
  std::ostringstream os;
  os << "Width " << d_bitwidth << endl; 
  os << "Base " << d_base.debugPrint() << endl;
  for (SliceBlock::const_iterator it = begin(); it!= end(); ++it) {
    os << (*it)->debugPrint() << endl;
  }
  return os.str(); 
}

Slicer::Slicer()
  : d_simpleEqualities(),
    d_roots(),
    d_numRoots(0),
    d_nodeRootMap(),
    d_rootBlocks(),
    d_coreTermCache()
{}

RootId Slicer::makeRoot(TNode n)  {
  Assert (n.getKind() != kind::BITVECTOR_EXTRACT && n.getKind() != kind::BITVECTOR_CONCAT);
  if (d_nodeRootMap.find(n) != d_nodeRootMap.end()) {
    return d_nodeRootMap[n];
  }
  RootId id = d_roots.size();
  d_nodeRootMap[n] = id; 
  d_roots.push_back(n); 
  d_rootBlocks.push_back(new SliceBlock(id, utils::getSize(n), this));
  Assert (d_roots.size() == d_rootBlocks.size());
  
  Debug("bv-slicer") << "Slicer::makeRoot " << n << " -> " << id << endl;
  return id; 
}

void Slicer::splitEqualities(TNode node, std::vector<Node>& equalities) {
  Assert (node.getKind() == kind::EQUAL);
  TNode t1 = node[0];
  TNode t2 = node[1];

  uint32_t width = utils::getSize(t1); 
  
  Base base1(width); 
  if (t1.getKind() == kind::BITVECTOR_CONCAT) {
    int size = -1;
    // no need to count the last child since the end cut point is implicit 
    for (int i = t1.getNumChildren() - 1; i >= 1 ; --i) {
      size = size + utils::getSize(t1[i]);
      base1.sliceAt(size); 
    }
  }

  Base base2(width); 
  if (t2.getKind() == kind::BITVECTOR_CONCAT) {
    unsigned size = -1; 
    for (int i = t2.getNumChildren() - 1; i >= 1; --i) {
      size = size + utils::getSize(t2[i]);
      base2.sliceAt(size); 
    }
  }

  base1.sliceWith(base2); 
  if (base1 != Base(width)) {
    // we split the equalities according to the base
    int last = 0; 
    for (unsigned i = 1; i < utils::getSize(t1); ++i) {
      if (base1.isCutPoint(i)) {
        Node extract1 = Rewriter::rewrite(utils::mkExtract(t1, i, last));
        Node extract2 = Rewriter::rewrite(utils::mkExtract(t2, i, last));
        last = i + 1;
        Assert (utils::getSize(extract1) == utils::getSize(extract2)); 
        equalities.push_back(utils::mkNode(kind::EQUAL, extract1, extract2)); 
      }
    }
  } else {
    // just return same equality
    equalities.push_back(node);
  }
} 
 
void Slicer::processEquality(TNode node) {
  Assert (node.getKind() == kind::EQUAL);
  Debug("bv-slicer") << "theory::bv::Slicer::processEquality " << node << endl; 
  std::vector<Node> equalities;
  splitEqualities(node, equalities); 
  for (unsigned i = 0; i < equalities.size(); ++i) {
    Debug("bv-slicer") << "    splitEqualities " << node << endl;
    registerSimpleEquality(equalities[i]); 
    d_simpleEqualities.push_back(equalities[i]);
  }
}

void Slicer::registerSimpleEquality(TNode eq) {
  Assert (eq.getKind() == kind::EQUAL);
  Debug("bv-slicer") << "theory::bv::Slicer::registerSimpleEquality " << eq << endl;  
  TNode a = eq[0];
  TNode b = eq[1];
 
  RootId id_a = registerTerm(a);
  RootId id_b = registerTerm(b);
  
  unsigned low_a = 0; 
  unsigned low_b = 0; 

  if (a.getKind() == kind::BITVECTOR_EXTRACT) {
    low_a  = utils::getExtractLow(a);
  }
  
  if (b.getKind() == kind::BITVECTOR_EXTRACT) {
    low_b  = utils::getExtractLow(b);
  }

  if (id_a == id_b) {
    // we are in the special case a[i0:j0] = a[i1:j1]
    Index high_a = utils::getExtractHigh(a);
    Index high_b = utils::getExtractHigh(b);
    
    unsigned intersection_low = std::max(low_a, low_b);
    unsigned intersection_high = std::min(high_a, high_b);
    if (intersection_low <= intersection_high) {
      // if the two extracts intersect 
      unsigned intersection_size = intersection_high - intersection_low + 1;
      // gcd between overlapping area and difference
      unsigned diff = low_a > low_b ? low_a - low_b  : low_b - low_a; 
      unsigned granularity = gcd(intersection_size, diff);
      SliceBlock* block_a = d_rootBlocks[id_a];
      Assert (a.getKind() == kind::BITVECTOR_EXTRACT);
      unsigned size = utils::getSize(a[0]);
      
      Slice* slice = new Slice(size);
      unsigned low = low_a > low_b ? low_b : low_a;
      unsigned high = high_a > high_b ? high_a : high_b;
      Splinter* prev_splinter = NULL;
      // the row the new slice will be in 
      unsigned block_row = block_a->getSize(); 
      for (unsigned i = low; i < high; i+=granularity) {
        Splinter* s = new Splinter(i+ granularity-1, i);
        slice->addSplinter(i, s);
        // update splinter pointers to reflect entailed equalities 
        if (prev_splinter!= NULL) {
          // the previous splinter will be equal to the current 
          prev_splinter->setPointer(SplinterPointer(id_a, block_row, i));
        }
        prev_splinter = s; 
      }
      // make sure to splinters for the extremities
      if (low!= 0) {
        Splinter* s = new Splinter(low -1 , 0);
        slice->addSplinter(0, s); 
      }
      if (high + granularity <= size) {
        Splinter* s = new Splinter(size - 1, high + 1);
        slice->addSplinter(high+1, s); 
      }
      block_a->addSlice(slice);
      d_rootBlocks[id_a] = block_a; 
      Debug("bv-slicer") << "     updated block" << id_a << " to " << endl;
      Debug("bv-slicer") << block_a->debugPrint() << endl;
      return; 
    }
  }
  
  Slice* slice_a = makeSlice(a);
  Slice* slice_b = makeSlice(b); 

  SliceBlock* block_a = d_rootBlocks[id_a];
  SliceBlock* block_b = d_rootBlocks[id_b];

  uint32_t row_a = block_a->addSlice(slice_a);
  uint32_t row_b = block_b->addSlice(slice_b); 

  SplinterPointer sp_a = SplinterPointer(id_a, row_a, low_a);
  SplinterPointer sp_b = SplinterPointer(id_b, row_b, low_b); 

  slice_a->getSplinter(low_a)->setPointer(sp_b);
  slice_b->getSplinter(low_b)->setPointer(sp_a);
  Debug("bv-slicer") << "     updated block" << id_a << " to " << endl;
  Debug("bv-slicer") << block_a->debugPrint() << endl;
  Debug("bv-slicer") << "     updated block" <<id_b << " to " << endl;
  Debug("bv-slicer") << block_b->debugPrint() << endl;
}

Slice* Slicer::makeSlice(TNode node) {
  //Assert (d_sliceSet.find(node) == d_sliceSet.end());
  
  Index bitwidth = utils::getSize(node); 
  Index low = 0;
  Index high = bitwidth -1;
  if (node.getKind() == kind::BITVECTOR_EXTRACT) {
    low  = utils::getExtractLow(node);
    high = utils::getExtractHigh(node);
    bitwidth = utils::getSize(node[0]); 
  }
  Splinter* splinter = new Splinter(high, low);
  Slice* slice = new Slice(bitwidth);
  slice->addSplinter(low, splinter);
  if (low != 0) {
    Splinter* bottom_splinter = new Splinter(low-1, 0);
    slice->addSplinter(0, bottom_splinter); 
  }
  if (high != bitwidth - 1) {
    Splinter* top_splinter = new Splinter(bitwidth - 1, high + 1);
    slice->addSplinter(high+1, top_splinter); 
  }
  return slice; 
}


RootId Slicer::registerTerm(TNode node) {
  if (node.getKind() == kind::BITVECTOR_EXTRACT ) {
    node = node[0];
    Assert (isRootTerm(node)); 
  }
  // setting up the data-structures for the root term
  RootId id = makeRoot(node);
  return id; 
}

bool Slicer::isCoreTerm(TNode node) {
  if (d_coreTermCache.find(node) == d_coreTermCache.end()) {
    Kind kind = node.getKind(); 
    if (kind != kind::BITVECTOR_EXTRACT &&
        kind != kind::BITVECTOR_CONCAT &&
        kind != kind::EQUAL && kind != kind::NOT) {
      d_coreTermCache[node] = false;
      return false; 
    } else {
      // we need to recursively check whether the term is a root term or not
      bool isCore = true;
      for (unsigned i = 0; i < node.getNumChildren(); ++i) {
        isCore = isCore && isCoreTerm(node[i]); 
      }
      d_coreTermCache[node] = isCore;
      return isCore; 
    }
  }
  return d_coreTermCache[node]; 
}

bool Slicer::isRootTerm(TNode node) {
  Kind kind = node.getKind();
  return kind != kind::BITVECTOR_EXTRACT && kind != kind::BITVECTOR_CONCAT;
}

// Base Slicer::getBase(TNode node) {
//   Assert (d_bases.find(node) != d_bases.end());
//   return d_bases[node]; 
// }

// void Slicer::updateBase(TNode node, const Base& base) {
//   Assert (d_bases.find(node) != d_bases.end());
//   d_bases[node] = d_bases[node].bitwiseOr(base); 
// }


void Slicer::computeCoarsestBase() {
  Debug("bv-slicer") << "theory::bv::Slicer::computeCoarsestBase " << endl; 
  std::vector<RootId> queue;
  for (unsigned i = 0; i < d_rootBlocks.size(); ++i) {
    SliceBlock* block = d_rootBlocks[i];
    block->computeBlockBase(queue);
  }

  Debug("bv-slicer") << " processing queue of size " << queue.size() << endl; 
  while (!queue.empty()) {
    // process split candidate
    RootId current = queue.back();
    queue.pop_back();
    SliceBlock* block = d_rootBlocks[current];
    block->computeBlockBase(queue); 
  }
  debugCheckBase(); 
}

void Slicer::getExtractOverBase(TNode node, std::vector<Node>& decomp) {
  
}

void Slicer::getBaseDecomposition(TNode node, std::vector<Node>& decomp) {
  if (node.getKind() == kind::BITVECTOR_CONCAT) {
    for (unsigned i = 0; i < node.getNumChildren(); ++i) {
      TNode root = node[i].getKind() == kind::BITVECTOR_EXTRACT ? node[i][0] : node[i]; 
      Assert (isRootTerm(root)); 
      Base base = getSliceBlock(getRootId(root))->getBase();
      std::vector<Node> decomp_i; 
      base.decomposeNode(node[i], decomp_i);
      // TODO: add check for extract and finish this code here
      
      if (node[i].getKind() == kind::BITVECTOR_EXTRACT) {
        unsigned low = utils::getExtractLow(node[i]); 
        unsigned high = utils::getExtractHigh(node[i]); 
        unsigned size = 0;
        bool add = false;
        for (int i = decomp_i.size() - 1; i >= 0; --i) {
          int size_i = utils::getSize(decomp_i[i]);
          if (low > size + size_i - 1 && !add) {
            add = true; 
            Node first = utils::mkExtract(decomp_i[i], size + size_i -1, low - size);
            decomp.push_back(first); 
          }
          if (high < size + size_i - 1 && add) {
            add = false;
            Node last = utils::mkExtract(decomp_i[i], size + size_i - 1, high - size);
            decomp.push_back(last); 
          }
          if(add) {
            decomp.push_back(decomp_i[i]); 
          }
          size += size_i; 
        }
      }
    }
  } else {
    TNode root = node.getKind() == kind::BITVECTOR_EXTRACT ? node[0] : node; 
    Assert (isRootTerm(root)); 
    Base base = getSliceBlock(getRootId(root))->getBase();
    base.decomposeNode(node, decomp);
  }
}

void Slicer::debugCheckBase() {
  // iterate through blocks and check that the block base is the same as each slice base
  for (unsigned i = 0; i < d_rootBlocks.size(); ++i) {
    SliceBlock* block = d_rootBlocks[i];
    Base block_base = block->getBase();
    SliceBlock::const_iterator it = block->begin();
    for (; it != block->end(); ++it) {
      Slice* slice = *it;
      Base diff_points = slice->getBase().diffCutPoints(block_base);
      Assert (diff_points == Base(block->getBitwidth()));
      Slice::const_iterator slice_it = slice->begin();
      for (; slice_it!= slice->end(); ++slice_it) {
        Splinter* splinter = (*slice_it).second;
        const SplinterPointer& sp = splinter->getPointer();
        if (sp != Undefined) {
          Splinter* other = getSplinter(sp);
          Assert (splinter->getBitwidth() == other->getBitwidth());
        }
      }
    }
  }
}






