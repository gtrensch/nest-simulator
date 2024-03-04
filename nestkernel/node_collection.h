/*
 *  node_collection.h
 *
 *  This file is part of NEST.
 *
 *  Copyright (C) 2004 The NEST Initiative
 *
 *  NEST is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  NEST is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with NEST.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef NODE_COLLECTION_H
#define NODE_COLLECTION_H

// C++ includes:
#include <ctime>
#include <memory>
#include <ostream>
#include <stdexcept> // out_of_range
#include <vector>

// Includes from libnestuil:
#include "lockptr.h"

// Includes from nestkernel:
#include "exceptions.h"
#include "nest_types.h"

// Includes from sli:
#include "arraydatum.h"
#include "dictdatum.h"

namespace nest
{
class Node;
class NodeCollection;
class NodeCollectionPrimitive;
class NodeCollectionComposite;
class NodeCollectionMetadata;

using NodeCollectionPTR = std::shared_ptr< NodeCollection >;
using NodeCollectionMetadataPTR = std::shared_ptr< NodeCollectionMetadata >;

/**
 * Class for Metadata attached to NodeCollection.
 *
 * NEST modules that want to add metadata to NodeCollections they
 * create need to implement their own concrete subclass.
 */
class NodeCollectionMetadata
{
public:
  NodeCollectionMetadata() = default;
  virtual ~NodeCollectionMetadata() = default;

  virtual void set_status( const DictionaryDatum&, bool ) = 0;

  /**
   * Retrieve status information sliced according to slicing of node collection
   *
   * @note If nullptr is passed for NodeCollection*, full metadata irrespective of any slicing is returned.
   */
  virtual void get_status( DictionaryDatum&, NodeCollection const* ) const = 0;

  virtual void set_first_node_id( size_t ) = 0;
  virtual size_t get_first_node_id() const = 0;
  virtual std::string get_type() const = 0;

  virtual bool operator==( const NodeCollectionMetadataPTR ) const = 0;
};

class NodeIDTriple
{
public:
  size_t node_id { 0 };
  size_t model_id { 0 };
  size_t lid { 0 }; // position with node collection
  NodeIDTriple() = default;
};

/**
 * Iterator for NodeCollections.
 *
 * This iterator can iterate over primitive and composite NodeCollections.
 * Behavior is determined by the constructor used to create the iterator.
 *
 * @note In addition to a raw pointer to either a primitive or composite node collection, which is used
 * for all actual work, the iterator also holds a NodeCollectionPTR to the NC it iterates over. This is solely
 * so that anonymous node collections at the SLI/Python level are not auto-destroyed when they go out
 * of scope while the iterator lives on.
 */
class nc_const_iterator
{
  friend class NodeCollectionPrimitive;
  friend class NodeCollectionComposite;

private:
  NodeCollectionPTR coll_ptr_; //!< pointer for keep node collection alive, see note
  size_t element_idx_;         //!< index into (current) primitive node collection
  size_t part_idx_;            //!< index into parts vector of composite collection
  size_t step_;                //!< step for skipping due to e.g. slicing

  //! Pointer to primitive collection to iterate over.  Zero if iterator is for composite collection.
  NodeCollectionPrimitive const* const primitive_collection_;

  //! Pointer to composite collection to iterate over. Zero if iterator is for primitive collection.
  NodeCollectionComposite const* const composite_collection_;

  /**
   * Create safe iterator for NodeCollectionPrimitive.
   *
   * @param collection_ptr smart pointer to collection to keep collection alive
   * @param collection  Collection to iterate over
   * @param offset  Index of collection element iterator points to
   * @param step    Step for skipping due to e.g. slicing
   */
  explicit nc_const_iterator( NodeCollectionPTR collection_ptr,
    const NodeCollectionPrimitive& collection,
    size_t offset,
    size_t step = 1 );

  /**
   * Create safe iterator for NodeCollectionComposite.
   *
   * @param collection_ptr smart pointer to collection to keep collection alive
   * @param collection  Collection to iterate over
   * @param part    Index of part of collection iterator points to
   * @param offset  Index of element in NC part that iterator points to
   * @param step    Step for skipping due to e.g. slicing
   */
  explicit nc_const_iterator( NodeCollectionPTR collection_ptr,
    const NodeCollectionComposite& collection,
    size_t part,
    size_t offset,
    size_t step = 1 );

  /**
   * Conditionally update element_idx and part_idx for composite NodeCollections
   */
  void composite_update_indices_();

public:
  using iterator_category = std::forward_iterator_tag;
  using difference_type = long;
  using value_type = NodeIDTriple;
  using pointer = NodeIDTriple*;
  using reference = NodeIDTriple&;

  nc_const_iterator( const nc_const_iterator& nci ) = default;
  void get_current_part_offset( size_t&, size_t& ) const;

  NodeIDTriple operator*() const;
  bool operator==( const nc_const_iterator& rhs ) const;
  bool operator!=( const nc_const_iterator& rhs ) const;
  bool operator<( const nc_const_iterator& rhs ) const;
  bool operator<=( const nc_const_iterator& rhs ) const;

  nc_const_iterator& operator++();
  nc_const_iterator operator++( int ); // postfix
  nc_const_iterator& operator+=( const size_t );
  nc_const_iterator operator+( const size_t ) const;

  size_t get_step_size() const; //!< step size of iterator in number of elements

  void print_me( std::ostream& ) const;
};

/**
 * Superclass for NodeCollections.
 *
 * The superclass acts as an interface to the primitive and composite
 * NodeCollection types. It contains methods, mostly virtual, for the subclasses,
 * and also create()-methods to be interfaced externally.
 *
 * The superclass also contains handling of the fingerprint, a unique identity
 * the NodeCollection gets from the kernel on creation, which ensures that the
 * NodeCollection is not used after the kernel is reset.
 */
class NodeCollection
{
  friend class nc_const_iterator;

public:
  using const_iterator = nc_const_iterator;

  /**
   * Initializer gets current fingerprint from the kernel.
   */
  NodeCollection();

  virtual ~NodeCollection() = default;

  /**
   * Create a NodeCollection from a vector of node IDs.
   *
   * Results in a primitive if the
   * node IDs are homogeneous and contiguous, or a composite otherwise.
   *
   * @param node_ids Vector of node IDs from which to create the NodeCollection
   * @return a NodeCollection pointer to the created NodeCollection
   */
  static NodeCollectionPTR create( const IntVectorDatum& node_ids );

  /**
   * Create a NodeCollection from an array of node IDs.
   *
   * Results in a primitive if the node IDs are homogeneous and
   * contiguous, or a composite otherwise.
   *
   * @param node_ids Array of node IDs from which to create the NodeCollection
   * @return a NodeCollection pointer to the created NodeCollection
   */
  static NodeCollectionPTR create( const TokenArray& node_ids );

  /**
   * Create a NodeCollection from a single node ID.
   *
   * Results in a primitive unconditionally.
   *
   * @param node_id Node ID from which to create the NodeCollection
   * @return a NodeCollection pointer to the created NodeCollection
   */
  static NodeCollectionPTR create( const size_t node_id );

  /**
   * Create a NodeCollection from a single node pointer.
   *
   * Results in a primitive unconditionally.
   *
   * @param node Node pointer from which to create the NodeCollection
   * @return a NodeCollection pointer to the created NodeCollection
   */
  static NodeCollectionPTR create( const Node* node );

  /**
   * Create a NodeCollection from an array of node IDs.
   *
   * Results in a primitive if the node IDs are homogeneous and
   * contiguous, or a composite otherwise.
   *
   * @param node_ids Array of node IDs from which to create the NodeCollection
   * @return a NodeCollection pointer to the created NodeCollection
   */
  static NodeCollectionPTR create( const std::vector< size_t >& node_ids );

  /**
   * Check to see if the fingerprint of the NodeCollection matches that of the
   * kernel.
   *
   * @return true if the fingerprint matches that of the kernel, false otherwise
   */
  bool valid() const;

  /**
   * Print out the contents of the NodeCollection in a pretty and informative
   * way.
   */
  virtual void print_me( std::ostream& ) const = 0;

  /**
   * Get the node ID in the specified index in the NodeCollection.
   *
   * @param idx Index in the NodeCollection
   * @return a node ID
   */
  virtual size_t operator[]( size_t ) const = 0;

  /**
   * Join two NodeCollections.
   *
   * May return a primitive or composite, depending on
   * the input.
   *
   * @param rhs NodeCollection pointer to the NodeCollection to be added
   * @return a NodeCollection pointer
   */
  virtual NodeCollectionPTR operator+( NodeCollectionPTR ) const = 0;
  virtual bool operator==( NodeCollectionPTR ) const = 0;

  /**
   * Check if two NodeCollections are equal.
   *
   * @param rhs NodeCollection pointer to the NodeCollection to be checked against
   * @return true if they are equal, false otherwise
   */
  virtual bool operator!=( NodeCollectionPTR ) const;

  /**
   * Method to get an iterator representing the beginning of the NodeCollection.
   *
   * @return an iterator representing the beginning of the NodeCollection
   */
  virtual const_iterator begin( NodeCollectionPTR = NodeCollectionPTR( nullptr ) ) const = 0;

  /**
   * Return iterator stepping from first node on the thread it is called on over nodes on that thread.
   *
   * @return an iterator representing the beginning of the NodeCollection, in a
   * parallel context.
   */
  virtual const_iterator thread_local_begin( NodeCollectionPTR = NodeCollectionPTR( nullptr ) ) const = 0;

  /**
   * Method to get an iterator representing the beginning of the NodeCollection.
   *
   * @return an iterator representing the beginning of the NodeCollection, in an
   * MPI-parallel context.
   */
  virtual const_iterator rank_local_begin( NodeCollectionPTR = NodeCollectionPTR( nullptr ) ) const = 0;

  /**
   * Method to get an iterator representing the end of the NodeCollection.
   *
   * @param offset Index of element NC that iterator points to
   *
   * @return an iterator representing the end of the NodeCollection, taking
   * offset into account
   */
  virtual const_iterator end( NodeCollectionPTR = NodeCollectionPTR( nullptr ) ) const = 0;

  /**
   * Method that creates an ArrayDatum filled with node IDs from the NodeCollection.
   *
   * @return an ArrayDatum containing node IDs
   */
  virtual ArrayDatum to_array() const = 0;

  /**
   * Get the size of the NodeCollection.
   *
   * @return number of node IDs in the NodeCollection
   */
  virtual size_t size() const = 0;

  /**
   * Get the step of the NodeCollection.
   *
   * @return step between node IDs in the NodeCollection
   */
  virtual size_t step() const = 0;

  /**
   * Check if the NodeCollection contains a specified node ID
   *
   * @param node_id node ID to see if exists in the NodeCollection
   * @return true if the NodeCollection contains the node ID, false otherwise
   */
  virtual bool contains( const size_t node_id ) const = 0;

  /**
   * Slices the NodeCollection to the boundaries, with an optional step
   * parameter.
   *
   * Note that the boundaries being specified are inclusive.
   *
   * @param start Index of the NodeCollection to start at
   * @param end One past the index of the NodeCollection to stop at
   * @param step Number of places between node IDs to skip. Defaults to 1
   * @return a NodeCollection pointer to the new, sliced NodeCollection.
   */
  virtual NodeCollectionPTR slice( size_t start, size_t end, size_t step ) const = 0;

  /**
   * Sets the metadata of the NodeCollection.
   *
   * @param meta A Metadata pointer
   */
  virtual void set_metadata( NodeCollectionMetadataPTR ) = 0;

  /**
   * Gets the metadata of the NodeCollection.
   *
   * @return A Metadata pointer
   */
  virtual NodeCollectionMetadataPTR get_metadata() const = 0;

  virtual bool is_range() const = 0;

  /**
   * Checks if the NodeCollection has no elements.
   *
   * @return true if the NodeCollection is empty, false otherwise
   */
  virtual bool empty() const = 0;

  /**
   * Returns index of node with given node ID in NodeCollection.
   *
   * @return Index of node with given node ID; -1 if node not in NodeCollection.
   */
  virtual long get_lid( const size_t ) const = 0;

  /**
   * Returns whether the NodeCollection contains any nodes with proxies or not.
   *
   * @return true if any nodes in the NodeCollection has proxies, false otherwise.
   */
  virtual bool has_proxies() const = 0;

  /**
   * Collect metadata into dictionary.
   */
  void get_metadata_status( DictionaryDatum& ) const;

  /**
   * return the first stored ID (i.e, ID at index zero) inside the NodeCollection
   */
  size_t get_first() const;

  /**
   * return the last stored ID inside the NodeCollection
   */
  size_t get_last() const;


private:
  unsigned long fingerprint_; //!< Unique identity of the kernel that created the NodeCollection
  static NodeCollectionPTR create_();
  static NodeCollectionPTR create_( const std::vector< size_t >& );
};

/**
 * Subclass for the primitive NodeCollection type.
 *
 * The primitive type contains only homogeneous and contiguous node IDs. It also
 * contains model ID and metadata of the node IDs.
 */
class NodeCollectionPrimitive : public NodeCollection
{
  friend class nc_const_iterator;

private:
  size_t first_;                       //!< The first node ID in the primitive
  size_t last_;                        //!< The last node ID in the primitive
  size_t model_id_;                    //!< Model ID of the node IDs
  NodeCollectionMetadataPTR metadata_; //!< Pointer to the metadata of the node IDs
  bool nodes_have_no_proxies_;         //!< Whether the primitive contains devices or not

  /**
   * Raise an error if the model IDs of all nodes in the primitive are not the same as the expected model id.
   *
   * @note  For use in the constructor only.
   *
   * @param model_id Expected model id.
   */
  void assert_consistent_model_ids_( const size_t ) const;

public:
  using const_iterator = nc_const_iterator;

  /**
   * Create a primitive from a range of node IDs, with provided model ID and
   * metadata pointer.
   *
   * @param first The first node ID in the primitive
   * @param last  The last node ID in the primitive
   * @param model_id Model ID of the node IDs
   * @param meta Metadata pointer of the node IDs
   */
  NodeCollectionPrimitive( size_t first, size_t last, size_t model_id, NodeCollectionMetadataPTR );

  /**
   * Create a primitive from a range of node IDs, with provided model ID.
   *
   * @param first The first node ID in the primitive
   * @param last  The last node ID in the primitive
   * @param model_id Model ID of the node IDs
   */
  NodeCollectionPrimitive( size_t first, size_t last, size_t model_id );

  /**
   * Create a primitive from a range of node IDs. The model ID has to be found by
   * the constructor.
   *
   * @param first The first node ID in the primitive
   * @param last  The last node ID in the primitive
   */
  NodeCollectionPrimitive( size_t first, size_t last );

  /**
   * Primitive copy constructor.
   *
   * @param rhs Primitive to copy
   */
  NodeCollectionPrimitive( const NodeCollectionPrimitive& ) = default;

  /**
   * Primitive assignment operator.
   *
   * @param rhs Primitive to assign
   */
  NodeCollectionPrimitive& operator=( const NodeCollectionPrimitive& ) = default;

  /**
   * Create empty NodeCollection.
   *
   * @note This is only for use by SPBuilder.
   */
  NodeCollectionPrimitive();

  void print_me( std::ostream& ) const override;
  void print_primitive( std::ostream& ) const;

  size_t operator[]( const size_t ) const override;
  NodeCollectionPTR operator+( NodeCollectionPTR rhs ) const override;
  bool operator==( const NodeCollectionPTR rhs ) const override;
  bool operator==( const NodeCollectionPrimitive& rhs ) const;

  const_iterator begin( NodeCollectionPTR = NodeCollectionPTR( nullptr ) ) const override;
  const_iterator thread_local_begin( NodeCollectionPTR = NodeCollectionPTR( nullptr ) ) const override;
  const_iterator rank_local_begin( NodeCollectionPTR = NodeCollectionPTR( nullptr ) ) const override;
  const_iterator end( NodeCollectionPTR = NodeCollectionPTR( nullptr ) ) const override;

  //! Returns an ArrayDatum filled with node IDs from the primitive.
  ArrayDatum to_array() const override;

  //! Returns total number of node IDs in the primitive.
  size_t size() const override;

  //! Returns the step between node IDs in the primitive.
  size_t step() const override;

  bool contains( const size_t node_id ) const override;
  NodeCollectionPTR slice( size_t start, size_t end, size_t step = 1 ) const override;

  void set_metadata( NodeCollectionMetadataPTR ) override;

  NodeCollectionMetadataPTR get_metadata() const override;

  bool is_range() const override;
  bool empty() const override;

  long get_lid( const size_t ) const override;

  bool has_proxies() const override;

  /**
   * Checks if node IDs in another primitive is a continuation of node IDs in this
   * primitive.
   *
   * @param other Primitive to check for continuity
   * @return True if the first element in the other primitive is the next after
   * the last element in this primitive, and they both have the same model ID.
   * Otherwise false.
   */
  bool is_contiguous_ascending( const NodeCollectionPrimitive& other ) const;

  /**
   * Checks if node IDs of another primitive is overlapping node IDs of this primitive
   *
   * @param rhs Primitive to be checked.
   * @return True if the other primitive overlaps, false otherwise.
   */
  bool overlapping( const NodeCollectionPrimitive& rhs ) const;
};

NodeCollectionPTR operator+( NodeCollectionPTR lhs, NodeCollectionPTR rhs );

/**
 * Subclass for the composite NodeCollection type.
 *
 * The composite type contains a collection of primitives which are not
 * contiguous and homogeneous with each other. If the composite is sliced, it
 * also holds information about what index to start at, one past the index to end at, and
 * the step. The endpoint is one past the last valid node.
 *
 * @note To avoid creating copies of Primitives (not sure that saves much), Composite keeps
 * primitives as they are and then sets markers to the first node belonging to the slice
 * (tuple start_part, start_offset_) and one past the last node belongig to the slice ( tuple end_part_, end_offset_).
 * For the latter, the following logic applies to make comparison operators simpler
 * - Assume that the last element final of the composite collection (after all slicing effects are taken into account),
 * is in part i.
 * - Let idx_in_i be the index of final in part[i], i.e., part[i][index_in_i] == final.
 * - If final is the last element of part[i], i.e., idx_in_i == part[i].size()-1, then end_part_ == i+1 and end_offset_
 * == 0
 * - Otherwise, end_part_ == i and end_offset_ == idx_in_i
 * Thus,
 * - end_offset_ always observes "one-past-the-end" logic, while end_part_ does so only if end_offset_ == 0
 * - to iterate over all parts containing elements of the NC, we need to do
 *     for ( auto pix = start_part_ ; pix < end_part_ + ( end_offset_ == 0 ? 0 : 1 ) ; ++pix )
 * - the logic here is that if end_offset_ == 0, end_part_ already follows "one past" logic, but otherwise we need to
 * add 1
 */
class NodeCollectionComposite : public NodeCollection
{
  friend class nc_const_iterator;

private:
  std::vector< NodeCollectionPrimitive > parts_; //!< Vector of primitives
  size_t size_;                                  //!< Total number of node IDs
  size_t step_;                                  //!< Step length, set when slicing.
  size_t start_part_;                            //!< Primitive to start at, set when slicing
  size_t start_offset_;                          //!< Element to start at, set when slicing
  size_t end_part_;   //!< Primitive or one past the primitive to end at, set when slicing (see note above)
  size_t end_offset_; //!< One past the element to end at, set when slicing (see note above)
  bool is_sliced_;    //!< Whether the NodeCollectionComposite is sliced

  /**
   * Goes through the vector of primitives, merging as much as possible.
   *
   * @param parts Vector of primitives to be merged.
   */
  void merge_parts_( std::vector< NodeCollectionPrimitive >& parts ) const;

public:
  /**
   * Create a composite from a primitive, with boundaries and step length.
   *
   * @param primitive Primitive to be converted
   * @param start Offset in the primitive to begin at.
   * @param end Offset in the primitive, one past the node to end at.
   * @param step Length to step in the primitive.
   */
  NodeCollectionComposite( const NodeCollectionPrimitive&, size_t, size_t, size_t );

  /**
   * Composite copy constructor.
   *
   * @param comp Composite to be copied.
   */
  NodeCollectionComposite( const NodeCollectionComposite& ) = default;

  /**
   * Creates a new composite from another, with boundaries and step length.
   * This constructor is used only when slicing.
   *
   * @param composite Composite to slice.
   * @param start Index in the composite to begin at.
   * @param end Index in the composite one past the node to end at.
   * @param step Length to step in the composite.
   */
  NodeCollectionComposite( const NodeCollectionComposite&, size_t, size_t, size_t );

  /**
   * Create a composite from a vector of primitives.
   *
   * @param parts Vector of primitives.
   */
  explicit NodeCollectionComposite( const std::vector< NodeCollectionPrimitive >& );

  void print_me( std::ostream& ) const override;

  size_t operator[]( const size_t ) const override;

  /**
   * Addition operator.
   *
   * Joins this composite with another NodeCollection. The resulting
   * NodeCollection is sorted and merged, and converted to a primitive if
   * possible.
   *
   * @param rhs NodeCollection to add to this composite
   * @return a NodeCollection pointer to either a primitive or a composite.
   */
  NodeCollectionPTR operator+( NodeCollectionPTR rhs ) const override;
  NodeCollectionPTR operator+( const NodeCollectionPrimitive& rhs ) const;
  bool operator==( const NodeCollectionPTR rhs ) const override;

  const_iterator begin( NodeCollectionPTR = NodeCollectionPTR( nullptr ) ) const override;
  const_iterator thread_local_begin( NodeCollectionPTR = NodeCollectionPTR( nullptr ) ) const override;
  const_iterator rank_local_begin( NodeCollectionPTR = NodeCollectionPTR( nullptr ) ) const override;
  const_iterator end( NodeCollectionPTR = NodeCollectionPTR( nullptr ) ) const override;

  //! Returns an ArrayDatum filled with node IDs from the composite.
  ArrayDatum to_array() const override;

  //! Returns total number of node IDs in the composite.
  size_t size() const override;

  //! Returns the step between node IDs in the composite.
  size_t step() const override;

  bool contains( const size_t node_id ) const override;
  NodeCollectionPTR slice( size_t start, size_t end, size_t step = 1 ) const override;

  void set_metadata( NodeCollectionMetadataPTR ) override;

  NodeCollectionMetadataPTR get_metadata() const override;

  bool is_range() const override;
  bool empty() const override;

  long get_lid( const size_t ) const override;

  bool has_proxies() const override;
};

inline bool
NodeCollection::operator!=( NodeCollectionPTR rhs ) const
{
  return not( *this == rhs );
}

inline void
NodeCollection::set_metadata( NodeCollectionMetadataPTR )
{
  throw KernelException( "Cannot set Metadata on this type of NodeCollection." );
}

inline size_t
NodeCollection::get_first() const
{
  return ( *begin() ).node_id;
}

inline size_t
NodeCollection::get_last() const
{
  size_t offset = size() - 1;
  return ( *( begin() + offset ) ).node_id;
}


inline nc_const_iterator&
nc_const_iterator::operator+=( const size_t n )
{
  element_idx_ += n * step_;
  if ( composite_collection_ )
  {
    composite_update_indices_();
  }
  return *this;
}

inline nc_const_iterator
nc_const_iterator::operator+( const size_t n ) const
{
  nc_const_iterator it = *this;
  return it += n;
}

inline bool
nc_const_iterator::operator==( const nc_const_iterator& rhs ) const
{
  return part_idx_ == rhs.part_idx_ and element_idx_ == rhs.element_idx_;
}

inline bool
nc_const_iterator::operator!=( const nc_const_iterator& rhs ) const
{
  return not( part_idx_ == rhs.part_idx_ and element_idx_ == rhs.element_idx_ );
}

inline bool
nc_const_iterator::operator<( const nc_const_iterator& rhs ) const
{
  return ( part_idx_ < rhs.part_idx_ or ( part_idx_ == rhs.part_idx_ and element_idx_ < rhs.element_idx_ ) );
}

inline bool
nc_const_iterator::operator<=( const nc_const_iterator& rhs ) const
{
  return ( part_idx_ < rhs.part_idx_ or ( part_idx_ == rhs.part_idx_ and element_idx_ <= rhs.element_idx_ ) );
}

inline void
nc_const_iterator::get_current_part_offset( size_t& part, size_t& offset ) const
{
  part = part_idx_;
  offset = element_idx_;
}

inline size_t
nc_const_iterator::get_step_size() const
{
  return step_;
}

inline size_t
NodeCollectionPrimitive::operator[]( const size_t idx ) const
{
  // throw exception if outside of NodeCollection
  if ( first_ + idx > last_ )
  {
    throw std::out_of_range( "pos points outside of the NodeCollection" );
  }
  return first_ + idx;
}

inline bool
NodeCollectionPrimitive::operator==( NodeCollectionPTR rhs ) const
{
  auto const* const rhs_ptr = dynamic_cast< NodeCollectionPrimitive const* >( rhs.get() );
  // Checking that rhs_ptr is valid first, to avoid segfaults. If rhs is a NodeCollectionComposite,
  // rhs_ptr will be a null pointer.
  if ( not rhs_ptr )
  {
    return false;
  }

  // Not dereferencing rhs_ptr->metadata_ in the equality comparison because we want to avoid overloading
  // operator==() of *metadata_, and to let it handle typechecking.
  const bool eq_metadata = ( not metadata_ and not rhs_ptr->metadata_ )
    or ( metadata_ and rhs_ptr->metadata_ and *metadata_ == rhs_ptr->metadata_ );

  return first_ == rhs_ptr->first_ and last_ == rhs_ptr->last_ and model_id_ == rhs_ptr->model_id_ and eq_metadata;
}

inline bool
NodeCollectionPrimitive::operator==( const NodeCollectionPrimitive& rhs ) const
{
  // Not dereferencing rhs_ptr->metadata_ in the equality comparison because we want to avoid overloading
  // operator==() of *metadata_, and to let it handle typechecking.
  const bool eq_metadata =
    ( not metadata_ and not rhs.metadata_ ) or ( metadata_ and rhs.metadata_ and *metadata_ == rhs.metadata_ );

  return first_ == rhs.first_ and last_ == rhs.last_ and model_id_ == rhs.model_id_ and eq_metadata;
}

inline NodeCollectionPrimitive::const_iterator
NodeCollectionPrimitive::begin( NodeCollectionPTR cp ) const
{
  return const_iterator( cp, *this, 0 );
}

inline NodeCollectionPrimitive::const_iterator
NodeCollectionPrimitive::end( NodeCollectionPTR cp ) const
{
  return const_iterator( cp, *this, size() );
}

inline size_t
NodeCollectionPrimitive::size() const
{
  // empty NC has first_ == last_ == 0, need to handle that special
  return std::min( last_, last_ - first_ + 1 );
}

inline size_t
NodeCollectionPrimitive::step() const
{
  return 1;
}

inline bool
NodeCollectionPrimitive::contains( const size_t node_id ) const
{
  return first_ <= node_id and node_id <= last_;
}

inline void
NodeCollectionPrimitive::set_metadata( NodeCollectionMetadataPTR meta )
{
  metadata_ = meta;
}

inline NodeCollectionMetadataPTR
NodeCollectionPrimitive::get_metadata() const
{
  return metadata_;
}

inline bool
NodeCollectionPrimitive::is_range() const
{
  return true;
}

inline bool
NodeCollectionPrimitive::empty() const
{
  return last_ == 0;
}

inline long
NodeCollectionPrimitive::get_lid( const size_t neuron_id ) const
{
  if ( neuron_id > last_ )
  {
    return -1;
  }
  else
  {
    return neuron_id - first_;
  }
}

inline bool
NodeCollectionPrimitive::has_proxies() const
{
  return not nodes_have_no_proxies_;
}

inline NodeCollectionComposite::const_iterator
NodeCollectionComposite::begin( NodeCollectionPTR cp ) const
{
  return const_iterator( cp, *this, start_part_, start_offset_, step_ );
}

inline NodeCollectionComposite::const_iterator
NodeCollectionComposite::end( NodeCollectionPTR cp ) const
{
  if ( is_sliced_ )
  {
    return const_iterator( cp, *this, end_part_, end_offset_, step_ );
  }
  else
  {
    return const_iterator( cp, *this, parts_.size(), 0 );
  }
}

inline size_t
NodeCollectionComposite::size() const
{
  return size_;
}

inline size_t
NodeCollectionComposite::step() const
{
  return step_;
}

inline void
NodeCollectionComposite::set_metadata( NodeCollectionMetadataPTR meta )
{
  for ( auto& part : parts_ )
  {
    part.set_metadata( meta );
  }
}

inline NodeCollectionMetadataPTR
NodeCollectionComposite::get_metadata() const
{
  return parts_[ 0 ].get_metadata();
}

inline bool
NodeCollectionComposite::is_range() const
{
  return false;
}

inline bool
NodeCollectionComposite::empty() const
{
  // Composite NodeCollections can never be empty.
  return false;
}
} // namespace nest

#endif /* #ifndef NODE_COLLECTION_H */
