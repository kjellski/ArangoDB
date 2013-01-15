////////////////////////////////////////////////////////////////////////////////
/// @brief index
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2010-2011 triagens GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is triAGENS GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
/// @author Copyright 2011, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "index.h"

#include "BasicsC/conversions.h"
#include "BasicsC/files.h"
#include "BasicsC/json.h"
#include "BasicsC/linked-list.h"
#include "BasicsC/logging.h"
#include "BasicsC/string-buffer.h"
#include "BasicsC/strings.h"
#include "BasicsC/utf8-helper.h"
#include "FulltextIndex/fulltext-index.h"
#include "FulltextIndex/fulltext-wordlist.h"
#include "ShapedJson/shape-accessor.h"
#include "ShapedJson/shaped-json.h"
#include "VocBase/document-collection.h"
#include "VocBase/edge-collection.h"
#include "VocBase/voc-shaper.h"

// -----------------------------------------------------------------------------
// --SECTION--                                                             INDEX
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief initialise basic index properties
////////////////////////////////////////////////////////////////////////////////

static void InitIndex (TRI_index_t* idx, 
                       const TRI_idx_type_e type, 
                       struct TRI_primary_collection_s* collection,
                       bool unique) {
  idx->_iid            = TRI_NewTickVocBase();
  idx->_type           = type;
  idx->_collection     = collection;
  idx->_unique         = unique;
  
  // init common functions
  idx->cleanup         = NULL;

  LOG_TRACE("initialising index of type %s", TRI_TypeNameIndex(idx));
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief free an index
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeIndex (TRI_index_t* const idx) {
  assert(idx);

  LOG_TRACE("freeing index");

  switch (idx->_type) {
    case TRI_IDX_TYPE_GEO1_INDEX:
    case TRI_IDX_TYPE_GEO2_INDEX:
      TRI_FreeGeoIndex(idx);
      break;

    case TRI_IDX_TYPE_BITARRAY_INDEX:
      TRI_FreeBitarrayIndex(idx);
      break;

    case TRI_IDX_TYPE_HASH_INDEX:
      TRI_FreeHashIndex(idx);
      break;
    
    case TRI_IDX_TYPE_EDGE_INDEX:
      TRI_FreeEdgeIndex(idx);
      break;

    case TRI_IDX_TYPE_SKIPLIST_INDEX:
      TRI_FreeSkiplistIndex(idx);
      break;
    
    case TRI_IDX_TYPE_FULLTEXT_INDEX:
      TRI_FreeFulltextIndex(idx);
      break;

    case TRI_IDX_TYPE_CAP_CONSTRAINT:
      TRI_FreeCapConstraint(idx);
      break;

    case TRI_IDX_TYPE_PRIMARY_INDEX:
      TRI_FreePrimaryIndex(idx);
      break;

    default: 
      // no action necessary
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes an index file
////////////////////////////////////////////////////////////////////////////////

bool TRI_RemoveIndexFile (TRI_primary_collection_t* collection, TRI_index_t* idx) {
  char* filename;
  char* name;
  char* number;
  int res;

  // construct filename
  number = TRI_StringUInt64(idx->_iid);

  if (number == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
    LOG_ERROR("out of memory when creating index number");
    return false;
  }

  name = TRI_Concatenate3String("index-", number, ".json");

  if (name == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

    TRI_FreeString(TRI_CORE_MEM_ZONE, number);
    LOG_ERROR("out of memory when creating index name");
    return false;
  }

  filename = TRI_Concatenate2File(collection->base._directory, name);

  if (filename == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

    TRI_FreeString(TRI_CORE_MEM_ZONE, number);
    TRI_FreeString(TRI_CORE_MEM_ZONE, name);
    LOG_ERROR("out of memory when creating index filename");
    return false;
  }

  TRI_FreeString(TRI_CORE_MEM_ZONE, name);
  TRI_FreeString(TRI_CORE_MEM_ZONE, number);

  res = TRI_UnlinkFile(filename);
  TRI_FreeString(TRI_CORE_MEM_ZONE, filename);

  if (res != TRI_ERROR_NO_ERROR) {
    LOG_ERROR("cannot remove index definition: %s", TRI_last_error());
    return false;
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief saves an index
////////////////////////////////////////////////////////////////////////////////

int TRI_SaveIndex (TRI_primary_collection_t* collection, TRI_index_t* idx) {
  TRI_json_t* json;
  char* filename;
  char* name;
  char* number;
  bool ok;

  // convert into JSON
  json = idx->json(idx, collection);

  if (json == NULL) {
    LOG_TRACE("cannot save index definition: index cannot be jsonfied");
    return TRI_set_errno(TRI_ERROR_INTERNAL);
  }

  // construct filename
  number = TRI_StringUInt64(idx->_iid);
  name = TRI_Concatenate3String("index-", number, ".json");
  filename = TRI_Concatenate2File(collection->base._directory, name);

  TRI_FreeString(TRI_CORE_MEM_ZONE, name);
  TRI_FreeString(TRI_CORE_MEM_ZONE, number);

  // and save
  ok = TRI_SaveJson(filename, json);

  TRI_FreeString(TRI_CORE_MEM_ZONE, filename);
  TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);

  if (! ok) {
    LOG_ERROR("cannot save index definition: %s", TRI_last_error());
    return TRI_errno();
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up an index identifier
////////////////////////////////////////////////////////////////////////////////

TRI_index_t* TRI_LookupIndex (TRI_primary_collection_t* collection, TRI_idx_iid_t iid) {
  TRI_document_collection_t* doc;
  TRI_index_t* idx;
  size_t i;

  if (! TRI_IS_DOCUMENT_COLLECTION(collection->base._info._type)) {
    TRI_set_errno(TRI_ERROR_ARANGO_UNKNOWN_COLLECTION_TYPE);
    return NULL;
  }

  doc = (TRI_document_collection_t*) collection;

  for (i = 0;  i < doc->_allIndexes._length;  ++i) {
    idx = doc->_allIndexes._buffer[i];

    if (idx->_iid == iid) {
      return idx;
    }
  }

  TRI_set_errno(TRI_ERROR_ARANGO_NO_INDEX);
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief gets name of index type
////////////////////////////////////////////////////////////////////////////////

char const* TRI_TypeNameIndex (const TRI_index_t* const idx) {
  switch (idx->_type) {
    case TRI_IDX_TYPE_BITARRAY_INDEX:
      return "bitarray";
      
    case TRI_IDX_TYPE_HASH_INDEX: 
      return "hash";
    
    case TRI_IDX_TYPE_EDGE_INDEX: 
      return "edge";

    case TRI_IDX_TYPE_PRIORITY_QUEUE_INDEX:
      return "priorityqueue";

    case TRI_IDX_TYPE_SKIPLIST_INDEX:
      return "skiplist";
    
    case TRI_IDX_TYPE_FULLTEXT_INDEX:
      return "fulltext";

    case TRI_IDX_TYPE_GEO1_INDEX:
      return "geo1";

    case TRI_IDX_TYPE_GEO2_INDEX:
      return "geo2";

    case TRI_IDX_TYPE_CAP_CONSTRAINT:
      return "cap";

    case TRI_IDX_TYPE_PRIMARY_INDEX:
      return "primary";
  }

  return "unknown";
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return whether an index supports full coverage only
////////////////////////////////////////////////////////////////////////////////

// .............................................................................
// Full coverage here means that all fields which comprise a index MUST be
// assigned a value otherwise the index can not return a meaningful result. 
// Thus this function below returns whether full coverage is required by index.
// .............................................................................

bool TRI_NeedsFullCoverageIndex (const TRI_index_t* const idx) {
  // we'll use a switch here so the compiler warns if new index types are added elsewhere but not here
  switch (idx->_type) {
    case TRI_IDX_TYPE_GEO1_INDEX:
    case TRI_IDX_TYPE_GEO2_INDEX:
    case TRI_IDX_TYPE_PRIMARY_INDEX:
    case TRI_IDX_TYPE_HASH_INDEX:
    case TRI_IDX_TYPE_EDGE_INDEX:
    case TRI_IDX_TYPE_FULLTEXT_INDEX:
    case TRI_IDX_TYPE_PRIORITY_QUEUE_INDEX:
    case TRI_IDX_TYPE_CAP_CONSTRAINT:
      return true;
    case TRI_IDX_TYPE_BITARRAY_INDEX:
    case TRI_IDX_TYPE_SKIPLIST_INDEX:
      return false;
  }

  assert(false);
  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                     PRIMARY INDEX
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief insert methods does nothing
////////////////////////////////////////////////////////////////////////////////

static int InsertPrimary (TRI_index_t* idx, TRI_doc_mptr_t const* doc) {
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief update methods does nothing
////////////////////////////////////////////////////////////////////////////////

static int UpdatePrimary (TRI_index_t* idx, TRI_doc_mptr_t const* doc, TRI_shaped_json_t const* old) {
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief remove methods does nothing
////////////////////////////////////////////////////////////////////////////////

static int RemovePrimary (TRI_index_t* idx, TRI_doc_mptr_t const* doc) {
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief JSON description of a primary index
////////////////////////////////////////////////////////////////////////////////

static TRI_json_t* JsonPrimary (TRI_index_t* idx, TRI_primary_collection_t const* collection) {
  TRI_json_t* json;
  TRI_json_t* fields;

  json = TRI_CreateArrayJson(TRI_UNKNOWN_MEM_ZONE);
  fields = TRI_CreateListJson(TRI_UNKNOWN_MEM_ZONE);

  TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, fields, TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, "_id"));

  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "id", TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, 0));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "type", TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, "primary"));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "fields", fields);

  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                        constructors / destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief create the primary index
////////////////////////////////////////////////////////////////////////////////

TRI_index_t* TRI_CreatePrimaryIndex (struct TRI_primary_collection_s* collection) {
  TRI_index_t* primary;
  char* id;

  // create primary index
  primary = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_index_t), false);
  if (primary == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

    return NULL;
  }
  
  id = TRI_DuplicateStringZ(TRI_UNKNOWN_MEM_ZONE, "_id");
  if (id == NULL) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, primary);
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

    return NULL;
  }
  
  TRI_InitVectorString(&primary->_fields, TRI_UNKNOWN_MEM_ZONE);
  TRI_PushBackVectorString(&primary->_fields, id);
  
  InitIndex(primary, TRI_IDX_TYPE_PRIMARY_INDEX, collection, true);
  // override iid
  primary->_iid = 0;

  primary->insert = InsertPrimary;
  primary->remove = RemovePrimary;
  primary->update = UpdatePrimary;
  primary->json = JsonPrimary;

  return primary;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys a primary index, but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyPrimaryIndex (TRI_index_t* idx) {
  LOG_TRACE("destroying primary index");

  TRI_DestroyVectorString(&idx->_fields);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free a primary index
////////////////////////////////////////////////////////////////////////////////

void TRI_FreePrimaryIndex (TRI_index_t* idx) {
 TRI_DestroyPrimaryIndex(idx);
 TRI_Free(TRI_UNKNOWN_MEM_ZONE, idx); 
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                        EDGE INDEX
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief hashes an edge header
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashElementEdge (TRI_multi_pointer_t* array, void const* data) {
  TRI_edge_header_t const* h;
  uint64_t hash[3];

  h = data;

  // only include directional bits for hashing, exclude special bits
  hash[0] = (uint64_t) (h->_flags & TRI_EDGE_BITS_DIRECTION); 
  hash[1] = h->_cid;
  hash[2] = TRI_FnvHashString((char const*) h->_key); 

  return TRI_FnvHashPointer(hash, sizeof(hash));  
}

////////////////////////////////////////////////////////////////////////////////
/// @brief checks if key and element match
////////////////////////////////////////////////////////////////////////////////

static bool IsEqualKeyEdge (TRI_multi_pointer_t* array, 
                            void const* left, 
                            void const* right) {
  TRI_edge_header_t const* l;
  TRI_edge_header_t const* r;

  l = left;
  r = right;

  // only include directional flags, exclude special bits
  return ((l->_flags & TRI_EDGE_BITS_DIRECTION) == (r->_flags & TRI_EDGE_BITS_DIRECTION)) &&
         (l->_cid == r->_cid) && 
         (strcmp(l->_key, r->_key) == 0);  
}

////////////////////////////////////////////////////////////////////////////////
/// @brief checks for elements are equal
////////////////////////////////////////////////////////////////////////////////

static bool IsEqualElementEdge (TRI_multi_pointer_t* array, 
                                void const* left, 
                                void const* right) {
  TRI_edge_header_t const* l;
  TRI_edge_header_t const* r;

  l = left;
  r = right;

  // only include directional flags, exclude special bits
  return (l->_mptr == r->_mptr) && 
         ((l->_flags & TRI_EDGE_BITS_DIRECTION) == (r->_flags & TRI_EDGE_BITS_DIRECTION)) && 
         (l->_cid == r->_cid) && 
         (strcmp(l->_key, r->_key) == 0);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief insert method for edges
////////////////////////////////////////////////////////////////////////////////

static int InsertEdge (TRI_index_t* idx, TRI_doc_mptr_t const* doc) {
  TRI_edge_header_t* entries;
  TRI_edge_header_t* entryIn;
  TRI_edge_header_t* entryOut;
  TRI_doc_edge_key_marker_t const* edge;
  bool isReflexive;
  bool isBidirectional;
  TRI_multi_pointer_t* edgesIndex = &(((TRI_edge_index_t*) idx)->_edges);

  edge = doc->_data;

  // is the edge self-reflexive (_from & _to are identical)?
  isReflexive = (edge->_toCid == edge->_fromCid && strcmp(((char*) edge) + edge->_offsetToKey, ((char*) edge) + edge->_offsetFromKey) == 0);
  isBidirectional = edge->_isBidirectional;

  // allocate all edge headers and return early if memory allocation fails

  // use one allocation with 2 slots
  // the IN entry will be in the first slot, and we only need to free this one later
  // using one allocation with 2 slots saves a lot of mallocs, and speeds up the
  // index insertion for a large edge collection
  entries = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, 2 * sizeof(TRI_edge_header_t), false);
  if (entries == NULL) {
    return TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
  }
  
  // we have allocated all necessary memory by here

  // first slot: IN
  entryIn = entries;
  entryIn->_mptr = doc;
  entryIn->_flags = TRI_FlagsEdge(TRI_EDGE_IN, isReflexive, isBidirectional);
  entryIn->_cid = edge->_toCid;
  entryIn->_key = ((char*) edge) + edge->_offsetToKey;
  TRI_InsertElementMultiPointer(edgesIndex, entryIn, true, true);

  // second slot: OUT
  entryOut = entries + 1;
  entryOut->_mptr = doc;
  entryOut->_flags = TRI_FlagsEdge(TRI_EDGE_OUT, isReflexive, isBidirectional);
  entryOut->_cid = edge->_fromCid;
  entryOut->_key = ((char*) edge) + edge->_offsetFromKey;
  TRI_InsertElementMultiPointer(edgesIndex, entryOut, true, true);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief update method does nothing
////////////////////////////////////////////////////////////////////////////////

static int UpdateEdge (TRI_index_t* idx, TRI_doc_mptr_t const* doc, TRI_shaped_json_t const* old) {
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief remove an edge 
////////////////////////////////////////////////////////////////////////////////

static int RemoveEdge (TRI_index_t* idx, TRI_doc_mptr_t const* doc) {
  TRI_edge_header_t entry;
  TRI_edge_header_t* old;
  TRI_doc_edge_key_marker_t const* edge;
  TRI_multi_pointer_t* edgesIndex = &(((TRI_edge_index_t*) idx)->_edges);
  
  edge = doc->_data;
  
  entry._mptr = doc;

  // OUT
  // we do not need to free the OUT element
  entry._flags = TRI_LookupFlagsEdge(TRI_EDGE_OUT);
  entry._cid = edge->_fromCid;
  entry._key = ((char*) edge) + edge->_offsetFromKey;
  TRI_RemoveElementMultiPointer(edgesIndex, &entry);
  
  // IN
  entry._flags = TRI_LookupFlagsEdge(TRI_EDGE_IN);
  entry._cid = edge->_toCid;
  entry._key = ((char*) edge) + edge->_offsetToKey;
  old = TRI_RemoveElementMultiPointer(edgesIndex, &entry);

  // the pointer to the IN element is also the memory pointer we need to free
  if (old != NULL) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, old);
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief JSON description of edge index
////////////////////////////////////////////////////////////////////////////////

static TRI_json_t* JsonEdge (TRI_index_t* idx, TRI_primary_collection_t const* collection) {
  TRI_json_t* json;
  TRI_json_t* fields;

  json = TRI_CreateArrayJson(TRI_UNKNOWN_MEM_ZONE);
  fields = TRI_CreateListJson(TRI_UNKNOWN_MEM_ZONE);

  TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, fields, TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, "_from"));

  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "id", TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, 0));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "type", TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, "edge"));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "fields", fields);

  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                        constructors / destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief create the edge index
////////////////////////////////////////////////////////////////////////////////

TRI_index_t* TRI_CreateEdgeIndex (struct TRI_primary_collection_s* collection) {
  TRI_edge_index_t* edgeIndex;
  char* id;

  // create index
  edgeIndex = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_edge_index_t), false);
  if (edgeIndex == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

    return NULL;
  }
  
  id = TRI_DuplicateStringZ(TRI_UNKNOWN_MEM_ZONE, "_from");
  if (id == NULL) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, edgeIndex);
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

    return NULL;
  }
  
  TRI_InitVectorString(&edgeIndex->base._fields, TRI_UNKNOWN_MEM_ZONE);
  TRI_PushBackVectorString(&edgeIndex->base._fields, id);
 
  InitIndex(&edgeIndex->base, TRI_IDX_TYPE_EDGE_INDEX, collection, false); 
  edgeIndex->base.insert  = InsertEdge;
  edgeIndex->base.remove  = RemoveEdge;
  edgeIndex->base.update  = UpdateEdge;
  edgeIndex->base.json    = JsonEdge;

  TRI_InitMultiPointer(&edgeIndex->_edges,
                       TRI_UNKNOWN_MEM_ZONE, 
                       HashElementEdge,
                       HashElementEdge,
                       IsEqualKeyEdge,
                       IsEqualElementEdge);

  return &edgeIndex->base;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys the edge index, but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyEdgeIndex (TRI_index_t* idx) {
  TRI_edge_index_t* edgesIndex;
  size_t i, n;

  edgesIndex = (TRI_edge_index_t*) idx;

  LOG_TRACE("destroying edge index");

  // free all elements in the edges index
  n = edgesIndex->_edges._nrAlloc;

  for (i = 0; i < n; ++i) {
    TRI_edge_header_t* element = edgesIndex->_edges._table[i];
    if (element != NULL && (element->_flags & TRI_EDGE_BIT_DIRECTION_IN) != 0) {
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, element);
    }
  }

  TRI_DestroyMultiPointer(&edgesIndex->_edges);
  
  TRI_DestroyVectorString(&idx->_fields);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the edge index
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeEdgeIndex (TRI_index_t* idx) {
  TRI_DestroyEdgeIndex(idx);
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, idx);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    CAP CONSTRAINT
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief describes a cap constraint as a json object
////////////////////////////////////////////////////////////////////////////////

static TRI_json_t* JsonCapConstraint (TRI_index_t* idx, TRI_primary_collection_t const* collection) {
  TRI_json_t* json;
  TRI_cap_constraint_t* cap;
   
  // recast as a cap constraint
  cap = (TRI_cap_constraint_t*) idx;

  if (cap == NULL) {
    TRI_set_errno(TRI_ERROR_INTERNAL);
    return NULL;
  }
  
  // create json object and fill it
  json = TRI_CreateArrayJson(TRI_UNKNOWN_MEM_ZONE);
  if (json == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

    return NULL;
  }

  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "id",   TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, idx->_iid));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "type", TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, "cap"));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "size",  TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, cap->_size));

  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a cap constraint from collection
////////////////////////////////////////////////////////////////////////////////

static void RemoveIndexCapConstraint (TRI_index_t* idx, TRI_primary_collection_t* collection) {
  collection->_capConstraint = NULL;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief inserts a document
////////////////////////////////////////////////////////////////////////////////

static int InsertCapConstraint (TRI_index_t* idx, TRI_doc_mptr_t const* doc) {
  TRI_cap_constraint_t* cap;

  cap = (TRI_cap_constraint_t*) idx;

  return TRI_AddLinkedArray(&cap->_array, doc);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief updates a document
////////////////////////////////////////////////////////////////////////////////

static int UpdateCapConstraint (TRI_index_t* idx,
                                const TRI_doc_mptr_t* newDoc, 
                                const TRI_shaped_json_t* oldDoc) {
  TRI_cap_constraint_t* cap;

  cap = (TRI_cap_constraint_t*) idx;
  TRI_MoveToBackLinkedArray(&cap->_array, newDoc);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a document
////////////////////////////////////////////////////////////////////////////////

static int RemoveCapConstraint (TRI_index_t* idx, TRI_doc_mptr_t const* doc) {
  TRI_cap_constraint_t* cap;

  cap = (TRI_cap_constraint_t*) idx;
  TRI_RemoveLinkedArray(&cap->_array, doc);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a cap constraint
////////////////////////////////////////////////////////////////////////////////

TRI_index_t* TRI_CreateCapConstraint (struct TRI_primary_collection_s* collection,
                                      size_t size) {
  TRI_cap_constraint_t* cap;

  cap = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_cap_constraint_t), false);
  if (cap == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

    return NULL;
  }
  
  InitIndex(&cap->base, TRI_IDX_TYPE_CAP_CONSTRAINT, collection, false); 
  cap->base.json = JsonCapConstraint;
  cap->base.removeIndex = RemoveIndexCapConstraint;

  cap->base.insert = InsertCapConstraint;
  cap->base.update = UpdateCapConstraint;
  cap->base.remove = RemoveCapConstraint;

  TRI_InitLinkedArray(&cap->_array, TRI_UNKNOWN_MEM_ZONE);

  cap->_size = size;
  
  return &cap->base;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated, but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyCapConstraint (TRI_index_t* idx) {
  TRI_cap_constraint_t* cap = (TRI_cap_constraint_t*) idx;

  TRI_DestroyLinkedArray(&cap->_array);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated and frees the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeCapConstraint (TRI_index_t* idx) {
  TRI_DestroyCapConstraint(idx);
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, idx);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                         GEO INDEX
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief extracts a double value from an array
////////////////////////////////////////////////////////////////////////////////

static bool ExtractDoubleArray (TRI_shaper_t* shaper,
                                TRI_shaped_json_t const* document,
                                TRI_shape_pid_t pid,
                                double* result,
                                bool* missing) {
  TRI_shape_t const* shape;
  TRI_shaped_json_t json;
  bool ok;

  *missing = false;

  ok = TRI_ExtractShapedJsonVocShaper(shaper, document, 0, pid, &json, &shape);

  if (! ok) {
    return false;
  }

  if (shape == NULL) {
    *missing = true;
    return false;
  }
  else if (json._sid == shaper->_sidNumber) {
    *result = * (double*) json._data.data;
    return true;
  }
  else if (json._sid == shaper->_sidNull) {
    *missing = true;
    return false;
  }
  else {
    return false;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief extracts a double value from a list
////////////////////////////////////////////////////////////////////////////////

static bool ExtractDoubleList (TRI_shaper_t* shaper,
                               TRI_shaped_json_t const* document,
                               TRI_shape_pid_t pid,
                               double* latitude,
                               double* longitude,
                               bool* missing) {
  TRI_shape_t const* shape;
  TRI_shaped_json_t entry;
  TRI_shaped_json_t list;
  bool ok;
  size_t len;

  *missing = false;

  ok = TRI_ExtractShapedJsonVocShaper(shaper, document, 0, pid, &list, &shape);

  if (! ok) {
    return false;
  }

  if (shape == NULL) {
    *missing = true;
    return false;
  }

  // in-homogenous list
  if (shape->_type == TRI_SHAPE_LIST) {
    len = TRI_LengthListShapedJson((const TRI_list_shape_t*) shape, &list);

    if (len < 2) {
      return false;
    }

    // latitude
    ok = TRI_AtListShapedJson((const TRI_list_shape_t*) shape, &list, 0, &entry);

    if (! ok || entry._sid != shaper->_sidNumber) {
      return false;
    }

    *latitude = * (double*) entry._data.data;

    // longitude
    ok = TRI_AtListShapedJson((const TRI_list_shape_t*) shape, &list, 1, &entry);

    if (! ok || entry._sid != shaper->_sidNumber) {
      return false;
    }

    *longitude = * (double*) entry._data.data;

    return true;
  }

  // homogenous list
  else if (shape->_type == TRI_SHAPE_HOMOGENEOUS_LIST) {
    const TRI_homogeneous_list_shape_t* hom;

    hom = (const TRI_homogeneous_list_shape_t*) shape;

    if (hom->_sidEntry != shaper->_sidNumber) {
      return false;
    }

    len = TRI_LengthHomogeneousListShapedJson((const TRI_homogeneous_list_shape_t*) shape, &list);

    if (len < 2) {
      return false;
    }

    // latitude
    ok = TRI_AtHomogeneousListShapedJson((const TRI_homogeneous_list_shape_t*) shape, &list, 0, &entry);

    if (! ok) {
      return false;
    }

    *latitude = * (double*) entry._data.data;

    // longitude
    ok = TRI_AtHomogeneousListShapedJson((const TRI_homogeneous_list_shape_t*) shape, &list, 1, &entry);

    if (! ok) {
      return false;
    }

    *longitude = * (double*) entry._data.data;

    return true;
  }

  // homogeneous list
  else if (shape->_type == TRI_SHAPE_HOMOGENEOUS_SIZED_LIST) {
    const TRI_homogeneous_sized_list_shape_t* hom;

    hom = (const TRI_homogeneous_sized_list_shape_t*) shape;

    if (hom->_sidEntry != shaper->_sidNumber) {
      return false;
    }

    len = TRI_LengthHomogeneousSizedListShapedJson((const TRI_homogeneous_sized_list_shape_t*) shape, &list);

    if (len < 2) {
      return false;
    }

    // latitude
    ok = TRI_AtHomogeneousSizedListShapedJson((const TRI_homogeneous_sized_list_shape_t*) shape, &list, 0, &entry);

    if (! ok) {
      return false;
    }

    *latitude = * (double*) entry._data.data;

    // longitude
    ok = TRI_AtHomogeneousSizedListShapedJson((const TRI_homogeneous_sized_list_shape_t*) shape, &list, 1, &entry);

    if (! ok) {
      return false;
    }

    *longitude = * (double*) entry._data.data;

    return true;
  }

  // null
  else if (shape->_type == TRI_SHAPE_NULL) {
    *missing = true;
  }

  // ups
  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief inserts a new document
////////////////////////////////////////////////////////////////////////////////

static int InsertGeoIndex (TRI_index_t* idx, TRI_doc_mptr_t const* doc) {
  union { void* p; void const* c; } cnv;
  GeoCoordinate gc;
  TRI_shaped_json_t shapedJson;
  TRI_geo_index_t* geo;
  TRI_shaper_t* shaper;
  bool missing;
  bool ok;
  double latitude;
  double longitude;
  int res;

  geo = (TRI_geo_index_t*) idx;
  shaper = geo->base._collection->_shaper;
  TRI_EXTRACT_SHAPED_JSON_MARKER(shapedJson, doc->_data);

  // lookup latitude and longitude
  if (geo->_location != 0) {
    if (geo->_geoJson) {
      ok = ExtractDoubleList(shaper, &shapedJson, geo->_location, &longitude, &latitude, &missing);
    }
    else {
      ok = ExtractDoubleList(shaper, &shapedJson, geo->_location, &latitude, &longitude, &missing);
    }
  }
  else {
    ok = ExtractDoubleArray(shaper, &shapedJson, geo->_latitude, &latitude, &missing);
    ok = ok && ExtractDoubleArray(shaper, &shapedJson, geo->_longitude, &longitude, &missing);
  }

  if (! ok) {
    if (geo->_constraint) {
      if (geo->base._ignoreNull && missing) {
        return TRI_ERROR_NO_ERROR;
      }
      else {
        return TRI_set_errno(TRI_ERROR_ARANGO_GEO_INDEX_VIOLATED);
      }
    }
    else {
      return TRI_ERROR_NO_ERROR;
    }
  }

  // and insert into index
  gc.latitude = latitude;
  gc.longitude = longitude;

  cnv.c = doc;
  gc.data = cnv.p;

  res = GeoIndex_insert(geo->_geoIndex, &gc);

  if (res == -1) {
    LOG_WARNING("found duplicate entry in geo-index, should not happen");
    return TRI_set_errno(TRI_ERROR_INTERNAL);
  }
  else if (res == -2) {
    return TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
  }
  else if (res == -3) {
    if (geo->_constraint) {
      LOG_DEBUG("illegal geo-coordinates, ignoring entry");
      return TRI_set_errno(TRI_ERROR_ARANGO_GEO_INDEX_VIOLATED);
    }
    else {
      return TRI_ERROR_NO_ERROR;
    }
  }
  else if (res < 0) {
    return TRI_set_errno(TRI_ERROR_INTERNAL);
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief updates a document
////////////////////////////////////////////////////////////////////////////////

static int UpdateGeoIndex (TRI_index_t* idx, TRI_doc_mptr_t const* doc, TRI_shaped_json_t const* old) {
  union { void* p; void const* c; } cnv;
  GeoCoordinate gc;
  TRI_shaped_json_t shapedJson;
  TRI_geo_index_t* geo;
  TRI_shaper_t* shaper;
  bool missing;
  bool ok;
  double latitude;
  double longitude;
  int res;

  geo = (TRI_geo_index_t*) idx;
  shaper = geo->base._collection->_shaper;
  TRI_EXTRACT_SHAPED_JSON_MARKER(shapedJson, doc->_data);

  // lookup OLD latitude and longitude
  if (geo->_location != 0) {
    ok = ExtractDoubleList(shaper, old, geo->_location, &latitude, &longitude, &missing);
  }
  else {
    ok = ExtractDoubleArray(shaper, old, geo->_latitude, &latitude, &missing);
    ok = ok && ExtractDoubleArray(shaper, old, geo->_longitude, &longitude, &missing);
  }

  // and remove old entry
  if (ok) {
    gc.latitude = latitude;
    gc.longitude = longitude;

    cnv.c = doc;
    gc.data = cnv.p;

    res = GeoIndex_remove(geo->_geoIndex, &gc);

    if (res != 0) {
      LOG_DEBUG("cannot remove old index entry: %d", res);
    }
  }

  // create new entry with new coordinates
  if (geo->_location != 0) {
    ok = ExtractDoubleList(shaper, &shapedJson, geo->_location, &latitude, &longitude, &missing);
  }
  else {
    ok = ExtractDoubleArray(shaper, &shapedJson, geo->_latitude, &latitude, &missing);
    ok = ok && ExtractDoubleArray(shaper, &shapedJson, geo->_longitude, &longitude, &missing);
  }

  if (! ok) {
    if (geo->_constraint) {
      if (geo->base._ignoreNull && missing) {
        return TRI_ERROR_NO_ERROR;
      }
      else {
        return TRI_set_errno(TRI_ERROR_ARANGO_GEO_INDEX_VIOLATED);
      }
    }
    else {
      return TRI_ERROR_NO_ERROR;
    }
  }

  gc.latitude = latitude;
  gc.longitude = longitude;

  cnv.c = doc;
  gc.data = cnv.p;

  res = GeoIndex_insert(geo->_geoIndex, &gc);

  if (res == -1) {
    LOG_WARNING("found duplicate entry in geo-index, should not happen");
    return TRI_set_errno(TRI_ERROR_INTERNAL);
  }
  else if (res == -2) {
    LOG_WARNING("out-of-memory in geo-index");
    return TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
  }
  else if (res == -3) {
    if (geo->_constraint) {
      LOG_DEBUG("illegal geo-coordinates, ignoring entry");
      return TRI_set_errno(TRI_ERROR_ARANGO_GEO_INDEX_VIOLATED);
    }
    else {
      return TRI_ERROR_NO_ERROR;
    }
  }
  else if (res < 0) {
    return TRI_set_errno(TRI_ERROR_INTERNAL);
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief erases a document
////////////////////////////////////////////////////////////////////////////////

static int RemoveGeoIndex (TRI_index_t* idx, TRI_doc_mptr_t const* doc) {
  union { void* p; void const* c; } cnv;
  GeoCoordinate gc;
  TRI_shaped_json_t shapedJson;
  TRI_geo_index_t* geo;
  TRI_shaper_t* shaper;
  bool missing;
  bool ok;
  double latitude;
  double longitude;

  geo = (TRI_geo_index_t*) idx;
  shaper = geo->base._collection->_shaper;
  TRI_EXTRACT_SHAPED_JSON_MARKER(shapedJson, doc->_data);

  // lookup OLD latitude and longitude
  if (geo->_location != 0) {
    ok = ExtractDoubleList(shaper, &shapedJson, geo->_location, &latitude, &longitude, &missing);
  }
  else {
    ok = ExtractDoubleArray(shaper, &shapedJson, geo->_latitude, &latitude, &missing);
    ok = ok && ExtractDoubleArray(shaper, &shapedJson, geo->_longitude, &longitude, &missing);
  }

  // and remove old entry
  if (ok) {
    int res;

    gc.latitude = latitude;
    gc.longitude = longitude;

    cnv.c = doc;
    gc.data = cnv.p;

    res = GeoIndex_remove(geo->_geoIndex, &gc);

    if (res != 0) {
      LOG_DEBUG("cannot remove old index entry: %d", res);
      return TRI_set_errno(TRI_ERROR_INTERNAL);
    }
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief JSON description of a geo index, location is a list
////////////////////////////////////////////////////////////////////////////////

static TRI_json_t* JsonGeo1Index (TRI_index_t* idx, TRI_primary_collection_t const* collection) {
  TRI_json_t* json;
  TRI_json_t* fields;
  TRI_shape_path_t const* path;
  char const* location;
  TRI_geo_index_t* geo;

  geo = (TRI_geo_index_t*) idx;

  // convert location to string
  path = collection->_shaper->lookupAttributePathByPid(collection->_shaper, geo->_location);

  if (path == 0) {
    return NULL;
  }

  location = ((char const*) path) + sizeof(TRI_shape_path_t) + (path->_aidLength * sizeof(TRI_shape_aid_t));

  // create json
  json = TRI_CreateArrayJson(TRI_UNKNOWN_MEM_ZONE);

  if (json == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
    return NULL;
  }

  fields = TRI_CreateListJson(TRI_UNKNOWN_MEM_ZONE);
  TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, fields, TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, location));

  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "id", TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, idx->_iid));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "type", TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, "geo1"));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "geoJson", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, geo->_geoJson));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "constraint", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, geo->_constraint));

  if (geo->_constraint) {
    TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "ignoreNull", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, geo->base._ignoreNull));
  }

  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "fields", fields);

  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief JSON description of a geo index, two attributes
////////////////////////////////////////////////////////////////////////////////

static TRI_json_t* JsonGeo2Index (TRI_index_t* idx, TRI_primary_collection_t const* collection) {
  TRI_json_t* json;
  TRI_json_t* fields;
  TRI_shape_path_t const* path;
  char const* latitude;
  char const* longitude;
  TRI_geo_index_t* geo;

  geo = (TRI_geo_index_t*) idx;

  // convert latitude to string
  path = collection->_shaper->lookupAttributePathByPid(collection->_shaper, geo->_latitude);

  if (path == 0) {
    return NULL;
  }

  latitude = ((char const*) path) + sizeof(TRI_shape_path_t) + path->_aidLength * sizeof(TRI_shape_aid_t);

  // convert longitude to string
  path = collection->_shaper->lookupAttributePathByPid(collection->_shaper, geo->_longitude);

  if (path == 0) {
    return NULL;
  }

  longitude = ((char const*) path) + sizeof(TRI_shape_path_t) + path->_aidLength * sizeof(TRI_shape_aid_t);

  // create json
  json = TRI_CreateArrayJson(TRI_UNKNOWN_MEM_ZONE);

  if (json == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
    return NULL;
  }

  fields = TRI_CreateListJson(TRI_UNKNOWN_MEM_ZONE);
  TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, fields, TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, latitude));
  TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, fields, TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, longitude));

  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "id", TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, idx->_iid));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "type", TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, "geo2"));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "constraint", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, geo->_constraint));

  if (geo->_constraint) {
    TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "ignoreNull", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, geo->base._ignoreNull));
  }

  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "fields", fields);

  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a json index from collection
////////////////////////////////////////////////////////////////////////////////

static void RemoveIndexGeoIndex (TRI_index_t* idx, TRI_primary_collection_t* collection) {
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a geo-index for lists
////////////////////////////////////////////////////////////////////////////////

TRI_index_t* TRI_CreateGeo1Index (struct TRI_primary_collection_s* collection,
                                  char const* locationName,
                                  TRI_shape_pid_t location,
                                  bool geoJson,
                                  bool constraint,
                                  bool ignoreNull) {
  TRI_geo_index_t* geo;
  char* ln;
  int result;
  
  geo = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_geo_index_t), false);

  if (geo == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

    return NULL;
  }

  ln = TRI_DuplicateStringZ(TRI_UNKNOWN_MEM_ZONE, locationName);
  if (ln == NULL) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, geo);
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

    return NULL;
  }

  TRI_InitVectorString(&geo->base._fields, TRI_UNKNOWN_MEM_ZONE);

  InitIndex(&geo->base, TRI_IDX_TYPE_GEO1_INDEX, collection, false); 
  geo->base._ignoreNull = ignoreNull;

  geo->base.json = JsonGeo1Index;
  geo->base.removeIndex = RemoveIndexGeoIndex;

  geo->base.insert = InsertGeoIndex;
  geo->base.remove = RemoveGeoIndex;
  geo->base.update = UpdateGeoIndex;

  TRI_PushBackVectorString(&geo->base._fields, ln);

  geo->_constraint = constraint;
  geo->_geoIndex = GeoIndex_new();
  
  if (geo->_geoIndex == NULL) { // oops out of memory?
    LOG_WARNING("geo index creation failed -- internal error when creating new goe index structure");
    TRI_DestroyVectorString(&geo->base._fields);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, geo);
    return NULL;
  }
  
  geo->_variant = geoJson ? INDEX_GEO_COMBINED_LAT_LON : INDEX_GEO_COMBINED_LON_LAT;
  geo->_location = location;
  geo->_latitude = 0;
  geo->_longitude = 0;
  geo->_geoJson = geoJson;
  
  result = GeoIndex_assignMethod(&(geo->base.indexQuery), TRI_INDEX_METHOD_ASSIGNMENT_QUERY);
  result = result || GeoIndex_assignMethod(&(geo->base.indexQueryFree), TRI_INDEX_METHOD_ASSIGNMENT_FREE);
  result = result || GeoIndex_assignMethod(&(geo->base.indexQueryResult), TRI_INDEX_METHOD_ASSIGNMENT_RESULT);
  
  if (result != TRI_ERROR_NO_ERROR) {
    TRI_DestroyVectorString(&geo->base._fields);
    GeoIndex_free(geo->_geoIndex); 
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, geo);
    LOG_WARNING("geo index creation failed -- internal error when assigning function calls");
    return NULL;
  }
  
  return &geo->base;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a geo-index for arrays
////////////////////////////////////////////////////////////////////////////////

TRI_index_t* TRI_CreateGeo2Index (struct TRI_primary_collection_s* collection,
                                  char const* latitudeName,
                                  TRI_shape_pid_t latitude,
                                  char const* longitudeName,
                                  TRI_shape_pid_t longitude,
                                  bool constraint,
                                  bool ignoreNull) {
  TRI_geo_index_t* geo;
  char* lat;
  char* lon;
  int result;
  
  geo = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_geo_index_t), false);

  if (geo == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

    return NULL;
  }

  lat = TRI_DuplicateStringZ(TRI_UNKNOWN_MEM_ZONE, latitudeName);
  if (lat == NULL) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, geo);
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

    return NULL;
  }

  lon = TRI_DuplicateStringZ(TRI_UNKNOWN_MEM_ZONE, longitudeName);
  if (lon == NULL) {
    TRI_FreeString(TRI_UNKNOWN_MEM_ZONE, lat);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, geo);
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

    return NULL;
  }

  TRI_InitVectorString(&geo->base._fields, TRI_UNKNOWN_MEM_ZONE);

  InitIndex(&geo->base, TRI_IDX_TYPE_GEO2_INDEX, collection, false); 
  geo->base._ignoreNull = ignoreNull;

  geo->base.json = JsonGeo2Index;
  geo->base.removeIndex = RemoveIndexGeoIndex;

  geo->base.insert = InsertGeoIndex;
  geo->base.remove = RemoveGeoIndex;
  geo->base.update = UpdateGeoIndex;

  TRI_PushBackVectorString(&geo->base._fields, lat);
  TRI_PushBackVectorString(&geo->base._fields, lon);

  geo->_constraint = constraint;
  geo->_geoIndex = GeoIndex_new();

  if (geo->_geoIndex == NULL) { // oops out of memory?
    LOG_WARNING("geo index creation failed -- internal error when creating new goe index structure");
    TRI_DestroyVectorString(&geo->base._fields);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, geo);
    return NULL;
  }
  
  geo->_variant = INDEX_GEO_INDIVIDUAL_LAT_LON;
  geo->_location = 0;
  geo->_latitude = latitude;
  geo->_longitude = longitude;

  result = GeoIndex_assignMethod(&(geo->base.indexQuery), TRI_INDEX_METHOD_ASSIGNMENT_QUERY);
  result = result || GeoIndex_assignMethod(&(geo->base.indexQueryFree), TRI_INDEX_METHOD_ASSIGNMENT_FREE);
  result = result || GeoIndex_assignMethod(&(geo->base.indexQueryResult), TRI_INDEX_METHOD_ASSIGNMENT_RESULT);
    
  if (result != TRI_ERROR_NO_ERROR) {
    TRI_DestroyVectorString(&geo->base._fields);
    GeoIndex_free(geo->_geoIndex); 
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, geo);

    LOG_WARNING("geo index creation failed -- internal error when assigning function calls");
    return NULL;
  }
  
  return &geo->base;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated, but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyGeoIndex (TRI_index_t* idx) {
  TRI_geo_index_t* geo;

  LOG_TRACE("destroying geo index");
  TRI_DestroyVectorString(&idx->_fields);

  geo = (TRI_geo_index_t*) idx;

  GeoIndex_free(geo->_geoIndex);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated and frees the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeGeoIndex (TRI_index_t* idx) {
  TRI_DestroyGeoIndex(idx);
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, idx);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up all points within a given radius
////////////////////////////////////////////////////////////////////////////////

GeoCoordinates* TRI_WithinGeoIndex (TRI_index_t* idx,
                                    double lat,
                                    double lon,
                                    double radius) {
  TRI_geo_index_t* geo;
  GeoCoordinate gc;

  geo = (TRI_geo_index_t*) idx;
  gc.latitude = lat;
  gc.longitude = lon;

  return GeoIndex_PointsWithinRadius(geo->_geoIndex, &gc, radius);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief looks up the nearest points
////////////////////////////////////////////////////////////////////////////////

GeoCoordinates* TRI_NearestGeoIndex (TRI_index_t* idx,
                                     double lat,
                                     double lon,
                                     size_t count) {
  TRI_geo_index_t* geo;
  GeoCoordinate gc;

  geo = (TRI_geo_index_t*) idx;
  gc.latitude = lat;
  gc.longitude = lon;

  return GeoIndex_NearestCountPoints(geo->_geoIndex, &gc, count);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////


// -----------------------------------------------------------------------------
// --SECTION--                                                        HASH INDEX
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief helper for hashing
////////////////////////////////////////////////////////////////////////////////

static int HashIndexHelper (TRI_hash_index_t const* hashIndex, 
                            HashIndexElement* hashElement,
                            TRI_doc_mptr_t const* document,
                            TRI_shaped_json_t const* shapedDoc) {
  union { void* p; void const* c; } cnv;
  TRI_shape_access_t const* acc;
  TRI_shaped_json_t shapedObject;
  TRI_shaped_json_t shapedJson;
  TRI_shaper_t* shaper;
  int res;
  size_t j;
  
  shaper = hashIndex->base._collection->_shaper;

  // .............................................................................
  // Attempting to locate a hash entry using TRI_shaped_json_t object. Use this
  // when we wish to remove a hash entry and we only have the "keys" rather than
  // having the document (from which the keys would follow).
  // .............................................................................

  if (shapedDoc != NULL) {
    hashElement->data = NULL;
  }

  // .............................................................................
  // Assign the document to the HashIndexElement structure - so that it can
  // later be retreived.
  // .............................................................................

  else if (document != NULL) {
    TRI_EXTRACT_SHAPED_JSON_MARKER(shapedJson, document->_data);

    cnv.c = document;
    hashElement->data = cnv.p;

    shapedDoc = &shapedJson;
  }

  else {
    return TRI_ERROR_INTERNAL;
  }
  
  // .............................................................................
  // Extract the attribute values
  // .............................................................................

  res = TRI_ERROR_NO_ERROR;

  for (j = 0;  j < hashIndex->_paths._length;  ++j) {
    TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(&hashIndex->_paths, j)));
      
    // determine if document has that particular shape 
    acc = TRI_FindAccessorVocShaper(shaper, shapedDoc->_sid, shape);

    if (acc == NULL || acc->_shape == NULL) {
      shapedObject._sid = shaper->_sidNull;
      shapedObject._data.length = 0;
      shapedObject._data.data = NULL;

      res = TRI_WARNING_ARANGO_INDEX_HASH_DOCUMENT_ATTRIBUTE_MISSING;
    }
    else {
     
      // extract the field
      if (! TRI_ExecuteShapeAccessor(acc, shapedDoc, &shapedObject)) {
        // TRI_Free(hashElement->fields); memory deallocated in the calling procedure
        return TRI_ERROR_INTERNAL;
      }

      if (shapedObject._sid == shaper->_sidNull) {
        res = TRI_WARNING_ARANGO_INDEX_HASH_DOCUMENT_ATTRIBUTE_MISSING;
      }
    }
      
    // store the json shaped Object -- this is what will be hashed
    hashElement->fields[j] = shapedObject;
  }
  
  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief describes a hash index as a json object
////////////////////////////////////////////////////////////////////////////////

static TRI_json_t* JsonHashIndex (TRI_index_t* idx, TRI_primary_collection_t const* collection) {
  TRI_json_t* json;
  TRI_json_t* fields;
  const TRI_shape_path_t* path;
  TRI_hash_index_t* hashIndex;
  char const** fieldList;
  size_t j;
   
  // ..........................................................................  
  // Recast as a hash index
  // ..........................................................................  

  hashIndex = (TRI_hash_index_t*) idx;

  if (hashIndex == NULL) {
    TRI_set_errno(TRI_ERROR_INTERNAL);
    return NULL;
  }
  
  // ..........................................................................  
  // Allocate sufficent memory for the field list
  // ..........................................................................  

  fieldList = TRI_Allocate( TRI_UNKNOWN_MEM_ZONE, (sizeof(char*) * hashIndex->_paths._length), false);

  if (fieldList == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
    return NULL;
  }

  // ..........................................................................  
  // Convert the attributes (field list of the hash index) into strings
  // ..........................................................................  

  for (j = 0; j < hashIndex->_paths._length; ++j) {
    TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(&hashIndex->_paths,j)));
    path = collection->_shaper->lookupAttributePathByPid(collection->_shaper, shape);

    if (path == NULL) {
      TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, fieldList);
      return NULL;
    }  

    fieldList[j] = ((const char*) path) + sizeof(TRI_shape_path_t) + path->_aidLength * sizeof(TRI_shape_aid_t);
  }

  // ..........................................................................  
  // create json object and fill it
  // ..........................................................................  

  json = TRI_CreateArrayJson(TRI_UNKNOWN_MEM_ZONE);

  if (json == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, fieldList);
    return NULL;
  }

  fields = TRI_CreateListJson(TRI_UNKNOWN_MEM_ZONE);

  for (j = 0; j < hashIndex->_paths._length; ++j) {
    TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, fields, TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, fieldList[j]));
  }

  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "id", TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, idx->_iid));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "unique", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, hashIndex->base._unique));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "type", TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, "hash"));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "fields", fields);

  TRI_Free(TRI_UNKNOWN_MEM_ZONE, fieldList);
    
  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a hash index from collection
////////////////////////////////////////////////////////////////////////////////

static void RemoveIndexHashIndex (TRI_index_t* idx, TRI_primary_collection_t* collection) {
}

////////////////////////////////////////////////////////////////////////////////
/// @brief inserts a a document to a hash index
////////////////////////////////////////////////////////////////////////////////

static int InsertHashIndex (TRI_index_t* idx, TRI_doc_mptr_t const* doc) {
  HashIndexElement hashElement;
  TRI_hash_index_t* hashIndex;
  int res;

  // .............................................................................
  // Obtain the hash index structure
  // .............................................................................

  hashIndex = (TRI_hash_index_t*) idx;

  if (idx == NULL) {
    LOG_WARNING("internal error in InsertHashIndex");
    return TRI_set_errno(TRI_ERROR_INTERNAL);
  }

  // .............................................................................
  // Allocate storage to shaped json objects stored as a simple list.
  // These will be used for hashing.
  // .............................................................................
    
  hashElement.fields    = TRI_Allocate(TRI_CORE_MEM_ZONE, sizeof(TRI_shaped_json_t) * hashIndex->_paths._length, false);
  if (hashElement.fields == NULL) {
    LOG_ERROR("out of memory in hashindex");
    return TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
  }

         
  res = HashIndexHelper(hashIndex, &hashElement, doc, NULL);

  // .............................................................................
  // It is possible that this document does not have the necessary attributes
  // (keys) to participate in this index.
  //
  // If an error occurred in the called procedure HashIndexHelper, we must
  // now exit -- and deallocate memory assigned to hashElement.
  // .............................................................................
  
  if (res != TRI_ERROR_NO_ERROR) {
  
    // .............................................................................
    // It may happen that the document does not have the necessary attributes to 
    // be included within the hash index, in this case do not report back an error.
    // .............................................................................
    
    if (res == TRI_WARNING_ARANGO_INDEX_HASH_DOCUMENT_ATTRIBUTE_MISSING) { 
      if (hashIndex->base._unique) {
        TRI_Free(TRI_CORE_MEM_ZONE, hashElement.fields);
        return TRI_ERROR_NO_ERROR;
      }
    }
    else {
      TRI_Free(TRI_CORE_MEM_ZONE, hashElement.fields);
      return res;
    }
  }
  
  // .............................................................................
  // Fill the json field list from the document for unique or non-unique index
  // .............................................................................
  
  if (hashIndex->base._unique) {
    res = HashIndex_insert(hashIndex->_hashIndex, &hashElement);
  }
  else {
    res = MultiHashIndex_insert(hashIndex->_hashIndex, &hashElement);
  }

  // .............................................................................
  // Memory which has been allocated to hashElement.fields remains allocated
  // contents of which are stored in the hash array.
  // .............................................................................
      
  TRI_Free(TRI_CORE_MEM_ZONE, hashElement.fields);
  
  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a document from a hash index
////////////////////////////////////////////////////////////////////////////////

static int RemoveHashIndex (TRI_index_t* idx, TRI_doc_mptr_t const* doc) {
  HashIndexElement hashElement;
  TRI_hash_index_t* hashIndex;
  int res;
  
  // .............................................................................
  // Obtain the hash index structure
  // .............................................................................

  hashIndex = (TRI_hash_index_t*) idx;

  if (idx == NULL) {
    LOG_WARNING("internal error in RemoveHashIndex");
    return TRI_set_errno(TRI_ERROR_INTERNAL);
  } 

  // .............................................................................
  // Allocate some memory for the HashIndexElement structure
  // .............................................................................

  hashElement.fields    = TRI_Allocate(TRI_CORE_MEM_ZONE, sizeof(TRI_shaped_json_t) * hashIndex->_paths._length, false);
  if (hashElement.fields == NULL) {
    LOG_ERROR("out of memory in hashindex");
    return TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
  }

  // .............................................................................
  // Fill the json field list from the document
  // .............................................................................

  res = HashIndexHelper(hashIndex, &hashElement, doc, NULL);

  // .............................................................................
  // It may happen that the document does not have attributes which match
  // For now return internal error, there needs to be its own error number
  // and the appropriate action needs to be taken by the calling function in 
  // such cases.
  // .............................................................................
  
  if (res != TRI_ERROR_NO_ERROR) {
   
    // .............................................................................
    // It may happen that the document does not have the necessary attributes to
    // have particpated within the hash index. In this case, we do not report an
    // error to the calling procedure.
    //
    // TRI_WARNING_ARANGO_INDEX_HASH_DOCUMENT_ATTRIBUTE_MISSING from the called
    // procedure HashIndexHelper implies that we do not propagate the error to
    // the parent function. However for removal we advice the parent
    // function. TODO: return a proper error code.
    // .............................................................................
    
    if (res == TRI_WARNING_ARANGO_INDEX_HASH_DOCUMENT_ATTRIBUTE_MISSING) { 
      if (hashIndex->base._unique) {
        TRI_Free(TRI_CORE_MEM_ZONE, hashElement.fields);
        return TRI_ERROR_NO_ERROR;
      }
    }
    else {
      TRI_Free(TRI_CORE_MEM_ZONE, hashElement.fields);
      return res;
    }
  }
  
  // .............................................................................
  // Attempt the removal for unique or non-unique hash indexes
  // .............................................................................
  
  if (hashIndex->base._unique) {
    res = HashIndex_remove(hashIndex->_hashIndex, &hashElement);
  } 
  else {
    res = MultiHashIndex_remove(hashIndex->_hashIndex, &hashElement);
  }
  
  // .............................................................................
  // Deallocate memory allocated to hashElement.fields above
  // .............................................................................
    
  TRI_Free(TRI_CORE_MEM_ZONE, hashElement.fields);
  
  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief updates a document from a hash index
////////////////////////////////////////////////////////////////////////////////

static int UpdateHashIndex (TRI_index_t* idx,
                            const TRI_doc_mptr_t* newDoc, 
                            const TRI_shaped_json_t* oldDoc) {
                             
  // .............................................................................
  // Note: The oldDoc is represented by the TRI_shaped_json_t rather than by a
  // TRI_doc_mptr_t object. However for non-unique indexes we must pass the
  // document shape to the hash remove function.
  // .............................................................................
  
  union { void* p; void const* c; } cnv;
  HashIndexElement hashElement;
  TRI_hash_index_t* hashIndex;
  int res;  
  
  // .............................................................................
  // Obtain the hash index structure
  // .............................................................................
  
  hashIndex = (TRI_hash_index_t*) idx;

  if (idx == NULL) {
    LOG_WARNING("internal error in UpdateHashIndex");
    return TRI_ERROR_INTERNAL;
  }

  // .............................................................................
  // Allocate some memory for the HashIndexElement structure
  // .............................................................................

  hashElement.fields    = TRI_Allocate(TRI_CORE_MEM_ZONE, sizeof(TRI_shaped_json_t) * hashIndex->_paths._length, false);
  if (hashElement.fields == NULL) {
    LOG_ERROR("out of memory in hashindex");
    return TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
  }
  
  // .............................................................................
  // Update for unique hash index
  //
  // Fill in the fields with the values from oldDoc
  // .............................................................................
  
  assert(oldDoc != NULL);

  res = HashIndexHelper(hashIndex, &hashElement, NULL, oldDoc);
    
  if (res == TRI_ERROR_NO_ERROR) {
    
    // ............................................................................
    // We must fill the hashElement with the value of the document shape -- this
    // is necessary when we attempt to remove non-unique hash indexes.
    // ............................................................................

    cnv.c = newDoc; // we are assuming here that the doc ptr does not change
    hashElement.data = cnv.p;
      
    // ............................................................................
    // Remove the old hash index entry
    // ............................................................................

    if (hashIndex->base._unique) {
      res = HashIndex_remove(hashIndex->_hashIndex, &hashElement); 
    }
    else {
      res = MultiHashIndex_remove(hashIndex->_hashIndex, &hashElement);
    }

    // ..........................................................................
    // This error is common, when a document 'update' occurs, but fails
    // due to the fact that a duplicate entry already exists, when the 'rollback'
    // is applied, there is no document to remove -- so we get this error.
    // ..........................................................................
        
    if (res != TRI_ERROR_NO_ERROR) {
      LOG_DEBUG("could not remove existing document from hash index in UpdateHashIndex");
    }
  }    
    
  else if (res != TRI_WARNING_ARANGO_INDEX_HASH_DOCUMENT_ATTRIBUTE_MISSING) {
    LOG_WARNING("existing document was not removed from hash index in UpdateHashIndex");
  }
    
  // ............................................................................
  // Fill the json simple list from the document
  // ............................................................................

  res = HashIndexHelper(hashIndex, &hashElement, newDoc, NULL);

  // ............................................................................
  // Deal with any errors reported back.
  // ............................................................................

  if (res != TRI_ERROR_NO_ERROR) {
    
    // probably fields do not match. 
    if (res == TRI_WARNING_ARANGO_INDEX_HASH_DOCUMENT_ATTRIBUTE_MISSING) {
      if (hashIndex->base._unique) {
        TRI_Free(TRI_CORE_MEM_ZONE, hashElement.fields);
        return TRI_ERROR_NO_ERROR;
      }
    }
    else {
      TRI_Free(TRI_CORE_MEM_ZONE, hashElement.fields);
      return res;
    }
  }

  // ............................................................................
  // Attempt to add the hash entry from the new doc
  // ............................................................................

  if (hashIndex->base._unique) {
    res = HashIndex_insert(hashIndex->_hashIndex, &hashElement);
  }
  else {
    res = MultiHashIndex_insert(hashIndex->_hashIndex, &hashElement);
  }

  // ............................................................................
  // Deallocate memory given to hashElement.fields
  // ............................................................................
  
  TRI_Free(TRI_CORE_MEM_ZONE, hashElement.fields);
  
  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a hash index
////////////////////////////////////////////////////////////////////////////////

TRI_index_t* TRI_CreateHashIndex (struct TRI_primary_collection_s* collection,
                                  TRI_vector_pointer_t* fields,
                                  TRI_vector_t* paths,
                                  bool unique,
                                  size_t initialDocumentCount) {
  TRI_hash_index_t* hashIndex;
  int result;
  size_t j;

  hashIndex = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_hash_index_t), false);

  if (hashIndex == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
    return NULL;
  }
  
  InitIndex(&hashIndex->base, TRI_IDX_TYPE_HASH_INDEX, collection, unique); 
  hashIndex->base.json = JsonHashIndex;
  hashIndex->base.removeIndex = RemoveIndexHashIndex;

  hashIndex->base.insert = InsertHashIndex;
  hashIndex->base.remove = RemoveHashIndex;
  hashIndex->base.update = UpdateHashIndex;

  // ...........................................................................
  // Copy the contents of the path list vector into a new vector and store this
  // ...........................................................................  

  TRI_InitVector(&hashIndex->_paths, TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shape_pid_t));

  for (j = 0;  j < paths->_length;  ++j) {
    TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(paths,j)));

    TRI_PushBackVector(&hashIndex->_paths, &shape);
  }
  
  TRI_InitVectorString(&hashIndex->base._fields, TRI_UNKNOWN_MEM_ZONE);

  for (j = 0;  j < fields->_length;  ++j) {
    char const* name = fields->_buffer[j];
    char* copy = TRI_DuplicateStringZ(TRI_UNKNOWN_MEM_ZONE, name);

    if (copy == NULL) {
      TRI_DestroyVector(&hashIndex->_paths); 
      TRI_DestroyVectorString(&hashIndex->base._fields); 
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, hashIndex);
      TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

      return NULL;
    }

    TRI_PushBackVectorString(&hashIndex->base._fields, copy);
  }

  if (unique) {
    // create a unique index preallocated for the current number of documents
    hashIndex->_hashIndex = HashIndex_new(hashIndex->_paths._length, initialDocumentCount);
  }
  else {
    // create a non-unique index preallocated for the current number of documents
    hashIndex->_hashIndex = MultiHashIndex_new(hashIndex->_paths._length, initialDocumentCount);
  }  
  
  if (hashIndex->_hashIndex == NULL) { // oops out of memory?
    TRI_DestroyVector(&hashIndex->_paths); 
    TRI_DestroyVectorString(&hashIndex->base._fields); 
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, hashIndex);
    return NULL;
  }
  
  // ...........................................................................
  // Assign the function calls used by the query engine
  // ...........................................................................
  
  result = HashIndex_assignMethod(&(hashIndex->base.indexQuery), TRI_INDEX_METHOD_ASSIGNMENT_QUERY);
  result = result || HashIndex_assignMethod(&(hashIndex->base.indexQueryFree), TRI_INDEX_METHOD_ASSIGNMENT_FREE);
  result = result || HashIndex_assignMethod(&(hashIndex->base.indexQueryResult), TRI_INDEX_METHOD_ASSIGNMENT_RESULT);
  
  if (result != TRI_ERROR_NO_ERROR) {
    TRI_DestroyVector(&hashIndex->_paths); 
    TRI_DestroyVectorString(&hashIndex->base._fields); 
    HashIndex_free(hashIndex->_hashIndex);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, hashIndex);
    LOG_WARNING("hash index creation failed -- internal error when assigning function calls");
    return NULL;
  }
    
  return &hashIndex->base;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated, but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyHashIndex (TRI_index_t* idx) {
  TRI_hash_index_t* hashIndex;

  LOG_TRACE("destroying hash index");
  TRI_DestroyVectorString(&idx->_fields);

  hashIndex = (TRI_hash_index_t*) idx;

  TRI_DestroyVector(&hashIndex->_paths);

  HashIndex_free(hashIndex->_hashIndex);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated and frees the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeHashIndex (TRI_index_t* idx) {
  TRI_DestroyHashIndex(idx);
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, idx);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief free a result set returned by a hash index query
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeResultHashIndex (const TRI_index_t* const idx, 
                              TRI_hash_index_elements_t* const result) {
  TRI_hash_index_t* hashIndex = (TRI_hash_index_t*) idx;

  if (hashIndex->base._unique) {
    HashIndex_freeResult(result);
  }
  else {
    MultiHashIndex_freeResult(result);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief locates entries in the hash index given shaped json objects
////////////////////////////////////////////////////////////////////////////////

TRI_hash_index_elements_t* TRI_LookupShapedJsonHashIndex (TRI_index_t* idx, TRI_shaped_json_t** values) {
  TRI_hash_index_t* hashIndex;
  TRI_hash_index_elements_t* result;
  HashIndexElement element;
  size_t j;
  
  hashIndex = (TRI_hash_index_t*) idx;

  element.fields    = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shaped_json_t) * hashIndex->_paths._length, false);

  if (element.fields == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
    LOG_WARNING("out-of-memory in LookupJsonHashIndex");
    return NULL; 
  }  

  for (j = 0; j < hashIndex->_paths._length; ++j) {
    element.fields[j] = *values[j];
  }
    
  if (hashIndex->base._unique) {
    result = HashIndex_find(hashIndex->_hashIndex, &element);
  }
  else {
    result = MultiHashIndex_find(hashIndex->_hashIndex, &element);
  }

  TRI_Free(TRI_UNKNOWN_MEM_ZONE, element.fields);
  
  return result;  
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                              PRIORITY QUEUE INDEX
// -----------------------------------------------------------------------------



// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief helper for priority queue index
////////////////////////////////////////////////////////////////////////////////

static int PriorityQueueIndexHelper (const TRI_priorityqueue_index_t* pqIndex, 
                                     PQIndexElement* pqElement,
                                     const TRI_doc_mptr_t* document,
                                     const TRI_shaped_json_t* shapedDoc) {
  union { void* p; void const* c; } cnv;
  TRI_shaped_json_t shapedObject;
  TRI_shape_access_t const* acc;
  size_t j;
  
  // ............................................................................
  // TODO: allow documents to be indexed on other keys (attributes) besides
  // doubles. For now, documents which do have an attribute of double, will be
  // be skipped. We return -2 to indicate this.
  // ............................................................................
  
  if (shapedDoc != NULL) {

    // ..........................................................................
    // Attempting to locate a priority queue entry using TRI_shaped_json_t object. 
    // Use this when we wish to remove a priority queue entry and we only have 
    // the "keys" rather than having the document (from which the keys would follow).
    // ..........................................................................
    
    pqElement->data = NULL;

    

    for (j = 0; j < pqIndex->_paths._length; ++j) {
      TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(&pqIndex->_paths,j)));
      
      // ..........................................................................
      // Determine if document has that particular shape 
      // ..........................................................................

      acc = TRI_FindAccessorVocShaper(pqIndex->base._collection->_shaper, shapedDoc->_sid, shape);

      // the attribute does not exist in the document
      if (acc == NULL || acc->_shape == NULL) {
        TRI_Free(TRI_UNKNOWN_MEM_ZONE, pqElement->fields);
        return -1;
      }  
     
      // ..........................................................................
      // Determine if the attribute is of the type double -- if not for now
      // ignore this document
      // ..........................................................................

      if (acc->_shape->_type !=  TRI_SHAPE_NUMBER) {
        TRI_Free(TRI_UNKNOWN_MEM_ZONE, pqElement->fields);
        return -2;    
      }    
    
      // ..........................................................................
      // Extract the field
      // ..........................................................................    

      if (! TRI_ExecuteShapeAccessor(acc, shapedDoc, &shapedObject)) {
        TRI_Free(TRI_UNKNOWN_MEM_ZONE, pqElement->fields);
        return TRI_set_errno(TRI_ERROR_INTERNAL);
      }
      
      // ..........................................................................
      // Store the json shaped Object -- this is what will be hashed
      // ..........................................................................    

      pqElement->fields[j] = shapedObject;
    }
  }
  
  else if (document != NULL) {
  
    // ..........................................................................
    // Assign the document to the PQIndexElement structure - so that it can later
    // be retreived.
    // ..........................................................................

    cnv.c = document;
    pqElement->data = cnv.p;
 
    for (j = 0; j < pqIndex->_paths._length; ++j) {
      TRI_shaped_json_t shapedJson;
      TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(&pqIndex->_paths,j)));
      
      // ..........................................................................
      // Determine if document has that particular shape 
      // It is not an error if the document DOES NOT have the particular shape
      // ..........................................................................

      TRI_EXTRACT_SHAPED_JSON_MARKER(shapedJson, document->_data);
      
      acc = TRI_FindAccessorVocShaper(pqIndex->base._collection->_shaper, shapedJson._sid, shape);

      if (acc == NULL || acc->_shape == NULL) {
        TRI_Free(TRI_UNKNOWN_MEM_ZONE, pqElement->fields);
        return -1;
      }  
      
      // ..........................................................................
      // Determine if the attribute is of the type double -- if not for now
      // ignore this document
      // ..........................................................................

      if (acc->_shape->_type !=  TRI_SHAPE_NUMBER) {
        TRI_Free(TRI_UNKNOWN_MEM_ZONE, pqElement->fields);
        return -2;    
      }    
      
      // ..........................................................................
      // Extract the field
      // ..........................................................................    

      if (! TRI_ExecuteShapeAccessor(acc, &shapedJson, &shapedObject)) {
        TRI_Free(TRI_UNKNOWN_MEM_ZONE, pqElement->fields);
        return TRI_set_errno(TRI_ERROR_INTERNAL);
      }
      
      // ..........................................................................
      // Store the field
      // ..........................................................................    

      pqElement->fields[j] = shapedObject;
    }
  }
  
  else {
    return TRI_set_errno(TRI_ERROR_INTERNAL);
  }
  
  return TRI_ERROR_NO_ERROR;
}



////////////////////////////////////////////////////////////////////////////////
/// @brief attempts to add a document to a priority queue index
////////////////////////////////////////////////////////////////////////////////

static int InsertPriorityQueueIndex (TRI_index_t* idx, TRI_doc_mptr_t const* doc) {
  PQIndexElement pqElement;
  TRI_priorityqueue_index_t* pqIndex;
  int res;

  // ............................................................................
  // Obtain the priority queue index structure
  // ............................................................................

  pqIndex = (TRI_priorityqueue_index_t*) idx;

  if (idx == NULL) {
    LOG_WARNING("internal error in InsertPriorityQueueIndex");
    return TRI_set_errno(TRI_ERROR_INTERNAL);
  }

  // ............................................................................
  // Allocate storage to shaped json objects stored as a simple list.
  // These will be used for adding the document to the priority queue
  // ............................................................................
    
  pqElement.numFields  = pqIndex->_paths._length;
  pqElement.fields     = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shaped_json_t) * pqElement.numFields, false);
  pqElement.collection = pqIndex->base._collection;
  
  
  if (pqElement.fields == NULL) {
    LOG_WARNING("out-of-memory in InsertPriorityQueueIndex");
    return TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
  }  

  res = PriorityQueueIndexHelper(pqIndex, &pqElement, doc, NULL);
  

  // ............................................................................
  // It is possible that this document does not have the necessary attributes
  // (keys) to participate in this index. Skip this document for now.
  // ............................................................................
  
  if (res == -1) {
    return TRI_ERROR_NO_ERROR;
  } 

  
  // ............................................................................
  // It is possible that while we have the correct attribute name, the type is
  // not double. Skip this document for now.  
  // ............................................................................
  
  else if (res == -2) {
    return TRI_ERROR_NO_ERROR;
  } 

  
  // ............................................................................
  // Some other error has occurred - report this error to the calling function
  // ............................................................................
  
  else if (res != TRI_ERROR_NO_ERROR) {
    return res;
  }    
  
  // ............................................................................
  // Attempt to insert document into priority queue index
  // ............................................................................
  
  res = PQIndex_insert(pqIndex->_pqIndex, &pqElement);
  
  if (res == TRI_ERROR_ARANGO_INDEX_PQ_INSERT_FAILED) {
    LOG_WARNING("priority queue insert failure");
  }  
  
  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief describes a priority queue index as a json object
////////////////////////////////////////////////////////////////////////////////

static TRI_json_t* JsonPriorityQueueIndex (TRI_index_t* idx, TRI_primary_collection_t const* collection) {
  TRI_json_t* json;
  TRI_json_t* fields;
  const TRI_shape_path_t* path;
  TRI_priorityqueue_index_t* pqIndex;
  char const** fieldList;
  size_t j;
   
  // ..........................................................................  
  // Recast as a priority queue index
  // ..........................................................................  

  pqIndex = (TRI_priorityqueue_index_t*) idx;

  if (pqIndex == NULL) {
    TRI_set_errno(TRI_ERROR_INTERNAL);
    return NULL;
  }
  
  // ..........................................................................  
  // Allocate sufficent memory for the field list
  // ..........................................................................  

  fieldList = TRI_Allocate( TRI_UNKNOWN_MEM_ZONE, (sizeof(char*) * pqIndex->_paths._length) , false);

  if (fieldList == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
    return NULL;
  }

  // ..........................................................................  
  // Convert the attributes (field list of the hash index) into strings
  // ..........................................................................  

  for (j = 0; j < pqIndex->_paths._length; ++j) {
    TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(&pqIndex->_paths,j)));
    path = collection->_shaper->lookupAttributePathByPid(collection->_shaper, shape);

    if (path == NULL) {
      TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, fieldList);
      return NULL;
    }  

    fieldList[j] = ((const char*) path) + sizeof(TRI_shape_path_t) + path->_aidLength * sizeof(TRI_shape_aid_t);
  }

  // ..........................................................................  
  // create json object and fill it
  // ..........................................................................  

  json = TRI_CreateArrayJson(TRI_UNKNOWN_MEM_ZONE);

  if (json == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, fieldList);
    return NULL;
  }

  fields = TRI_CreateListJson(TRI_UNKNOWN_MEM_ZONE);

  for (j = 0; j < pqIndex->_paths._length; ++j) {
    TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, fields, TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, fieldList[j]));
  }

  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "id", TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, idx->_iid));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "unique", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, pqIndex->base._unique));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "type", TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, TRI_TypeNameIndex(idx)));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "fields", fields);

  TRI_Free(TRI_UNKNOWN_MEM_ZONE, fieldList);
    
  return json;
}


////////////////////////////////////////////////////////////////////////////////
/// @brief removes a priority queue from collection
////////////////////////////////////////////////////////////////////////////////

static void RemoveIndexPriorityQueueIndex (TRI_index_t* idx, TRI_primary_collection_t* collection) {
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a document from a priority queue index
////////////////////////////////////////////////////////////////////////////////

static int RemovePriorityQueueIndex (TRI_index_t* idx, TRI_doc_mptr_t const* doc) {
  PQIndexElement pqElement;
  TRI_priorityqueue_index_t* pqIndex;
  int res;
  
  // ............................................................................
  // Obtain the priority queue index structure
  // ............................................................................

  pqIndex = (TRI_priorityqueue_index_t*) idx;

  if (idx == NULL) {
    LOG_WARNING("internal error in RemovePriorityQueueIndex");
    return TRI_set_errno(TRI_ERROR_INTERNAL);
  }

  // ............................................................................
  // Allocate some memory for the PQIndexElement structure
  // ............................................................................

  pqElement.numFields  = pqIndex->_paths._length;
  pqElement.fields     = TRI_Allocate( TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shaped_json_t) * pqElement.numFields, false);
  pqElement.collection = pqIndex->base._collection;

  if (pqElement.fields == NULL) {
    LOG_WARNING("out-of-memory in RemovePriorityQueueIndex");
    return TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
  }  
  
  // ..........................................................................
  // Fill the json field list from the document
  // ..........................................................................

  res = PriorityQueueIndexHelper(pqIndex, &pqElement, doc, NULL);
  
  
  // ............................................................................
  // It is possible that this document does not have the necessary attributes
  // (keys) to participate in this index. For now report this as an error. Todo,
  // add its own unique error code so that the calling function can take the
  // appropriate action.
  // ............................................................................
  
  if (res == -1) {
    return TRI_set_errno(TRI_ERROR_INTERNAL);
  }
  
  // ............................................................................
  // It is possible that while we have the correct attribute name, the type is
  // not double. Skip this document for now.  
  // ............................................................................
  
  else if (res == -2) {
    return TRI_ERROR_NO_ERROR;
  } 

  
  else if (res != TRI_ERROR_NO_ERROR) {
    return res;
  }    
  
  // ............................................................................
  // Attempt the removal for unique/non-unique priority queue indexes
  // ............................................................................
  
  res = PQIndex_remove(pqIndex->_pqIndex, &pqElement);
  
  return res;
}


////////////////////////////////////////////////////////////////////////////////
/// @brief updates a document from a priority queue index
////////////////////////////////////////////////////////////////////////////////

static int UpdatePriorityQueueIndex (TRI_index_t* idx,
                                     const TRI_doc_mptr_t* newDoc, 
                                     const TRI_shaped_json_t* oldDoc) {
                             
  // ..........................................................................
  // Note: The oldDoc is represented by the TRI_shaped_json_t rather than by
  //       a TRI_doc_mptr_t object. However for non-unique indexes (such as this
  //       one) we must pass the document shape to the hash remove function.
  // ..........................................................................
  
  union { void* p; void const* c; } cnv;
  PQIndexElement pqElement;
  TRI_priorityqueue_index_t* pqIndex;
  int res;  
  
  // ............................................................................
  // Obtain the priority queue index structure
  // ............................................................................
  
  pqIndex = (TRI_priorityqueue_index_t*) idx;

  if (idx == NULL) {
    LOG_WARNING("internal error in UpdatePriorityQueueIndex");
    return TRI_ERROR_INTERNAL;
  }

  // ............................................................................
  // Allocate some memory for the HashIndexElement structure
  // ............................................................................

  pqElement.numFields  = pqIndex->_paths._length;
  pqElement.fields     = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shaped_json_t) * pqElement.numFields, false);
  pqElement.collection = pqIndex->base._collection;

  if (pqElement.fields == NULL) {
    LOG_WARNING("out-of-memory in UpdatePriorityQueueIndex");
    return TRI_ERROR_OUT_OF_MEMORY;
  }  
  

  // ............................................................................
  // Fill in the fields with the values from oldDoc
  // ............................................................................
    
  res = PriorityQueueIndexHelper(pqIndex, &pqElement, NULL, oldDoc);

  if (res == TRI_ERROR_NO_ERROR) {

    cnv.c = newDoc;
    pqElement.data = cnv.p;
      
    // ............................................................................
    // Remove the priority queue index entry and return.
    // ............................................................................

    res = PQIndex_remove(pqIndex->_pqIndex, &pqElement);

    if (res != TRI_ERROR_NO_ERROR) {
      LOG_WARNING("could not remove old document from priority queue index in UpdatePriorityQueueIndex");
    }
  }    

  else {
    LOG_WARNING("could not remove old document from priority queue index in UpdatePriorityQueueIndex");
  }
  
  // ............................................................................
  // Fill the shaped json simple list from the document
  // ............................................................................

  res = PriorityQueueIndexHelper(pqIndex, &pqElement, newDoc, NULL); 

  if (res == -1) {

    // ..........................................................................
    // probably fields do not match  
    // ..........................................................................

    return TRI_ERROR_INTERNAL;
  }    
  
  // ............................................................................
  // It is possible that while we have the correct attribute name, the type is
  // not double. Skip this document for now.  
  // ............................................................................
  
  else if (res == -2) {
    return TRI_ERROR_NO_ERROR;
  } 

  
  else if (res != TRI_ERROR_NO_ERROR) {
    return res;
  }  

  // ............................................................................
  // Attempt to add the priority queue entry from the new doc
  // ............................................................................

  res = PQIndex_insert(pqIndex->_pqIndex, &pqElement);
  
  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a priority queue index
////////////////////////////////////////////////////////////////////////////////

TRI_index_t* TRI_CreatePriorityQueueIndex (struct TRI_primary_collection_s* collection,
                                           TRI_vector_pointer_t* fields,
                                           TRI_vector_t* paths,
                                           bool unique) {
  TRI_priorityqueue_index_t* pqIndex;
  size_t j;

  if (paths == NULL) {
    LOG_WARNING("Internal error in TRI_CreatePriorityQueueIndex. PriorityQueue index creation failed.");
    return NULL;
  }

  // ...........................................................................
  // TODO: Allow priority queue index to be indexed on more than one field. For
  // now report an error.
  // ...........................................................................
  
  if (paths->_length != 1) {
    LOG_WARNING("Currently only one attribute of the tye 'double' can be used for an index. PriorityQueue index creation failed.");
    return NULL;
  }
  
  pqIndex = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_priorityqueue_index_t), false);

  if (pqIndex == NULL) {
    TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);
    return NULL;
  }
  
  InitIndex(&pqIndex->base, TRI_IDX_TYPE_PRIORITY_QUEUE_INDEX, collection, unique); 
  pqIndex->base.json = JsonPriorityQueueIndex;
  pqIndex->base.removeIndex = RemoveIndexPriorityQueueIndex;

  pqIndex->base.insert = InsertPriorityQueueIndex;
  pqIndex->base.remove = RemovePriorityQueueIndex;
  pqIndex->base.update = UpdatePriorityQueueIndex;

  // ...........................................................................
  // Copy the contents of the path list vector into a new vector and store this
  // ...........................................................................  

  TRI_InitVector(&pqIndex->_paths, TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shape_pid_t));

  for (j = 0;  j < paths->_length;  ++j) {
    TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(paths,j)));

    TRI_PushBackVector(&pqIndex->_paths, &shape);
  }
  
  TRI_InitVectorString(&pqIndex->base._fields, TRI_UNKNOWN_MEM_ZONE);

  for (j = 0;  j < fields->_length;  ++j) {
    char const* name = fields->_buffer[j];
    char* copy = TRI_DuplicateStringZ(TRI_UNKNOWN_MEM_ZONE, name);

    if (copy == NULL) {
      TRI_DestroyVector(&pqIndex->_paths);
      TRI_DestroyVectorString(&pqIndex->base._fields);
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, pqIndex);
      TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

      return NULL;
    }

    TRI_PushBackVectorString(&pqIndex->base._fields, copy);
  }

  if (! unique) {
    pqIndex->_pqIndex = PQueueIndex_new();
  }
  else {
    assert(false);
  }  
  
  return &pqIndex->base;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated, but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyPriorityQueueIndex(TRI_index_t* idx) {
  TRI_priorityqueue_index_t* pqIndex;

  if (idx == NULL) {
    return;
  }
  
  TRI_DestroyVectorString(&idx->_fields);

  pqIndex = (TRI_priorityqueue_index_t*) idx;

  TRI_DestroyVector(&pqIndex->_paths);

  PQueueIndex_free(pqIndex->_pqIndex);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated and frees the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_FreePriorityQueueIndex(TRI_index_t* idx) {
  if (idx == NULL) {
    return;
  }  
  TRI_DestroyPriorityQueueIndex(idx);
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, idx);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief attempts to locate an entry in the priority queue index
/// A priority queue index lookup however only allows the 'top' most element
/// of the queue to be located.
///
/// @warning who ever calls this function is responsible for destroying
/// PQIndexElement* result (which could be null)
////////////////////////////////////////////////////////////////////////////////

PQIndexElements* TRI_LookupPriorityQueueIndex(TRI_index_t* idx, TRI_json_t* parameterList) {

  TRI_priorityqueue_index_t* pqIndex;
  PQIndexElements* result;
  size_t           j;
  uint64_t         numElements;
  TRI_json_t*      jsonObject;  
  
  
  if (idx == NULL) {
    return NULL;
  }

  pqIndex = (TRI_priorityqueue_index_t*) idx;
  
  
  // ..............................................................................
  // The parameter list should consist of exactly one parameter of the type number
  // which represents the first 'n' elements on the priority queue. If the 
  // parameter list is empty, then 'n' defaults to 1.
  // ..............................................................................

  if (parameterList == NULL) {  
    result = PQIndex_top(pqIndex->_pqIndex,1);
    return result;  
  }

  
  if (parameterList->_value._objects._length == 0) {
    result = PQIndex_top(pqIndex->_pqIndex,1);
    return result;  
  }
  
  if (parameterList->_value._objects._length != 1) {
    TRI_set_errno(TRI_ERROR_INTERNAL);
    LOG_WARNING("invalid parameter sent to LookupPriorityQueueIndex");
    return NULL;
  }  
  

  numElements = 0;  
    
  for (j = 0; j < parameterList->_value._objects._length; ++j) {
    jsonObject = (TRI_json_t*) (TRI_AtVector(&(parameterList->_value._objects),j));
    if (jsonObject->_type == TRI_JSON_NUMBER) {
      numElements = jsonObject->_value._number;
      break;
    }  
  }

  if (numElements == 0) {
    TRI_set_errno(TRI_ERROR_INTERNAL);
    LOG_WARNING("invalid parameter sent to LookupPriorityQueueIndex - request ignored");
    return NULL;
  }
  
  result = PQIndex_top(pqIndex->_pqIndex,numElements);
  return result;  
  
}


////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////



// -----------------------------------------------------------------------------
// --SECTION--                                                    SKIPLIST INDEX
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////


// .............................................................................
// Helper function for TRI_LookupSkiplistIndex
// .............................................................................


static int FillLookupSLOperator(TRI_index_operator_t* slOperator, TRI_primary_collection_t* collection) {
  TRI_json_t*                    jsonObject;
  TRI_shaped_json_t*             shapedObject;
  TRI_relation_index_operator_t* relationOperator;
  TRI_logical_index_operator_t*  logicalOperator;
  size_t j;
  int result;
  
  if (slOperator == NULL) {
    return TRI_ERROR_INTERNAL;
  }
  
  switch (slOperator->_type) {
    case TRI_AND_INDEX_OPERATOR: 
    case TRI_NOT_INDEX_OPERATOR:
    case TRI_OR_INDEX_OPERATOR: {
    
      logicalOperator = (TRI_logical_index_operator_t*)(slOperator);
      result = FillLookupSLOperator(logicalOperator->_left,collection);
      if (result == TRI_ERROR_NO_ERROR) {
        result = FillLookupSLOperator(logicalOperator->_right,collection);
      }
      if (result != TRI_ERROR_NO_ERROR) {
        return result;
      }        
      break;
    }
    
    case TRI_EQ_INDEX_OPERATOR: 
    case TRI_GE_INDEX_OPERATOR: 
    case TRI_GT_INDEX_OPERATOR: 
    case TRI_NE_INDEX_OPERATOR: 
    case TRI_LE_INDEX_OPERATOR: 
    case TRI_LT_INDEX_OPERATOR: {
    
      relationOperator = (TRI_relation_index_operator_t*)(slOperator);
      relationOperator->_numFields  = relationOperator->_parameters->_value._objects._length;
      relationOperator->_collection = collection;
      relationOperator->_fields     = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shaped_json_t) * relationOperator->_numFields, false);
      if (relationOperator->_fields != NULL) {
        for (j = 0; j < relationOperator->_numFields; ++j) {
          jsonObject   = (TRI_json_t*) (TRI_AtVector(&(relationOperator->_parameters->_value._objects),j));
          shapedObject = TRI_ShapedJsonJson(collection->_shaper, jsonObject);
          if (shapedObject) {
            relationOperator->_fields[j] = *shapedObject; // shallow copy here is ok
            TRI_Free(TRI_UNKNOWN_MEM_ZONE, shapedObject); // don't require storage anymore
          }
        }
      }  
      else {
        relationOperator->_numFields = 0; // out of memory?
      }        
      break;
    }
    
    // .........................................................................
    // This index operator is special
    // The parameters are given to us as a list of json objects for EQ(...),
    // however for the IN(...) operator each parameter in the parameters list
    // is itself a list. For skiplists, the number of parameters is a 
    // decreasing sequence. That is, for a skiplist with 3 attributes,
    // the parameters [ ["a","b","c","d"],["x","y"],[0] ] are allowed, whereas
    // the parameters [ ["a","b","c"], ["x","y"], [0,1,2] ] are not allowed.
    // .........................................................................
    
    case TRI_IN_INDEX_OPERATOR: {
      int maxEntries;
    
      relationOperator = (TRI_relation_index_operator_t*)(slOperator);
      relationOperator->_numFields  = 0;
      relationOperator->_collection = collection;
      relationOperator->_fields     = NULL;
      
      // .......................................................................
      // check that the parameters field is not null
      // .......................................................................

      if (relationOperator->_parameters == NULL) {
        LOG_WARNING("No parameters given when using Skiplist lookup index");
        return TRI_ERROR_INTERNAL;
      }

      
      // .......................................................................
      // check that the parameters json object is of the type list
      // .......................................................................
      
      if (relationOperator->_parameters->_type != TRI_JSON_LIST) {
        LOG_WARNING("Format of parameters given when using Skiplist lookup index are invalid (a)");
        return TRI_ERROR_INTERNAL;
      }
      
      // .......................................................................
      // Each entry in the list is itself a list
      // .......................................................................
      
      relationOperator->_numFields  = relationOperator->_parameters->_value._objects._length;
      relationOperator->_fields     = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shaped_json_t) * relationOperator->_numFields, false);
      if (relationOperator->_fields == NULL) {
        relationOperator->_numFields = 0; // out of memory?
        return TRI_ERROR_OUT_OF_MEMORY;
      }
      
      result     = 0;
      maxEntries = -1;
      
      for (j = 0; j < relationOperator->_numFields; ++j) {
        jsonObject   = (TRI_json_t*) (TRI_AtVector(&(relationOperator->_parameters->_value._objects),j));
        
        if (jsonObject == NULL) {
          result = -1;
          break;
        }  
        
        if (jsonObject->_type != TRI_JSON_LIST) {
          result = -2;
          break;
        }

        // check and see that entries are non-increasing
        if ((int) jsonObject->_value._objects._length > maxEntries) {
          if (maxEntries > 0) {
            result = -3;
            break;
          }
          maxEntries = jsonObject->_value._objects._length;
        }
                
        // convert json to shaped json
        shapedObject = TRI_ShapedJsonJson(collection->_shaper, jsonObject);
        if (shapedObject == NULL) {
          result = -4;
          break;
        }  
      
        // store shaped json list
        relationOperator->_fields[j] = *shapedObject; // shallow copy here is ok
        TRI_Free(TRI_UNKNOWN_MEM_ZONE, shapedObject); // don't require storage anymore
      }
      
      if (result != 0) {
        TRI_Free(TRI_UNKNOWN_MEM_ZONE,relationOperator->_fields);
        relationOperator->_fields = NULL;
        relationOperator->_numFields = 0;
        LOG_WARNING("Format of parameters given when using Skiplist lookup index are invalid (b)");
        return TRI_ERROR_INTERNAL;
      }  
      
    }    
    
    
  }
  
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief attempts to locate an entry in the skip list index
////////////////////////////////////////////////////////////////////////////////

// .............................................................................
// Note: this function will destroy the passed slOperator before it returns
// Warning: who ever calls this function is responsible for destroying
// TRI_skiplist_iterator_t* results
// .............................................................................


TRI_skiplist_iterator_t* TRI_LookupSkiplistIndex(TRI_index_t* idx, TRI_index_operator_t* slOperator) {
  TRI_skiplist_index_t*    skiplistIndex;
  TRI_skiplist_iterator_t* iteratorResult;
  int                      errorResult;
  
  skiplistIndex = (TRI_skiplist_index_t*)(idx);
  
  // .........................................................................
  // fill the relation operators which may be embedded in the slOperator with
  // additional information. Recall the slOperator is what information was
  // received from a user for query the skiplist.
  // .........................................................................
  
  errorResult = FillLookupSLOperator(slOperator, skiplistIndex->base._collection); 
  if (errorResult != TRI_ERROR_NO_ERROR) {
    return NULL;
  }
  
  if (skiplistIndex->base._unique) {
    iteratorResult = SkiplistIndex_find(skiplistIndex->_skiplistIndex, &skiplistIndex->_paths, slOperator);
  }
  else {
    iteratorResult = MultiSkiplistIndex_find(skiplistIndex->_skiplistIndex, &skiplistIndex->_paths, slOperator);
  }

  // .........................................................................
  // we must deallocate any memory we allocated in FillLookupSLOperator
  // .........................................................................
  
  TRI_FreeIndexOperator(slOperator); 
  
  return iteratorResult;  
}

////////////////////////////////////////////////////////////////////////////////
/// @brief helper for skiplist methods
////////////////////////////////////////////////////////////////////////////////

static int SkiplistIndexHelper(const TRI_skiplist_index_t* skiplistIndex, 
                                SkiplistIndexElement* skiplistElement,
                                const TRI_doc_mptr_t* document,
                                const TRI_shaped_json_t* shapedDoc) {
  union { void* p; void const* c; } cnv;
  TRI_shaped_json_t shapedObject;
  TRI_shape_access_t const* acc;
  size_t j;
  
  if (shapedDoc != NULL) {
  
    // ..........................................................................
    // Attempting to locate a entry using TRI_shaped_json_t object. Use this
    // when we wish to remove a entry and we only have the "keys" rather than
    // having the document (from which the keys would follow).
    // ..........................................................................
    
    skiplistElement->data = NULL;
    
 
    for (j = 0; j < skiplistIndex->_paths._length; ++j) {
      TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(&skiplistIndex->_paths,j)));
      
      // ..........................................................................
      // Determine if document has that particular shape 
      // ..........................................................................

      acc = TRI_FindAccessorVocShaper(skiplistIndex->base._collection->_shaper, shapedDoc->_sid, shape);

      if (acc == NULL || acc->_shape == NULL) {
        // TRI_Free(skiplistElement->fields); memory deallocated in the calling procedure
        return TRI_WARNING_ARANGO_INDEX_SKIPLIST_UPDATE_ATTRIBUTE_MISSING;
      }  
      
      
      // ..........................................................................
      // Extract the field
      // ..........................................................................    

      if (! TRI_ExecuteShapeAccessor(acc, shapedDoc, &shapedObject)) {
        // TRI_Free(skiplistElement->fields); memory deallocated in the calling procedure
        return TRI_ERROR_INTERNAL;
      }
      
      // ..........................................................................
      // Store the json shaped Object -- this is what will be hashed
      // ..........................................................................    

      skiplistElement->fields[j] = shapedObject;
    }
  }
  
  else if (document != NULL) {
  
    // ..........................................................................
    // Assign the document to the SkiplistIndexElement structure so that it can 
    // be retrieved later.
    // ..........................................................................
    cnv.c = document;
    skiplistElement->data = cnv.p;
 
    
    for (j = 0; j < skiplistIndex->_paths._length; ++j) {
      TRI_shaped_json_t shapedJson;
      TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(&skiplistIndex->_paths,j)));
      
      TRI_EXTRACT_SHAPED_JSON_MARKER(shapedJson, document->_data);

      // ..........................................................................
      // Determine if document has that particular shape 
      // ..........................................................................

      acc = TRI_FindAccessorVocShaper(skiplistIndex->base._collection->_shaper, shapedJson._sid, shape);

      if (acc == NULL || acc->_shape == NULL) {
        // TRI_Free(skiplistElement->fields); memory deallocated in the calling procedure
        return TRI_WARNING_ARANGO_INDEX_SKIPLIST_DOCUMENT_ATTRIBUTE_MISSING;
      }  
      
      
      // ..........................................................................
      // Extract the field
      // ..........................................................................    

      if (! TRI_ExecuteShapeAccessor(acc, &shapedJson, &shapedObject)) {
        // TRI_Free(skiplistElement->fields); memory deallocated in the calling procedure
        return TRI_ERROR_INTERNAL;
      }

      // ..........................................................................
      // Store the field
      // ..........................................................................    

      skiplistElement->fields[j] = shapedObject;
    }
  }
  
  else {
    return TRI_ERROR_INTERNAL;
  }
  
  return TRI_ERROR_NO_ERROR;
  
} // end of static function SkiplistIndexHelper



////////////////////////////////////////////////////////////////////////////////
/// @brief inserts a document into a skip list index
////////////////////////////////////////////////////////////////////////////////

static int InsertSkiplistIndex (TRI_index_t* idx, TRI_doc_mptr_t const* doc) {

  SkiplistIndexElement skiplistElement;
  TRI_skiplist_index_t* skiplistIndex;
  int res;

  
  // ............................................................................
  // Obtain the skip listindex structure
  // ............................................................................
  
  skiplistIndex = (TRI_skiplist_index_t*) idx;
  if (idx == NULL) {
    LOG_WARNING("internal error in InsertSkiplistIndex");
    return TRI_ERROR_INTERNAL;
  }
  

  // ............................................................................
  // Allocate storage to shaped json objects stored as a simple list.
  // These will be used for comparisions
  // ............................................................................
    
  skiplistElement.numFields   = skiplistIndex->_paths._length;
  skiplistElement.fields      = TRI_Allocate( TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shaped_json_t) * skiplistElement.numFields, false);
  skiplistElement.collection  = skiplistIndex->base._collection;
  
  if (skiplistElement.fields == NULL) {
    LOG_WARNING("out-of-memory in InsertSkiplistIndex");
    return TRI_ERROR_OUT_OF_MEMORY;
  }  
  
  res = SkiplistIndexHelper(skiplistIndex, &skiplistElement, doc, NULL);
    
  
  // ............................................................................
  // most likely the cause of this error is that the 'shape' of the document
  // does not match the 'shape' of the index structure -- so the document
  // is ignored. So not really an error at all.
  // ............................................................................
  
  if (res != TRI_ERROR_NO_ERROR) {
  
    // ..........................................................................
    // Deallocated the memory already allocated to skiplistElement.fields
    // ..........................................................................
      
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, skiplistElement.fields);
    
    
    // ..........................................................................
    // It may happen that the document does not have the necessary attributes to 
    // be included within the hash index, in this case do not report back an error.
    // ..........................................................................
    
    if (res == TRI_WARNING_ARANGO_INDEX_SKIPLIST_DOCUMENT_ATTRIBUTE_MISSING) { 
      return TRI_ERROR_NO_ERROR;
    }
    
    return res;
  }    
  

  // ............................................................................
  // Fill the json field list from the document for unique skiplist index
  // ............................................................................
  
  if (skiplistIndex->base._unique) {
    res = SkiplistIndex_insert(skiplistIndex->_skiplistIndex, &skiplistElement);
  }
  
  // ............................................................................
  // Fill the json field list from the document for non-unique skiplist index
  // ............................................................................
  
  else {
    res = MultiSkiplistIndex_insert(skiplistIndex->_skiplistIndex, &skiplistElement);
  }

  
  // ............................................................................
  // Memory which has been allocated to skiplistElement.fields remains allocated
  // contents of which are stored in the hash array.
  // ............................................................................
      
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, skiplistElement.fields);
  
  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief describes a skiplist index as a json object
////////////////////////////////////////////////////////////////////////////////

static TRI_json_t* JsonSkiplistIndex (TRI_index_t* idx, TRI_primary_collection_t const* collection) {
  TRI_json_t* json;
  TRI_json_t* fields;
  const TRI_shape_path_t* path;
  TRI_skiplist_index_t* skiplistIndex;
  char const** fieldList;
  size_t j;
   
  // ..........................................................................  
  // Recast as a skiplist index
  // ..........................................................................  
  skiplistIndex = (TRI_skiplist_index_t*) idx;
  if (skiplistIndex == NULL) {
    return NULL;
  }

  
  // ..........................................................................  
  // Allocate sufficent memory for the field list
  // ..........................................................................  
  fieldList = TRI_Allocate( TRI_UNKNOWN_MEM_ZONE, (sizeof(char*) * skiplistIndex->_paths._length) , false);
  if (fieldList == NULL) {
    return NULL;
  }


  // ..........................................................................  
  // Convert the attributes (field list of the skiplist index) into strings
  // ..........................................................................  
  for (j = 0; j < skiplistIndex->_paths._length; ++j) {
    TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(&skiplistIndex->_paths,j)));
    path = collection->_shaper->lookupAttributePathByPid(collection->_shaper, shape);
    if (path == NULL) {
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, fieldList);

      return NULL;
    }  
    fieldList[j] = ((const char*) path) + sizeof(TRI_shape_path_t) + path->_aidLength * sizeof(TRI_shape_aid_t);
  }
  

  // ..........................................................................  
  // create json object and fill it
  // ..........................................................................  
  json = TRI_CreateArrayJson(TRI_UNKNOWN_MEM_ZONE);
  if (json == NULL) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, fieldList);

    return NULL;
  }

  fields = TRI_CreateListJson(TRI_UNKNOWN_MEM_ZONE);

  for (j = 0; j < skiplistIndex->_paths._length; ++j) {
    TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, fields, TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, fieldList[j]));
  }

  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "id", TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, idx->_iid));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "unique", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, skiplistIndex->base._unique));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "type", TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, "skiplist"));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "fields", fields);

  TRI_Free(TRI_UNKNOWN_MEM_ZONE, fieldList);
    
  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a skip-list index from collection
////////////////////////////////////////////////////////////////////////////////

static void RemoveIndexSkiplistIndex (TRI_index_t* idx, TRI_primary_collection_t* collection) {
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a document from a skiplist index
////////////////////////////////////////////////////////////////////////////////

static int RemoveSkiplistIndex (TRI_index_t* idx, TRI_doc_mptr_t const* doc) {
  
  SkiplistIndexElement skiplistElement;
  TRI_skiplist_index_t* skiplistIndex;
  int res;
  

  // ............................................................................
  // Obtain the skiplist index structure
  // ............................................................................
  skiplistIndex = (TRI_skiplist_index_t*) idx;
  if (idx == NULL) {
    LOG_WARNING("internal error in RemoveSkiplistIndex");
    return TRI_ERROR_INTERNAL;
  }


  // ............................................................................
  // Allocate some memory for the SkiplistIndexElement structure
  // ............................................................................

  skiplistElement.numFields  = skiplistIndex->_paths._length;
  skiplistElement.fields     = TRI_Allocate( TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shaped_json_t) * skiplistElement.numFields, false);
  skiplistElement.collection = skiplistIndex->base._collection;
  
  if (skiplistElement.fields == NULL) {
    LOG_WARNING("out-of-memory in InsertSkiplistIndex");
    return TRI_ERROR_OUT_OF_MEMORY;
  }  
  
  // ..........................................................................
  // Fill the json field list from the document
  // ..........................................................................
  
  
  res = SkiplistIndexHelper(skiplistIndex, &skiplistElement, doc, NULL);
  
  // ..........................................................................
  // Error returned generally implies that the document never was part of the 
  // skiplist index
  // ..........................................................................
  
  if (res != TRI_ERROR_NO_ERROR) {
  
    // ........................................................................
    // Deallocate memory allocated to skiplistElement.fields above
    // ........................................................................
    
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, skiplistElement.fields);
    
    
    // ........................................................................
    // It may happen that the document does not have the necessary attributes
    // to have particpated within the hash index. In this case, we do not
    // report an error to the calling procedure.
    // ........................................................................
    
    if (res == TRI_WARNING_ARANGO_INDEX_SKIPLIST_DOCUMENT_ATTRIBUTE_MISSING) { 
      return TRI_ERROR_NO_ERROR;
    }
    
    return res;  
  }
  
  
  // ............................................................................
  // Attempt the removal for unique skiplist indexes
  // ............................................................................
  
  if (skiplistIndex->base._unique) {
    res = SkiplistIndex_remove(skiplistIndex->_skiplistIndex, &skiplistElement);
  } 
  
  // ............................................................................
  // Attempt the removal for non-unique skiplist indexes
  // ............................................................................
  
  else {
    res = MultiSkiplistIndex_remove(skiplistIndex->_skiplistIndex, &skiplistElement);
  }
  
  // ............................................................................
  // Deallocate memory allocated to skiplistElement.fields above
  // ............................................................................
    
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, skiplistElement.fields);
  
  return res;
}


static int UpdateSkiplistIndex (TRI_index_t* idx, const TRI_doc_mptr_t* newDoc, 
                                 const TRI_shaped_json_t* oldDoc) {
                             
  // ..........................................................................
  // Note: The oldDoc is represented by the TRI_shaped_json_t rather than by
  //       a TRI_doc_mptr_t object. However for non-unique indexes we must
  //       pass the document shape to the hash remove function.
  // ..........................................................................
  
  union { void* p; void const* c; } cnv;
  SkiplistIndexElement skiplistElement;
  TRI_skiplist_index_t* skiplistIndex;
  int res;  

  
  // ............................................................................
  // Obtain the skiplist index structure
  // ............................................................................
  
  skiplistIndex = (TRI_skiplist_index_t*) idx;
  if (idx == NULL) {
    LOG_WARNING("internal error in UpdateSkiplistIndex");
    return TRI_ERROR_INTERNAL;
  }


  // ............................................................................
  // Allocate some memory for the SkiplistIndexElement structure
  // ............................................................................

  skiplistElement.numFields  = skiplistIndex->_paths._length;
  skiplistElement.fields     = TRI_Allocate( TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shaped_json_t) * skiplistElement.numFields, false);
  skiplistElement.collection = skiplistIndex->base._collection;
  
  if (skiplistElement.fields == NULL) {
    LOG_WARNING("out-of-memory in UpdateHashIndex");
    return TRI_ERROR_OUT_OF_MEMORY;
  }  
      
  
  // ............................................................................
  // Update for unique skiplist index
  // ............................................................................
  
  
  // ............................................................................
  // Fill in the fields with the values from oldDoc
  // ............................................................................
  
  if (skiplistIndex->base._unique) {
  
    res = SkiplistIndexHelper(skiplistIndex, &skiplistElement, NULL, oldDoc);
      
    if (res == TRI_ERROR_NO_ERROR) {
    
      // ............................................................................
      // We must fill the skiplistElement with the value of the document shape -- this
      // is necessary when we attempt to remove non-unique skiplist indexes.
      // ............................................................................
      cnv.c = newDoc; // we are assuming here that the doc ptr does not change
      skiplistElement.data = cnv.p;
      
      
      // ............................................................................
      // Remove the skiplist index entry and return.
      // ............................................................................
      
      res = SkiplistIndex_remove(skiplistIndex->_skiplistIndex, &skiplistElement);
      
      if (res != TRI_ERROR_NO_ERROR) {
      
        // ..........................................................................
        // This error is common, when a document 'update' occurs, but fails
        // due to the fact that a duplicate entry already exists, when the 'rollback'
        // is applied, there is no document to remove -- so we get this error.
        // ..........................................................................
        
        LOG_WARNING("could not remove existing document from skiplist index in UpdateSkiplistIndex");
      }
      
    }    

    // ..............................................................................
    // Here we are assuming that the existing document could not be removed, because
    // the doc did not have the correct attributes. TODO: do not make this assumption.
    // ..............................................................................

    else if (res == TRI_WARNING_ARANGO_INDEX_SKIPLIST_UPDATE_ATTRIBUTE_MISSING) {
    }
    
    // ..............................................................................
    // some other error
    // ..............................................................................
    
    else {
      LOG_WARNING("existing document was not removed from skiplist index in UpdateSkiplistIndex");
    }
    
    
    // ............................................................................
    // Fill the json simple list from the document
    // ............................................................................
    
    res = SkiplistIndexHelper(skiplistIndex, &skiplistElement, newDoc, NULL);
    
    if (res != TRI_ERROR_NO_ERROR) {
    
      // ..........................................................................
      // Deallocated memory given to skiplistElement.fields
      // ..........................................................................
    
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, skiplistElement.fields);
      
      if (res == TRI_WARNING_ARANGO_INDEX_SKIPLIST_DOCUMENT_ATTRIBUTE_MISSING) {
      
        // ........................................................................
        // probably fields do not match. 
        // ........................................................................

        return TRI_ERROR_NO_ERROR;
      }
    
      return res;
    }    


    // ............................................................................
    // Attempt to add the skiplist entry from the new doc
    // ............................................................................
    
    res = SkiplistIndex_insert(skiplistIndex->_skiplistIndex, &skiplistElement);
  }

  
  // ............................................................................
  // Update for non-unique skiplist index
  // ............................................................................

  else {
  
    // ............................................................................
    // Fill in the fields with the values from oldDoc
    // ............................................................................    

    res = SkiplistIndexHelper(skiplistIndex, &skiplistElement, NULL, oldDoc);
    
    if (res == TRI_ERROR_NO_ERROR) {
    
      // ............................................................................
      // We must fill the skiplistElement with the value of the document shape -- this
      // is necessary when we attempt to remove non-unique skiplist indexes.
      // ............................................................................
      
      cnv.c = newDoc;
      skiplistElement.data = cnv.p;
      
      
      // ............................................................................
      // Remove the skiplist index entry and return.
      // ............................................................................
      
      res = MultiSkiplistIndex_remove(skiplistIndex->_skiplistIndex, &skiplistElement);
      
      if (res != TRI_ERROR_NO_ERROR) {
        LOG_WARNING("could not remove old document from (non-unique) skiplist index  UpdateSkiplistIndex");
      }
      
    }    

    else if (res == TRI_WARNING_ARANGO_INDEX_SKIPLIST_UPDATE_ATTRIBUTE_MISSING) {
    }
    
    // ..............................................................................
    // some other error
    // ..............................................................................
    
    else {
      LOG_WARNING("existing document was not removed from (non-unique) skiplist index in UpdateSkiplistIndex");
    }


    
    // ............................................................................
    // Fill the shaped json simple list from the document
    // ............................................................................
    
    res = SkiplistIndexHelper(skiplistIndex, &skiplistElement, newDoc, NULL);
    
    if (res != TRI_ERROR_NO_ERROR) {
    
      // ..........................................................................
      // Deallocated memory given to skiplistElement.fields
      // ..........................................................................
    
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, skiplistElement.fields);
      
      if (res == TRI_WARNING_ARANGO_INDEX_SKIPLIST_DOCUMENT_ATTRIBUTE_MISSING) {
      
        // ........................................................................
        // probably fields do not match. 
        // ........................................................................

        return TRI_ERROR_NO_ERROR;
      }
    
      return res;
    }    


    // ............................................................................
    // Attempt to add the skiplist entry from the new doc
    // ............................................................................
    res = MultiSkiplistIndex_insert(skiplistIndex->_skiplistIndex, &skiplistElement);
    
  }
  
  
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, skiplistElement.fields);  
  
  return res;
}
  
////////////////////////////////////////////////////////////////////////////////
/// @brief creates a skiplist index
////////////////////////////////////////////////////////////////////////////////

TRI_index_t* TRI_CreateSkiplistIndex (struct TRI_primary_collection_s* collection,
                                      TRI_vector_pointer_t* fields,
                                      TRI_vector_t* paths,
                                      bool unique) {
  TRI_skiplist_index_t* skiplistIndex;
  int result;
  size_t j;

  skiplistIndex = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_skiplist_index_t), false);
  if (! skiplistIndex) {
    return NULL;
  }
  
  InitIndex(&skiplistIndex->base, TRI_IDX_TYPE_SKIPLIST_INDEX, collection, unique); 
  skiplistIndex->base.json = JsonSkiplistIndex;
  skiplistIndex->base.removeIndex = RemoveIndexSkiplistIndex;

  skiplistIndex->base.insert = InsertSkiplistIndex;
  skiplistIndex->base.remove = RemoveSkiplistIndex;
  skiplistIndex->base.update = UpdateSkiplistIndex;
  
  // ...........................................................................
  // Copy the contents of the shape list vector into a new vector and store this
  // ...........................................................................  

  TRI_InitVector(&skiplistIndex->_paths, TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shape_pid_t));

  for (j = 0;  j < paths->_length;  ++j) {
    TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(paths,j)));

    TRI_PushBackVector(&skiplistIndex->_paths, &shape);
  }

  TRI_InitVectorString(&skiplistIndex->base._fields, TRI_UNKNOWN_MEM_ZONE);

  for (j = 0;  j < fields->_length;  ++j) {
    char const* name = fields->_buffer[j];
    char* copy = TRI_DuplicateStringZ(TRI_UNKNOWN_MEM_ZONE, name);

    if (copy == NULL) {
      TRI_DestroyVector(&skiplistIndex->_paths);
      TRI_DestroyVectorString(&skiplistIndex->base._fields);
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, skiplistIndex);
      TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

      return NULL;
    }

    TRI_PushBackVectorString(&skiplistIndex->base._fields, copy); 
  }
  
  if (unique) {
    skiplistIndex->_skiplistIndex = SkiplistIndex_new();
  }
  else {
    skiplistIndex->_skiplistIndex = MultiSkiplistIndex_new();
  }  
  
  if (skiplistIndex->_skiplistIndex == NULL) {
    TRI_DestroyVector(&skiplistIndex->_paths);
    TRI_DestroyVectorString(&skiplistIndex->base._fields);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, skiplistIndex);
    LOG_WARNING("skiplist index creation failed -- internal error when creating skiplist structure");
    return NULL;
  }  
  
  
  // ...........................................................................
  // Assign the function calls used by the query engine
  // ...........................................................................
  
  result = SkiplistIndex_assignMethod(&(skiplistIndex->base.indexQuery), TRI_INDEX_METHOD_ASSIGNMENT_QUERY);
  result = result || SkiplistIndex_assignMethod(&(skiplistIndex->base.indexQueryFree), TRI_INDEX_METHOD_ASSIGNMENT_FREE);
  result = result || SkiplistIndex_assignMethod(&(skiplistIndex->base.indexQueryResult), TRI_INDEX_METHOD_ASSIGNMENT_RESULT);
  
  if (result != TRI_ERROR_NO_ERROR) {
    TRI_DestroyVector(&skiplistIndex->_paths);
    TRI_DestroyVectorString(&skiplistIndex->base._fields);
    SkiplistIndex_free(skiplistIndex->_skiplistIndex);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, skiplistIndex);
    LOG_WARNING("skiplist index creation failed -- internal error when assigning function calls");
    return NULL;
  }
    
  return &skiplistIndex->base;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated, but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroySkiplistIndex (TRI_index_t* idx) {
  TRI_skiplist_index_t* sl;

  if (idx == NULL) {
    return;
  }
  
  LOG_TRACE("destroying skiplist index");
  TRI_DestroyVectorString(&idx->_fields);

  sl = (TRI_skiplist_index_t*) idx;
  TRI_DestroyVector(&sl->_paths);

  SkiplistIndex_free(sl->_skiplistIndex);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated and frees the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeSkiplistIndex (TRI_index_t* idx) {
  if (idx == NULL) {
    return;
  }  
  TRI_DestroySkiplistIndex(idx);
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, idx);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    FULLTEXT INDEX
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief callback function called by the fulltext index to determine the
/// words to index for a specific document
////////////////////////////////////////////////////////////////////////////////
  
static TRI_fulltext_wordlist_t* GetWordlist (TRI_index_t* idx, 
                                             const TRI_doc_mptr_t* const document) {
  TRI_fulltext_index_t* fulltextIndex;
  TRI_fulltext_wordlist_t* wordlist;
  TRI_shaped_json_t shaped;
  TRI_shaped_json_t json;
  TRI_shape_t const* shape;
  TRI_doc_mptr_t* doc;
  char* text;
  size_t textLength;
  TRI_vector_string_t* words;
  bool ok;
  
  fulltextIndex = (TRI_fulltext_index_t*) idx;
  doc = (TRI_doc_mptr_t*) ((uintptr_t) document);

  // extract the shape
  TRI_EXTRACT_SHAPED_JSON_MARKER(shaped, doc->_data);
  ok = TRI_ExtractShapedJsonVocShaper(fulltextIndex->base._collection->_shaper, &shaped, 0, fulltextIndex->_attribute, &json, &shape);

  if (! ok || shape == NULL) {
    return NULL;
  }

  // extract the string value for the indexed attribute
  ok = TRI_StringValueShapedJson(shape, &json, &text, &textLength);
  if (! ok) {
    return NULL;
  } 

  // parse the document text
  words = TRI_get_words(text, textLength, (size_t) fulltextIndex->_minWordLength, (size_t) TRI_FULLTEXT_MAX_WORD_LENGTH, true);
  if (words == NULL) {
    return NULL;
  }

  wordlist = TRI_CreateWordlistFulltextIndex(words->_buffer, words->_length);
  if (wordlist == NULL) {
    TRI_FreeVectorString(TRI_UNKNOWN_MEM_ZONE, words);
    return NULL;
  }

  // this really is a hack, but it works well:
  // make the word list vector think it's empty and free it
  // this does not free the word list, that we have already over the result
  words->_length = 0;
  words->_buffer = NULL;
  TRI_FreeVectorString(TRI_UNKNOWN_MEM_ZONE, words);

  return wordlist; 
}

////////////////////////////////////////////////////////////////////////////////
/// @brief inserts a document into the fulltext index
////////////////////////////////////////////////////////////////////////////////

static int InsertFulltextIndex (TRI_index_t* idx, TRI_doc_mptr_t const* doc) {
  TRI_fulltext_index_t* fulltextIndex;
  TRI_fulltext_wordlist_t* wordlist;
  int res;
  
  fulltextIndex = (TRI_fulltext_index_t*) idx;
  if (idx == NULL) {
    LOG_WARNING("internal error in InsertFulltextIndex");
    return TRI_ERROR_INTERNAL;
  }
    
  res = TRI_ERROR_NO_ERROR;

  wordlist = GetWordlist(idx, doc);
  if (wordlist == NULL) {
    // TODO: distinguish the cases "empty wordlist" and "out of memory"
    // LOG_WARNING("could not build wordlist");
    return res;
  }

  if (wordlist->_numWords > 0) {
    // TODO: use status codes
    if (! TRI_InsertWordsFulltextIndex(fulltextIndex->_fulltextIndex, (TRI_fulltext_doc_t) ((uintptr_t) doc), wordlist)) {
      LOG_ERROR("adding document to fulltext index failed");
      res = TRI_ERROR_INTERNAL;
    }
  }

  TRI_FreeWordlistFulltextIndex(wordlist);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief describes a fulltext index as a json object
////////////////////////////////////////////////////////////////////////////////

static TRI_json_t* JsonFulltextIndex (TRI_index_t* idx, TRI_primary_collection_t const* collection) {
  TRI_json_t* json;
  TRI_json_t* fields;
  TRI_fulltext_index_t* fulltextIndex;
  TRI_shape_path_t const* path;
  char const* attributeName;
   
  fulltextIndex = (TRI_fulltext_index_t*) idx;
  if (fulltextIndex == NULL) {
    return NULL;
  }
  
  // convert attribute to string
  path = collection->_shaper->lookupAttributePathByPid(collection->_shaper, fulltextIndex->_attribute);
  if (path == 0) {
    return NULL;
  }
  
  attributeName = ((char const*) path) + sizeof(TRI_shape_path_t) + (path->_aidLength * sizeof(TRI_shape_aid_t));

  json = TRI_CreateArrayJson(TRI_UNKNOWN_MEM_ZONE);
  if (json == NULL) {
    return NULL;
  }

  fields = TRI_CreateListJson(TRI_UNKNOWN_MEM_ZONE);
  TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, fields, TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, attributeName));

  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "id", TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, idx->_iid));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "unique", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, idx->_unique));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "type", TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, "fulltext"));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "indexSubstrings", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, fulltextIndex->_indexSubstrings));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "minLength", TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, (double) fulltextIndex->_minWordLength));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "fields", fields);
    
  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a fulltext index from collection
////////////////////////////////////////////////////////////////////////////////

static void RemoveIndexFulltextIndex (TRI_index_t* idx, TRI_primary_collection_t* collection) {
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a document from a fulltext index
////////////////////////////////////////////////////////////////////////////////

static int RemoveFulltextIndex (TRI_index_t* idx, TRI_doc_mptr_t const* doc) {
  TRI_fulltext_index_t* fulltextIndex;

  fulltextIndex = (TRI_fulltext_index_t*) idx;

  TRI_DeleteDocumentFulltextIndex(fulltextIndex->_fulltextIndex, (TRI_fulltext_doc_t) ((uintptr_t) doc));

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief updates a document in a fulltext index
////////////////////////////////////////////////////////////////////////////////

static int UpdateFulltextIndex (TRI_index_t* idx, 
                                const TRI_doc_mptr_t* newDoc, 
                                const TRI_shaped_json_t* oldDoc) {
  TRI_fulltext_index_t* fulltextIndex;
  TRI_fulltext_wordlist_t* wordlist;
  int res;  
    
  assert(idx);
  
  wordlist = GetWordlist(idx, newDoc);

  res = TRI_ERROR_NO_ERROR;
  fulltextIndex = (TRI_fulltext_index_t*) idx;

  TRI_DeleteDocumentFulltextIndex(fulltextIndex->_fulltextIndex, (TRI_fulltext_doc_t) ((uintptr_t) newDoc));
  
  if (wordlist != NULL && wordlist->_numWords > 0) {
    // TODO: use status codes
    if (! TRI_InsertWordsFulltextIndex(fulltextIndex->_fulltextIndex, (TRI_fulltext_doc_t) ((uintptr_t) newDoc), wordlist)) {
      res = TRI_ERROR_INTERNAL;
    }
  }

  TRI_FreeWordlistFulltextIndex(wordlist);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief cleanup function for the fulltext index
///
/// This will incrementally clean the index by removing document/word pairs
/// for deleted documents
////////////////////////////////////////////////////////////////////////////////

static int CleanupFulltextIndex (TRI_index_t* idx) {
  TRI_fulltext_index_t* fulltextIndex;
  int res;

  LOG_TRACE("fulltext cleanup called");

  fulltextIndex = (TRI_fulltext_index_t*) idx;
  res = TRI_ERROR_NO_ERROR;

  // check whether we should do a cleanup at all
  if (! TRI_CompactFulltextIndex(fulltextIndex->_fulltextIndex)) {
    res = TRI_ERROR_INTERNAL;
  }

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a fulltext index
////////////////////////////////////////////////////////////////////////////////

TRI_index_t* TRI_CreateFulltextIndex (struct TRI_primary_collection_s* collection,
                                      const char* attributeName,
                                      const bool indexSubstrings,
                                      int minWordLength) {
  TRI_fulltext_index_t* fulltextIndex;
  TRI_fts_index_t* fts;
  TRI_shaper_t* shaper;
  char* copy;
  TRI_shape_pid_t attribute;
    
  // look up the attribute
  shaper = collection->_shaper;
  attribute = shaper->findAttributePathByName(shaper, attributeName);

  if (attribute == 0) {
    return NULL;
  }

  copy = TRI_DuplicateStringZ(TRI_UNKNOWN_MEM_ZONE, attributeName);
  if (copy == NULL) {
    return NULL;
  }

  fulltextIndex = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_fulltext_index_t), false);
  if (! fulltextIndex) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, copy);
    return NULL;
  }

  // TODO: indexSubstrings
  // TODO: minWordLength 
  fts = TRI_CreateFtsIndex(2048, 1, 1); 
  if (fts == NULL) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, fulltextIndex);
    return NULL;
  }

  InitIndex(&fulltextIndex->base, TRI_IDX_TYPE_FULLTEXT_INDEX, collection, false); 
  fulltextIndex->base.json = JsonFulltextIndex;
  fulltextIndex->base.removeIndex = RemoveIndexFulltextIndex;

  fulltextIndex->base.insert  = InsertFulltextIndex;
  fulltextIndex->base.remove  = RemoveFulltextIndex;
  fulltextIndex->base.update  = UpdateFulltextIndex;
  fulltextIndex->base.cleanup = CleanupFulltextIndex;
 
  fulltextIndex->_fulltextIndex = fts;
  fulltextIndex->_indexSubstrings = indexSubstrings;
  fulltextIndex->_attribute = attribute;
  fulltextIndex->_minWordLength = (minWordLength > 0 ? minWordLength : 1);
  
  TRI_InitVectorString(&fulltextIndex->base._fields, TRI_UNKNOWN_MEM_ZONE);
  TRI_PushBackVectorString(&fulltextIndex->base._fields, copy); 

  return &fulltextIndex->base;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated, but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyFulltextIndex (TRI_index_t* idx) {
  TRI_fulltext_index_t* fulltextIndex;

  if (idx == NULL) {
    return;
  }

  fulltextIndex = (TRI_fulltext_index_t*) idx;
      
  TRI_DestroyVectorString(&idx->_fields);
  
  LOG_TRACE("destroying fulltext index");

  TRI_FreeFtsIndex(fulltextIndex->_fulltextIndex);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated and frees the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeFulltextIndex (TRI_index_t* idx) {
  if (idx == NULL) {
    return;
  } 
   
  TRI_DestroyFulltextIndex(idx);
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, idx);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    BITARRAY INDEX
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

// .............................................................................
// Helper function for TRI_LookupBitarrayIndex
// .............................................................................

static int FillLookupBitarrayOperator(TRI_index_operator_t* indexOperator, TRI_primary_collection_t* collection) {
  TRI_relation_index_operator_t* relationOperator;
  TRI_logical_index_operator_t*  logicalOperator;
  
  if (indexOperator == NULL) {
    return TRI_ERROR_INTERNAL;
  }
  
  switch (indexOperator->_type) {
    case TRI_AND_INDEX_OPERATOR: 
    case TRI_NOT_INDEX_OPERATOR:
    case TRI_OR_INDEX_OPERATOR: {
    
      logicalOperator = (TRI_logical_index_operator_t*)(indexOperator);
      FillLookupBitarrayOperator(logicalOperator->_left,collection);
      FillLookupBitarrayOperator(logicalOperator->_right,collection);
      break;
    }
    
    case TRI_EQ_INDEX_OPERATOR: 
    case TRI_GE_INDEX_OPERATOR: 
    case TRI_GT_INDEX_OPERATOR: 
    case TRI_NE_INDEX_OPERATOR: 
    case TRI_LE_INDEX_OPERATOR: 
    case TRI_LT_INDEX_OPERATOR: {
    
      relationOperator = (TRI_relation_index_operator_t*)(indexOperator);
      relationOperator->_numFields  = relationOperator->_parameters->_value._objects._length;
      relationOperator->_collection = collection;      
      relationOperator->_fields     = NULL; // bitarray indexes need only the json representation of values
      
      // even tough we use the json representation of the values sent by the client
      // for a bitarray index, we still require the shaped_json values for later
      // if we intend to force a bitarray index to return a result set irrespective
      // of whether the index can do this efficiently, then we will require the shaped json
      // representation of the values to apply any filter condition. Note that
      // for skiplist indexes, we DO NOT use the json representation, rather the shaped json
      // representation of the values is used since for skiplists we are ALWAYS required to
      // go to the document and make comparisons with the document values and the client values
      

      // when you are ready to use the shaped json values -- uncomment the follow
      /*      
      relationOperator->_fields     = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shaped_json_t) * relationOperator->_numFields, false);
      if (relationOperator->_fields != NULL) {
        int j;
        TRI_json_t* jsonObject;
        TRI_shaped_json_t* shapedObject;
        for (j = 0; j < relationOperator->_numFields; ++j) {
          jsonObject   = (TRI_json_t*) (TRI_AtVector(&(relationOperator->_parameters->_value._objects),j));
          shapedObject = TRI_ShapedJsonJson(collection->_shaper, jsonObject);
          if (shapedObject) {
            relationOperator->_fields[j] = *shapedObject; // shallow copy here is ok
            TRI_Free(TRI_UNKNOWN_MEM_ZONE, shapedObject); // don't require storage anymore
          }
        }
      }  
      else {
        relationOperator->_numFields = 0; // out of memory?
      }        
      */
      
      break;
      
    }

    
    // .........................................................................
    // This index operator is special
    // .........................................................................
    
    case TRI_IN_INDEX_OPERATOR: {
      assert(false);
    }  
  }
  
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief attempts to locate an entry in the bitarray index
////////////////////////////////////////////////////////////////////////////////

// .............................................................................
// Note: this function will destroy the passed index operator before it returns
// Warning: who ever calls this function is responsible for destroying
// TRI_index_iterator_t* results
// .............................................................................


TRI_index_iterator_t* TRI_LookupBitarrayIndex(TRI_index_t* idx, 
                                              TRI_index_operator_t* indexOperator, 
                                              bool (*filter) (TRI_index_iterator_t*)) {
  TRI_bitarray_index_t* baIndex;
  TRI_index_iterator_t* iteratorResult;
  int                   errorResult;
  
  baIndex = (TRI_bitarray_index_t*)(idx);
  
  // .........................................................................
  // fill the relation operators which may be embedded in the indexOperator 
  // with additional information. Recall the indexOperator is what information 
  // was received from a client for querying the bitarray index.
  // .........................................................................
  
  errorResult = FillLookupBitarrayOperator(indexOperator, baIndex->base._collection); 
  if (errorResult != TRI_ERROR_NO_ERROR) {
    return NULL;
  }  
  
  
  iteratorResult = BitarrayIndex_find(baIndex->_bitarrayIndex, indexOperator, &baIndex->_paths, idx, NULL);

  TRI_FreeIndexOperator(indexOperator); 
  
  return iteratorResult;  
}

////////////////////////////////////////////////////////////////////////////////
/// @brief helper for bitarray methods
////////////////////////////////////////////////////////////////////////////////

static int BitarrayIndexHelper(const TRI_bitarray_index_t* baIndex, 
                               BitarrayIndexElement* element,
                               const TRI_doc_mptr_t* document,
                               const TRI_shaped_json_t* shapedDoc) {

  union { void* p; void const* c; } cnv;
  TRI_shaped_json_t shapedObject;
  TRI_shape_access_t const* acc;
  size_t j;

  // ...........................................................................
  // For the structure element->fields, memory will have been allocated for this 
  // by the calling procedure -- DO NOT deallocate the memory here -- it is the
  // responsibility of the calling procedure.
  // ...........................................................................
  
        
  if (shapedDoc != NULL) {
  
    // ..........................................................................
    // Attempting to locate a entry using TRI_shaped_json_t object. Use this
    // when we wish to remove a entry and we only have the "keys" rather than
    // having the document (from which the keys would follow).
    // ..........................................................................
    
    element->data = NULL;
    
 
    for (j = 0; j < baIndex->_paths._length; ++j) {
      TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(&baIndex->_paths,j)));
      
      // ..........................................................................
      // Determine if document has that particular shape 
      // ..........................................................................
      
      acc = TRI_FindAccessorVocShaper(baIndex->base._collection->_shaper, shapedDoc->_sid, shape);

      if (acc == NULL || acc->_shape == NULL) {
        return TRI_WARNING_ARANGO_INDEX_BITARRAY_UPDATE_ATTRIBUTE_MISSING;
      }  
      
      
      // ..........................................................................
      // Extract the field
      // ..........................................................................    
      
      if (! TRI_ExecuteShapeAccessor(acc, shapedDoc, &shapedObject)) {
        return TRI_ERROR_INTERNAL;
      }
      
      // ..........................................................................
      // Store the json shaped Object -- this is what will be used by index to
      // whatever it requires to be determined.
      // ..........................................................................    
      
      element->fields[j] = shapedObject;
    }
  }
  
  else if (document != NULL) {
  
    // ..........................................................................
    // Assign the document to the element structure so that it can 
    // be retreived later.
    // ..........................................................................
    
    cnv.c = document;
    element->data = cnv.p;
 
    
    for (j = 0; j < baIndex->_paths._length; ++j) {
      TRI_shaped_json_t shapedJson;
      TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(&baIndex->_paths,j)));
      
      // ..........................................................................
      // Determine if document has that particular shape 
      // ..........................................................................
      
      TRI_EXTRACT_SHAPED_JSON_MARKER(shapedJson, document->_data);
      
      acc = TRI_FindAccessorVocShaper(baIndex->base._collection->_shaper, shapedJson._sid, shape);      

      if (acc == NULL || acc->_shape == NULL) {
        return TRI_WARNING_ARANGO_INDEX_BITARRAY_DOCUMENT_ATTRIBUTE_MISSING;
      }  
      
      // ..........................................................................
      // Extract the field
      // ..........................................................................    
      
      if (! TRI_ExecuteShapeAccessor(acc, &shapedJson, &shapedObject)) {
        return TRI_ERROR_INTERNAL;
      }

      // ..........................................................................
      // Store the field
      // ..........................................................................    

      element->fields[j] = shapedObject;
    }
  }
  
  else {
    return TRI_ERROR_INTERNAL;
  }
  
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief inserts a document into a bitarray list index
////////////////////////////////////////////////////////////////////////////////

static int InsertBitarrayIndex (TRI_index_t* idx, TRI_doc_mptr_t const* doc) {

  BitarrayIndexElement element;
  TRI_bitarray_index_t* baIndex;
  int result;
  
  // ............................................................................
  // Obtain the bitarray index structure
  // ............................................................................
  
  baIndex = (TRI_bitarray_index_t*) idx;
  if (idx == NULL) {
    LOG_WARNING("internal error in InsertBitarrayIndex");
    return TRI_ERROR_INTERNAL;
  }
  


  // ............................................................................
  // Allocate storage to shaped json objects stored as a simple list.
  // These will be used for comparisions
  // ............................................................................
    
  element.numFields   = baIndex->_paths._length;
  element.fields      = TRI_Allocate( TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shaped_json_t) * element.numFields, false);
  element.collection  = baIndex->base._collection;
  
  if (element.fields == NULL) {
    LOG_WARNING("out-of-memory in InsertBitarrayIndex");
    return TRI_ERROR_OUT_OF_MEMORY;
  }  
  
  // ............................................................................
  // For each attribute we have defined in the index obtain its corresponding 
  // value.
  // ............................................................................
  
  result = BitarrayIndexHelper(baIndex, &element, doc, NULL);
  
  // ............................................................................
  // most likely the cause of this error is that the 'shape' of the document
  // does not match the 'shape' of the index structure -- so the document
  // is ignored. 
  // ............................................................................
  
  if (result != TRI_ERROR_NO_ERROR) {
  
    // ..........................................................................
    // Deallocated the memory already allocated to element.fields
    // ..........................................................................
      
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, element.fields);
    element.numFields = 0;
       
    
    // ..........................................................................
    // It may happen that the document does not have the necessary attributes to 
    // be included within the bitarray index, in this case do not report back an error.
    // ..........................................................................
    
    if (result == TRI_WARNING_ARANGO_INDEX_BITARRAY_DOCUMENT_ATTRIBUTE_MISSING) { 
      if (! baIndex->_supportUndef) {
        return TRI_ERROR_NO_ERROR;
      }  

      
      // ........................................................................
      // This insert means that the document does NOT have the index attributes
      // defined, however, we still insert it into aspecial 'undefined' column
      // ........................................................................
      
      result = BitarrayIndex_insert(baIndex->_bitarrayIndex, &element);
    }
    
    return result;
  }    
  
  
  // ............................................................................
  // This insert means that the document has ALL attributes which have been defined
  // in the index. However, it may happen that one or more attribute VALUES are
  // unsupported within the index -- in this case the function below will return
  // an error and insertion of the document is rolled back.
  // ............................................................................
  
  result = BitarrayIndex_insert(baIndex->_bitarrayIndex, &element);
  
  // ............................................................................
  // Since we have allocated memory to element.fields above, we have to deallocate
  // this here.
  // ............................................................................
      
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, element.fields);
  
  return result;
}


////////////////////////////////////////////////////////////////////////////////
/// @brief describes a bitarray index as a json object
////////////////////////////////////////////////////////////////////////////////

static TRI_json_t* JsonBitarrayIndex (TRI_index_t* idx, TRI_primary_collection_t const* collection) {
  TRI_json_t* json;      // the json object we return describing the index
  TRI_json_t* keyValues; // a list of attributes and their associated values
  const TRI_shape_path_t* path;
  TRI_bitarray_index_t* baIndex;
  char const** fieldList;
  size_t j;
   
  
  // ..........................................................................  
  // Recast index as bitarray index
  // ..........................................................................  
  
  baIndex = (TRI_bitarray_index_t*) idx;
  if (baIndex == NULL) {
    return NULL;
  }

  
  // ..........................................................................  
  // Allocate sufficent memory for the field list
  // ..........................................................................  
  
  fieldList = TRI_Allocate( TRI_UNKNOWN_MEM_ZONE, (sizeof(char*) * baIndex->_paths._length) , false);
  if (fieldList == NULL) {
    return NULL;
  }


  // ..........................................................................  
  // Convert the attributes (field list of the bitarray index) into strings
  // ..........................................................................  
  
  for (j = 0; j < baIndex->_paths._length; ++j) {
    TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(&baIndex->_paths,j)));
    path = collection->_shaper->lookupAttributePathByPid(collection->_shaper, shape);
    if (path == NULL) {
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, fieldList);
      return NULL;
    }  
    fieldList[j] = ((const char*) path) + sizeof(TRI_shape_path_t) + path->_aidLength * sizeof(TRI_shape_aid_t);
  }
  

  // ..........................................................................  
  // create the json object representing the index and proceed to fill it
  // ..........................................................................  
  
  json = TRI_CreateArrayJson(TRI_UNKNOWN_MEM_ZONE);
  if (! json) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, fieldList);
    return NULL;
  }


  // ..........................................................................  
  // Create json list which will hold the key value pairs. Assuming that the
  // index is constructed with 3 fields "a","b" % "c", these pairs are stored as follows:
  // [ ["a", [value-a1,...,value-aN]], ["b", [value-b1,...,value-bN]], ["c", [value-c1,...,value-cN]] ]
  // ..........................................................................  
  
  
  // ..........................................................................  
  // Create the key value list 
  // ..........................................................................  
  
  keyValues = TRI_CreateListJson(TRI_UNKNOWN_MEM_ZONE);
  if (! keyValues) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, fieldList);
    return NULL;
  }

  
  for (j = 0; j < baIndex->_paths._length; ++j) {
    TRI_json_t* keyValue;
    TRI_json_t* key;
    TRI_json_t* value;
  
    // ........................................................................
    // Create the list to store the pairs
    // ........................................................................
    
    keyValue = TRI_CreateListJson(TRI_UNKNOWN_MEM_ZONE);
    

    // ........................................................................
    // Create the key json object (copy the string)
    // ........................................................................
    
    key = TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, fieldList[j]);
    
    
    // ........................................................................
    // Create the list of values and fill it from the values stored in the 
    // bit array index structure
    // ........................................................................
    
    value = TRI_CreateListJson(TRI_UNKNOWN_MEM_ZONE);
        
    if (keyValue == NULL || key == NULL || value == NULL) {
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, fieldList);
      return NULL;
    }

    TRI_CopyToJson(TRI_UNKNOWN_MEM_ZONE, value, (TRI_json_t*)(TRI_AtVector(&baIndex->_values,j)));    

    
    // ........................................................................
    // insert the key first followed by the list of values
    // ........................................................................

    TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, keyValue, key);
    TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, keyValue, value);

    
    // ........................................................................
    // insert the key value pair into the list of such pairs
    // ........................................................................
    
    TRI_PushBack3ListJson(TRI_UNKNOWN_MEM_ZONE, keyValues, keyValue);
  }

  
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "id", TRI_CreateNumberJson(TRI_UNKNOWN_MEM_ZONE, idx->_iid));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "unique", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, false));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "type", TRI_CreateStringCopyJson(TRI_UNKNOWN_MEM_ZONE, "bitarray"));
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "fields", keyValues);
  TRI_Insert3ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "undefined", TRI_CreateBooleanJson(TRI_UNKNOWN_MEM_ZONE, baIndex->_supportUndef));

  TRI_Free(TRI_UNKNOWN_MEM_ZONE, fieldList);
    
  return json;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a bitarray index from collection
////////////////////////////////////////////////////////////////////////////////

static void RemoveIndexBitarrayIndex (TRI_index_t* idx, TRI_primary_collection_t* collection) {

}

////////////////////////////////////////////////////////////////////////////////
/// @brief removes a document from a bitarray index
////////////////////////////////////////////////////////////////////////////////

static int RemoveBitarrayIndex (TRI_index_t* idx, TRI_doc_mptr_t const* doc) {
  BitarrayIndexElement element;
  TRI_bitarray_index_t* baIndex;
  int result;
  

  // ............................................................................
  // Obtain the bitarray index structure
  // ............................................................................
  
  baIndex = (TRI_bitarray_index_t*) idx;
  if (idx == NULL) {
    LOG_WARNING("internal error in RemoveBitarrayIndex");
    return TRI_ERROR_INTERNAL;
  }


  // ............................................................................
  // Allocate some memory for the element structure
  // ............................................................................

  element.numFields  = baIndex->_paths._length;
  element.fields     = TRI_Allocate( TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shaped_json_t) * element.numFields, false);
  element.collection = baIndex->base._collection;
  
  if (element.fields == NULL) {
    LOG_WARNING("out-of-memory in RemoveBitarrayIndex");
    return TRI_ERROR_OUT_OF_MEMORY;
  }  

  
  // ..........................................................................
  // Fill the json field list with values from the document 
  // ..........................................................................
    
  result = BitarrayIndexHelper(baIndex, &element, doc, NULL);
  
  // ..........................................................................
  // Error returned generally implies that the document never was part of the 
  // index -- however for a bitarray index we support docs which do not have
  // such an index key(s).
  // ..........................................................................
  
  if (result != TRI_ERROR_NO_ERROR) {

  
    // ........................................................................
    // Check what type of error we received. If 'bad' error, then return
    // ........................................................................

    if (result != TRI_WARNING_ARANGO_INDEX_BITARRAY_DOCUMENT_ATTRIBUTE_MISSING) { 

      // ......................................................................
      // Deallocate memory allocated to element.fields above
      // ......................................................................
    
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, element.fields);
    
      return result;    
    }    

    
    // ........................................................................
    // If we support undefined documents in the index, then pass this on, 
    // otherwise return an error. Note that, eventually it may be slightly
    // more efficient to simply pass these undefined documents straight to
    // the index without using the BitarrayIndexHelper function above.
    // ........................................................................

    if (! baIndex->_supportUndef) {
      // ......................................................................
      // Deallocate memory allocated to element.fields above
      // ......................................................................
    
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, element.fields);
    
      return TRI_ERROR_NO_ERROR;    
    }
    
  }
  
  
  // ............................................................................
  // Attempt to remove document from index
  // ............................................................................
  
  result = BitarrayIndex_remove(baIndex->_bitarrayIndex, &element);


  // ............................................................................
  // Deallocate memory allocated to element.fields above
  // ............................................................................
    
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, element.fields);
  
  return result;
}


static int UpdateBitarrayIndex (TRI_index_t* idx, const TRI_doc_mptr_t* newDoc, 
                                const TRI_shaped_json_t* oldDoc) {

  union { void* p; void const* c; } cnv;
                            
  // ..........................................................................
  // Note: The oldDoc is represented by the TRI_shaped_json_t rather than by
  //       a TRI_doc_mptr_t object. 
  // ..........................................................................
  
  BitarrayIndexElement element;
  TRI_bitarray_index_t* baIndex;
  int result;  

  
  // ............................................................................
  // Obtain the bitarray index structure
  // ............................................................................
  
  baIndex = (TRI_bitarray_index_t*) idx;
  if (idx == NULL) {
    LOG_WARNING("internal error in UpdateBitarrayIndex");
    return TRI_ERROR_INTERNAL;
  }


  // ............................................................................
  // Allocate some memory for the element structure
  // ............................................................................

  element.numFields  = baIndex->_paths._length;
  element.fields     = TRI_Allocate( TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shaped_json_t) * element.numFields, false);
  element.collection = baIndex->base._collection;
  
  if (element.fields == NULL) {
    LOG_WARNING("out-of-memory in UpdateBitarrayIndex");
    return TRI_ERROR_OUT_OF_MEMORY;
  }  
      
  
  // ............................................................................
  // Fill in the fields with the values from oldDoc
  // ............................................................................    

  result = BitarrayIndexHelper(baIndex, &element, NULL, oldDoc);
  

  // ............................................................................
  // Problems, problems, problems. Could be the new doc does not have the 
  // attributes to be inserted into the index, or perhaps the old doc was never
  // there.
  // ............................................................................
  
  if (result != TRI_ERROR_NO_ERROR) {  
    if (result != TRI_WARNING_ARANGO_INDEX_BITARRAY_UPDATE_ATTRIBUTE_MISSING) {
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, element.fields);
      LOG_WARNING("error returned when an attempt to update bitarray index");
      return result;
    }
  }
  

  // ............................................................................
  // Remove the old entry in the index -- we do not require the oldDoc
  // ............................................................................
  
  cnv.c = newDoc;
  element.data = cnv.p;
  result = BitarrayIndex_remove(baIndex->_bitarrayIndex, &element);
  
  if (result != TRI_ERROR_NO_ERROR) {
    if (result != TRI_WARNING_ARANGO_INDEX_BITARRAY_REMOVE_ITEM_MISSING) {
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, element.fields);
      LOG_WARNING("error returned during removal phase when an attempt to update bitarray index");
      return result;
    }
  }


  // ............................................................................
  // Fill in the fields with the values from newDoc
  // ............................................................................    

  result = BitarrayIndexHelper(baIndex, &element, newDoc, NULL);
  
  if (result != TRI_ERROR_NO_ERROR) {
  
    // ..........................................................................
    // Deallocated the memory already allocated to element.fields
    // ..........................................................................
      
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, element.fields);
    element.numFields = 0;
       
    
    // ..........................................................................
    // It may happen that the document does not have the necessary attributes to 
    // be included within the bitarray index, in this case do not report back an error.
    // ..........................................................................
    
    if (result == TRI_WARNING_ARANGO_INDEX_BITARRAY_DOCUMENT_ATTRIBUTE_MISSING) { 
      if (! baIndex->_supportUndef) {
        return TRI_ERROR_NO_ERROR;
      }  
      result = BitarrayIndex_insert(baIndex->_bitarrayIndex, &element);
    }
    return result;
  }    
  
  
  result = BitarrayIndex_insert(baIndex->_bitarrayIndex, &element);
  
  // ............................................................................
  // Since we have allocated memory to element.fields above, we have to deallocate
  // this here.
  // ............................................................................
      
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, element.fields);
  return result;
}
  

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a bitarray index
////////////////////////////////////////////////////////////////////////////////

TRI_index_t* TRI_CreateBitarrayIndex (struct TRI_primary_collection_s* collection,
                                      TRI_vector_pointer_t* fields,
                                      TRI_vector_t* paths,
                                      TRI_vector_pointer_t* values,
                                      bool supportUndef, int* errorNum, char** errorStr) {
  TRI_bitarray_index_t* baIndex;
  size_t i,j,k;
  int result;
  void* createContext;
  int cardinality;
  

  // ...........................................................................
  // Before we start moving things about, ensure that the attributes have 
  // not been repeated
  // ...........................................................................
  
  for (j = 0;  j < paths->_length;  ++j) {
    TRI_shape_pid_t* leftShape = (TRI_shape_pid_t*)(TRI_AtVector(paths,j));
    for (i = j + 1; i < paths->_length;  ++i) {
      TRI_shape_pid_t* rightShape = (TRI_shape_pid_t*)(TRI_AtVector(paths,i));
      if (*leftShape == *rightShape) {
        LOG_WARNING("bitarray index creation failed -- duplicate keys in index");
        *errorNum = TRI_ERROR_ARANGO_INDEX_BITARRAY_CREATION_FAILURE_DUPLICATE_ATTRIBUTES;
        *errorStr = TRI_DuplicateString("bitarray index creation failed -- duplicate keys in index");
        return NULL;
      }
    }
  }  

  
  
  // ...........................................................................
  // For each key (attribute) ensure that the list of supported values are 
  // unique
  // ...........................................................................

  for (k = 0;  k < paths->_length;  ++k) {
    TRI_json_t* valueList = (TRI_json_t*)(TRI_AtVectorPointer(values,k));
    
    if (valueList == NULL || valueList->_type != TRI_JSON_LIST) {    
      LOG_WARNING("bitarray index creation failed -- list of values for index undefined");
      *errorNum = TRI_ERROR_ILLEGAL_OPTION;
      *errorStr = TRI_DuplicateString("bitarray index creation failed -- list of values for index undefined");
      return NULL;
    } 
    
    for (j = 0; j < valueList->_value._objects._length; ++j) {
      TRI_json_t* leftValue = (TRI_json_t*)(TRI_AtVector(&(valueList->_value._objects), j));            
      for (i = j + 1; i < valueList->_value._objects._length; ++i) {
        TRI_json_t* rightValue = (TRI_json_t*)(TRI_AtVector(&(valueList->_value._objects), i));            
        if (TRI_EqualJsonJson(leftValue, rightValue)) {
          LOG_WARNING("bitarray index creation failed -- duplicate values in value list for an attribute");          
          *errorNum = TRI_ERROR_ARANGO_INDEX_BITARRAY_CREATION_FAILURE_DUPLICATE_VALUES;
          *errorStr = TRI_DuplicateString("bitarray index creation failed -- duplicate values in value list for an attribute");
          return NULL;
        }
      }
    }
  }


  
  // ...........................................................................
  // attempt to allocate memory for the bit array index structure
  // ...........................................................................
  
  baIndex = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_bitarray_index_t), false);
  if (baIndex == NULL) {
    LOG_WARNING("bitarray index creation failed -- out of memory");
    return NULL;
  }
  
  InitIndex(&baIndex->base, TRI_IDX_TYPE_BITARRAY_INDEX, collection, false); 
  baIndex->base.json        = JsonBitarrayIndex;
  baIndex->base.removeIndex = RemoveIndexBitarrayIndex;

  baIndex->base.insert      = InsertBitarrayIndex;
  baIndex->base.remove      = RemoveBitarrayIndex;
  baIndex->base.update      = UpdateBitarrayIndex;
    
  baIndex->_supportUndef    = supportUndef;
  baIndex->_bitarrayIndex   = NULL;

  
  // ...........................................................................
  // Copy the contents of the shape list vector into a new vector and store this
  // Do the same for the values associated with the attributes
  // ...........................................................................  

  TRI_InitVector(&baIndex->_paths, TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_shape_pid_t));
  TRI_InitVector(&baIndex->_values, TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_json_t));

  for (j = 0;  j < paths->_length;  ++j) {
    TRI_json_t value;
    TRI_shape_pid_t shape = *((TRI_shape_pid_t*)(TRI_AtVector(paths,j)));
    TRI_PushBackVector(&baIndex->_paths, &shape);
    TRI_CopyToJson(TRI_UNKNOWN_MEM_ZONE, &value, (TRI_json_t*)(TRI_AtVectorPointer(values,j)));
    TRI_PushBackVector(&baIndex->_values, &value);
  }


  // ...........................................................................
  // Store the list of fields (attributes based on the paths above) as simple
  // c strings - saves us looking these up at a latter stage
  // ...........................................................................
  
  TRI_InitVectorString(&baIndex->base._fields, TRI_UNKNOWN_MEM_ZONE);

  for (j = 0;  j < fields->_length;  ++j) {
    char const* name = fields->_buffer[j];
    char* copy = TRI_DuplicateStringZ(TRI_UNKNOWN_MEM_ZONE, name);

    if (copy == NULL) {
      TRI_DestroyVector(&baIndex->_values);
      TRI_DestroyVector(&baIndex->_paths);
      TRI_DestroyVectorString(&baIndex->base._fields);
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, baIndex);
      TRI_set_errno(TRI_ERROR_OUT_OF_MEMORY);

      return NULL;
    }

    TRI_PushBackVectorString(&baIndex->base._fields, copy);
  }
  
  
  // ...........................................................................
  // Currently there is no creation context -- todo later
  // ...........................................................................
  
  createContext = NULL;
  

  // ...........................................................................
  // Check that the attributes have not been repeated
  // ...........................................................................
  
  // ...........................................................................
  // Determine the cardinality of the Bitarray index (that is, the number of 
  // columns which constitute the index)
  // ...........................................................................
  
  cardinality = 0;
  
  for (j = 0;  j < paths->_length;  ++j) {
    TRI_json_t* value = (TRI_json_t*)(TRI_AtVector(&baIndex->_values,j));
    size_t numValues;

    if (value == NULL) {    
      TRI_DestroyVector(&baIndex->_paths);
      TRI_DestroyVector(&baIndex->_values);
      TRI_Free(TRI_UNKNOWN_MEM_ZONE,baIndex);
      LOG_WARNING("bitarray index creation failed -- list of values for index undefined");
      return NULL;
    }      
       
    numValues = value->_value._objects._length;
    
    
    // .........................................................................
    // value is a list json type -- the number of entries informs us how many
    // different possible values there are
    // .........................................................................
    
    cardinality += numValues;
  
  }

  
  // ...........................................................................
  // for the moment we restrict the cardinality to 64
  // ...........................................................................
    
  if (cardinality > 64) {
    TRI_DestroyVector(&baIndex->_paths);
    TRI_DestroyVector(&baIndex->_values);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE,baIndex);
    LOG_WARNING("bitarray index creation failed -- more than 64 possible values");
    return NULL;
  }
    
    
  if (cardinality < 1 ) {
    TRI_DestroyVector(&baIndex->_paths);
    TRI_DestroyVector(&baIndex->_values);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE,baIndex);
    LOG_WARNING("bitarray index creation failed -- no index values defined");
    return NULL;
  }
    
 

  // ...........................................................................
  // Assign the function calls used by the query engine
  // ...........................................................................
  
  result = BittarrayIndex_assignMethod(&(baIndex->base.indexQuery), TRI_INDEX_METHOD_ASSIGNMENT_QUERY);
  result = result || BittarrayIndex_assignMethod(&(baIndex->base.indexQueryFree), TRI_INDEX_METHOD_ASSIGNMENT_FREE);
  result = result || BittarrayIndex_assignMethod(&(baIndex->base.indexQueryResult), TRI_INDEX_METHOD_ASSIGNMENT_RESULT);
  
  if (result != TRI_ERROR_NO_ERROR) {
    TRI_DestroyVector(&baIndex->_paths);
    TRI_DestroyVector(&baIndex->_values);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE,baIndex);
    LOG_WARNING("bitarray index creation failed -- internal error when assigning function calls");
    return NULL;
  }
    
  
  // ...........................................................................
  // attempt to create a new bitarray index
  // ...........................................................................
   
  result = BitarrayIndex_new(&(baIndex->_bitarrayIndex), TRI_UNKNOWN_MEM_ZONE, (size_t) cardinality, 
                             &baIndex->_values, supportUndef, createContext);  
  if (result != TRI_ERROR_NO_ERROR) {
    TRI_DestroyVector(&baIndex->_paths);
    TRI_DestroyVector(&baIndex->_values);
    TRI_FreeBitarrayIndex(&baIndex->base);
    TRI_Free(TRI_UNKNOWN_MEM_ZONE,baIndex);
    LOG_WARNING("bitarray index creation failed -- your guess as good as mine");
    return NULL;
  }  
  
  return &baIndex->base;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated, but does not free the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_DestroyBitarrayIndex (TRI_index_t* idx) {
  
  TRI_bitarray_index_t* baIndex;
  size_t j;
  
  
  if (idx == NULL) {
    return;
  }
  
  LOG_TRACE("destroying bitarray index");
  TRI_DestroyVectorString(&idx->_fields);

  baIndex = (TRI_bitarray_index_t*) idx;
  for (j = 0;  j < baIndex->_values._length;  ++j) {
    TRI_DestroyJson(TRI_UNKNOWN_MEM_ZONE, (TRI_json_t*)(TRI_AtVector(&(baIndex->_values),j)));
  }
  TRI_DestroyVector(&baIndex->_paths);
  TRI_DestroyVector(&baIndex->_values);
  BitarrayIndex_free(baIndex->_bitarrayIndex);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief frees the memory allocated and frees the pointer
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeBitarrayIndex (TRI_index_t* idx) {
  if (idx == NULL) {
    return;
  }  
  TRI_DestroyBitarrayIndex(idx);
  TRI_Free(TRI_UNKNOWN_MEM_ZONE, idx);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:
