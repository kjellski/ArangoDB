////////////////////////////////////////////////////////////////////////////////
/// @brief transaction subsystem
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
/// @author Jan Steemann
/// @author Copyright 2012, triagens GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "transaction.h" 

#include "BasicsC/logging.h" 
#include "BasicsC/strings.h"
 
#include "VocBase/vocbase.h"

// -----------------------------------------------------------------------------
// --SECTION--                                               TRANSACTION CONTEXT
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief hash a collection list
////////////////////////////////////////////////////////////////////////////////

static uint64_t HashCollection (TRI_associative_pointer_t* array, 
                                void const* element) {
  TRI_transaction_context_collection_t* collection = (TRI_transaction_context_collection_t*) element;

  return TRI_FnvHashString(collection->_name);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief comparison function used to determine collection list equality
////////////////////////////////////////////////////////////////////////////////

static bool IsEqualCollectionName (TRI_associative_pointer_t* array, 
                                   void const* key, 
                                   void const* element) {
  TRI_transaction_context_collection_t* collection = (TRI_transaction_context_collection_t*) element;

  return TRI_EqualString(key, collection->_name);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the type of the transaction as a string
////////////////////////////////////////////////////////////////////////////////

static const char* TypeString (const TRI_transaction_type_e type) {
  switch (type) {
    case TRI_TRANSACTION_READ:
      return "read";
    case TRI_TRANSACTION_WRITE:
      return "write";
  }

  assert(false);
  return "unknown";
}

////////////////////////////////////////////////////////////////////////////////
/// @brief return the status of the transaction as a string
////////////////////////////////////////////////////////////////////////////////

static const char* StatusString (const TRI_transaction_status_e status) {
  switch (status) {
    case TRI_TRANSACTION_CREATED:
      return "created";
    case TRI_TRANSACTION_RUNNING:
      return "running";
    case TRI_TRANSACTION_COMMITTED:
      return "committed";
    case TRI_TRANSACTION_ABORTED:
      return "aborted";
    case TRI_TRANSACTION_FINISHED:
      return "finished";
    case TRI_TRANSACTION_FAILED:
      return "failed";
  }

  assert(false);
  return "unknown";
}

////////////////////////////////////////////////////////////////////////////////
/// @brief generate a transaction id
/// The context lock must be held when calling this function
////////////////////////////////////////////////////////////////////////////////

static TRI_transaction_local_id_t NextLocalTransactionId (TRI_transaction_context_t* const context) {
  TRI_transaction_local_id_t id;

  id = ++context->_id._localId;
  return (TRI_transaction_local_id_t) id;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief register a transaction in the global transactions list
/// The context lock must be held when calling this function
////////////////////////////////////////////////////////////////////////////////

static int InsertTransactionList (TRI_transaction_list_t* const list, 
                                  TRI_transaction_t* const trx) {
  const TRI_transaction_local_id_t id = trx->_id._localId; 
  int res;
  TRI_transaction_list_entry_t entry;

  assert(trx->_status == TRI_TRANSACTION_RUNNING);

  entry._id = id;
  entry._status = TRI_TRANSACTION_RUNNING;

  res = TRI_PushBackVector(&list->_vector, &entry);
  if (res == TRI_ERROR_NO_ERROR) {
    ++list->_numRunning;
  }

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief locate a transaction in the global transactions list using a
/// binary search
/// The context lock must be held when calling this function
////////////////////////////////////////////////////////////////////////////////

static TRI_transaction_list_entry_t* FindTransactionList (const TRI_transaction_list_t* const list,
                                                          const TRI_transaction_local_id_t id,
                                                          size_t* position) {

  TRI_transaction_list_entry_t* entry;
  size_t l, r, n;
    
  LOG_TRACE("looking up transaction %lu", (unsigned long) id);

  n = list->_vector._length;
  if (n == 0) {
    return NULL;
  }

  l = 0;
  r = n - 1;
  while (true) {
    const size_t i = l + ((r - l) / 2);

    if (r < l) {
      // transaction not found
      return NULL;
    }

    entry = (TRI_transaction_list_entry_t*) TRI_AtVector(&list->_vector, i);
    assert(entry);

    if (entry->_id == id) {
      // found the transaction
      *position = i;

      return entry;
    }

    if (entry->_id > id) {
      r = i - 1;
    }
    else {
      l = i + 1;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief remove a transaction from the global transactions list
/// The context lock must be held when calling this function
////////////////////////////////////////////////////////////////////////////////

static int RemoveTransactionList (TRI_transaction_list_t* const list, 
                                  const TRI_transaction_local_id_t id) {
  TRI_transaction_list_entry_t* entry;
  size_t position;

  entry = FindTransactionList(list, id, &position);
  if (entry == NULL) {
    // transaction not found, should not happen
    LOG_ERROR("logical error in transaction list");
    return TRI_ERROR_INTERNAL;
  }

  assert(entry);

  // update counters
  if (entry->_status == TRI_TRANSACTION_RUNNING) {
    --list->_numRunning;
  }
  else {
    LOG_ERROR("logical error in transaction list");
    assert(false);
  }

  // remove it from the vector
  TRI_RemoveVector(&list->_vector, position);

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief remove a transaction from the global transactions list
/// The context lock must be held when calling this function
////////////////////////////////////////////////////////////////////////////////

static int UpdateTransactionList (TRI_transaction_list_t* const list, 
                                  const TRI_transaction_local_id_t id,
                                  const TRI_transaction_status_e status) {
  TRI_transaction_list_entry_t* entry;
  size_t position;

  assert(status == TRI_TRANSACTION_ABORTED);

  entry = FindTransactionList(list, id, &position);
  if (entry == NULL) {
    // transaction not found, should not happen
    LOG_ERROR("logical error in transaction list");
    return TRI_ERROR_INTERNAL;
  }

  // update counters
  if (entry->_status == TRI_TRANSACTION_RUNNING) {
    --list->_numRunning;
  }
  else {
    LOG_ERROR("logical error in transaction list");
  }

  if (status == TRI_TRANSACTION_ABORTED) {
    ++list->_numAborted;
  }

  entry->_status = status;
  
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief initialise a transactions list
////////////////////////////////////////////////////////////////////////////////

static void InitTransactionList (TRI_transaction_list_t* const list) {
  TRI_InitVector(&list->_vector, TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_transaction_list_entry_t));
  list->_numRunning = 0;
  list->_numAborted = 0;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy a transactions list
////////////////////////////////////////////////////////////////////////////////

static void DestroyTransactionList (TRI_transaction_list_t* const list) {
  TRI_DestroyVector(&list->_vector);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief dump the contents of a transaction list
////////////////////////////////////////////////////////////////////////////////

static void DumpTransactionList (const TRI_transaction_list_t* const list) {
  size_t i, n;

  n = list->_vector._length;
  for (i = 0; i < n; ++i) {
    TRI_transaction_list_entry_t* entry = TRI_AtVector(&list->_vector, i);

    LOG_INFO("- trx: #%lu, status: %s", 
             (unsigned long) entry->_id,
             StatusString(entry->_status));
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create a collection for the global context
////////////////////////////////////////////////////////////////////////////////

TRI_transaction_context_collection_t* CreateCollectionTransactionContext (TRI_transaction_collection_t* const collection) {
  TRI_transaction_context_collection_t* globalCollection;

  globalCollection = TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_transaction_context_collection_t), false);
  if (globalCollection == NULL) {
    return NULL;
  }

  globalCollection->_name = TRI_DuplicateStringZ(TRI_UNKNOWN_MEM_ZONE, collection->_name);
  if (globalCollection->_name == NULL) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, globalCollection);

    return NULL;
  }

  TRI_InitMutex(&globalCollection->_writeLock);
  InitTransactionList(&globalCollection->_writeTransactions);

  return globalCollection;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free a collection from the global context
////////////////////////////////////////////////////////////////////////////////

void FreeCollectionTransactionContext (TRI_transaction_context_collection_t* const globalCollection) {
  DestroyTransactionList(&globalCollection->_writeTransactions);
  TRI_DestroyMutex(&globalCollection->_writeLock);

  TRI_Free(TRI_UNKNOWN_MEM_ZONE, globalCollection);
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
/// @brief create the global transaction context
////////////////////////////////////////////////////////////////////////////////

TRI_transaction_context_t* TRI_CreateTransactionContext (TRI_vocbase_t* const vocbase,
                                                         TRI_transaction_server_id_t serverId) {
  TRI_transaction_context_t* context;

  context = (TRI_transaction_context_t*) TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_transaction_context_t), false);
  if (context == NULL) {
    return context;
  }

  TRI_InitMutex(&context->_lock);

  InitTransactionList(&context->_readTransactions);
  InitTransactionList(&context->_writeTransactions);
  TRI_InitAssociativePointer(&context->_collections,
                             TRI_UNKNOWN_MEM_ZONE,
                             TRI_HashStringKeyAssociativePointer, 
                             HashCollection,
                             IsEqualCollectionName, 
                             NULL);

  context->_vocbase = vocbase;
  context->_id._localId  = 0;
  context->_id._serverId = serverId;

  return context;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free the global transaction context
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeTransactionContext (TRI_transaction_context_t* const context) {
  DestroyTransactionList(&context->_writeTransactions);
  DestroyTransactionList(&context->_readTransactions);
  TRI_DestroyAssociativePointer(&context->_collections);

  TRI_DestroyMutex(&context->_lock);

  TRI_Free(TRI_UNKNOWN_MEM_ZONE, context);
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
/// @brief free all data associated with a specific collection
/// TODO: this function must be called for all collections that are dropped
////////////////////////////////////////////////////////////////////////////////

void TRI_RemoveCollectionTransactionContext (TRI_transaction_context_t* const context, 
                                             const char* const name) {
  TRI_transaction_context_collection_t* collection;

  // start critical section -----------------------------------------
  TRI_LockMutex(&context->_lock); 
    
  // TODO: must not remove collections with transactions going on
  collection = (TRI_transaction_context_collection_t*) TRI_RemoveKeyAssociativePointer(&context->_collections, name);

  TRI_UnlockMutex(&context->_lock); 
  // end critical section -----------------------------------------

  if (collection != NULL) {
    FreeCollectionTransactionContext(collection);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief dump transaction context data
////////////////////////////////////////////////////////////////////////////////

void TRI_DumpTransactionContext (TRI_transaction_context_t* const context) {
  TRI_LockMutex(&context->_lock);

  LOG_INFO("transaction context, last-id: %lu:%lu", 
           (unsigned long) context->_id._serverId, 
           (unsigned long) context->_id._localId);
  
  LOG_INFO("read transactions: numRunning: %lu, length: %lu, aborted: %lu", 
            (unsigned long) context->_readTransactions._vector._length,
            (unsigned long) context->_readTransactions._numRunning,
            (unsigned long) context->_readTransactions._numAborted);
 
  DumpTransactionList(&context->_readTransactions);
  
  LOG_INFO("write transactions: numRunning: %lu, length: %lu, aborted: %lu", 
            (unsigned long) context->_writeTransactions._vector._length,
            (unsigned long) context->_writeTransactions._numRunning,
            (unsigned long) context->_writeTransactions._numAborted);

  DumpTransactionList(&context->_writeTransactions);

  TRI_UnlockMutex(&context->_lock);
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                       TRANSACTION
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup VocBase
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief create a transaction collection container
////////////////////////////////////////////////////////////////////////////////

static TRI_transaction_collection_t* CreateCollection (const char* const name, 
                                                       const TRI_transaction_type_e type) {
  TRI_transaction_collection_t* collection;

  collection = (TRI_transaction_collection_t*) TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_transaction_collection_t), false);
  if (collection == NULL) {
    return NULL;
  }

  collection->_name = TRI_DuplicateStringZ(TRI_UNKNOWN_MEM_ZONE, name);
  if (collection->_name == NULL) {
    TRI_Free(TRI_UNKNOWN_MEM_ZONE, collection);

    return NULL;
  }

  collection->_type       = type;
  collection->_collection = NULL;
  collection->_locked     = false;
  InitTransactionList(&collection->_writeTransactions);

  return collection;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free a transaction collection container
////////////////////////////////////////////////////////////////////////////////

static void FreeCollection (TRI_transaction_collection_t* collection) {
  assert(collection);
  assert(collection->_name);

  TRI_FreeString(TRI_UNKNOWN_MEM_ZONE, (char*) collection->_name);
  
  DestroyTransactionList(&collection->_writeTransactions);

  TRI_Free(TRI_UNKNOWN_MEM_ZONE, collection);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief acquire collection locks for a transaction
////////////////////////////////////////////////////////////////////////////////

static int AcquireCollectionLocks (TRI_transaction_t* const trx) {
  TRI_transaction_context_t* context;
  size_t i, n;
  
  LOG_DEBUG("acquiring collection locks");

  context = trx->_context;

  n = trx->_collections._length;
  assert(n > 0);

  for (i = 0; i < n; ++i) {
    TRI_transaction_collection_t* collection;

    collection = TRI_AtVectorPointer(&trx->_collections, i);
    collection->_collection = TRI_UseCollectionByNameVocBase(trx->_context->_vocbase, collection->_name);

    if (collection->_collection == NULL) {
      return TRI_errno();
    }
  
    if (collection->_type == TRI_TRANSACTION_WRITE) {
      TRI_transaction_context_collection_t* globalCollection;
      
      LOG_DEBUG("acquiring write-lock on collection '%s'", collection->_name);

      // start critical section -----------------------------------------
      TRI_LockMutex(&context->_lock); 

      globalCollection = (TRI_transaction_context_collection_t*) TRI_LookupByKeyAssociativePointer(&context->_collections, collection->_name);
      if (globalCollection == NULL) {
        globalCollection = CreateCollectionTransactionContext(collection);
        if (globalCollection != NULL) {
          TRI_InsertKeyAssociativePointer(&context->_collections, collection->_name, globalCollection, false); 
        }
      }

      TRI_UnlockMutex(&context->_lock); 
      // end critical section -----------------------------------------

      if (globalCollection == NULL) {
        return TRI_ERROR_OUT_OF_MEMORY;
      }

      TRI_LockMutex(&globalCollection->_writeLock);
      collection->_locked = true;
    }
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief release collection locks for a transaction
////////////////////////////////////////////////////////////////////////////////

static int ReleaseCollectionLocks (TRI_transaction_t* const trx) {
  TRI_transaction_context_t* context;
  size_t i;
  int res;

  LOG_DEBUG("releasing collection locks");
  
  res = TRI_ERROR_NO_ERROR;
  context = trx->_context;

  i = trx->_collections._length;
  assert(i > 0);

  while (i-- > 0) {
    TRI_transaction_collection_t* collection;

    collection = TRI_AtVectorPointer(&trx->_collections, i);
    if (collection->_collection != NULL) {
      if (collection->_type == TRI_TRANSACTION_WRITE && collection->_locked) {
        TRI_transaction_context_collection_t* globalCollection;

        LOG_DEBUG("releasing write-lock on collection '%s'", collection->_name);

        globalCollection = (TRI_transaction_context_collection_t*) TRI_LookupByKeyAssociativePointer(&context->_collections, collection->_name);
        if (globalCollection != NULL) {
          TRI_UnlockMutex(&globalCollection->_writeLock);
        }
        else {
          res = TRI_ERROR_INTERNAL;
        }

        collection->_locked = false;
      }

      TRI_ReleaseCollectionVocBase(trx->_context->_vocbase, collection->_collection);
      collection->_collection = NULL;
    }
  }

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief insert a transaction into all collections' write transaction lists
/// The context lock must be held when calling this function
////////////////////////////////////////////////////////////////////////////////

static int InsertCollectionsWriteTransaction (TRI_transaction_t* const trx) {
  TRI_transaction_context_t* context;
  size_t i, n;

  context = trx->_context;

  n = trx->_collections._length;

  for (i = 0; i < n; ++i) {
    TRI_transaction_collection_t* collection;
    TRI_transaction_context_collection_t* globalCollection;
    int res;
    
    collection = (TRI_transaction_collection_t*) TRI_AtVectorPointer(&trx->_collections, i);

    if (collection->_type == TRI_TRANSACTION_READ) {
      continue;
    }

    globalCollection = (TRI_transaction_context_collection_t*) TRI_LookupByKeyAssociativePointer(&context->_collections, collection->_name);
    if (globalCollection == NULL) {
      return TRI_ERROR_INTERNAL;
    }
  
    res = InsertTransactionList(&globalCollection->_writeTransactions, trx);
    if (res != TRI_ERROR_NO_ERROR) {
      return res;
    }
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief remove a transaction from all collections' write transaction lists
/// The context lock must be held when calling this function
////////////////////////////////////////////////////////////////////////////////

static int RemoveCollectionsWriteTransaction (TRI_transaction_t* const trx, 
                                              const TRI_transaction_local_id_t id) {
  TRI_transaction_context_t* context;
  size_t i, n;

  context = trx->_context;

  n = trx->_collections._length;

  for (i = 0; i < n; ++i) {
    TRI_transaction_collection_t* collection;
    TRI_transaction_context_collection_t* globalCollection;
    int res;
    
    collection = (TRI_transaction_collection_t*) TRI_AtVectorPointer(&trx->_collections, i);

    if (collection->_type == TRI_TRANSACTION_READ) {
      continue;
    }

    globalCollection = (TRI_transaction_context_collection_t*) TRI_LookupByKeyAssociativePointer(&context->_collections, collection->_name);
    if (globalCollection == NULL) {
      // should not happen
      continue;
    }
    
    res = RemoveTransactionList(&globalCollection->_writeTransactions, id); 
    if (res != TRI_ERROR_NO_ERROR) {
      return res;
    }
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief update the status of a transaction in all collections' write
///  transaction lists
/// The context lock must be held when calling this function
////////////////////////////////////////////////////////////////////////////////

static int UpdateCollectionsWriteTransaction (TRI_transaction_t* const trx, 
                                              const TRI_transaction_local_id_t id,
                                              const TRI_transaction_status_e status) {
  TRI_transaction_context_t* context;
  size_t i, n;

  context = trx->_context;

  n = trx->_collections._length;

  for (i = 0; i < n; ++i) {
    TRI_transaction_collection_t* collection;
    TRI_transaction_context_collection_t* globalCollection;
    int res;
    
    collection = (TRI_transaction_collection_t*) TRI_AtVectorPointer(&trx->_collections, i);

    if (collection->_type == TRI_TRANSACTION_READ) {
      continue;
    }

    globalCollection = (TRI_transaction_context_collection_t*) TRI_LookupByKeyAssociativePointer(&context->_collections, collection->_name);
    if (globalCollection == NULL) {
      // should not happen
      continue;
    }
    
    res = UpdateTransactionList(&globalCollection->_writeTransactions, id, status); 
    if (res != TRI_ERROR_NO_ERROR) {
      return res;
    }
  }

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief update the status of a transaction 
////////////////////////////////////////////////////////////////////////////////

static int UpdateTransactionStatus (TRI_transaction_t* const trx, 
                                    const TRI_transaction_status_e status) {
  const TRI_transaction_local_id_t id = trx->_id._localId;
  TRI_transaction_context_t* context;
  int res;

  assert(trx->_status == TRI_TRANSACTION_RUNNING);

  context = trx->_context;

  // start critical section -----------------------------------------
  TRI_LockMutex(&context->_lock); 

  if (trx->_type == TRI_TRANSACTION_READ) {
    // read-only transactions cannot commit or roll-back
    assert(status == TRI_TRANSACTION_FINISHED);

    LOG_TRACE("removing read transaction %lu", (unsigned long) id);
    res = RemoveTransactionList(&context->_readTransactions, id);
  }
  else {
    // write transactions
    
    if (status == TRI_TRANSACTION_COMMITTED) {
      LOG_TRACE("removing write transaction %lu", (unsigned long) id);
      RemoveCollectionsWriteTransaction(trx, id);
      res = RemoveTransactionList(&context->_writeTransactions, id);
    }
    else if (status == TRI_TRANSACTION_ABORTED) {
      LOG_TRACE("updating write transaction %lu status %s", (unsigned long) id, StatusString(status));
      UpdateCollectionsWriteTransaction(trx, id, status);
      res = UpdateTransactionList(&context->_writeTransactions, id, status);
    }
    else {
      res = TRI_ERROR_INTERNAL;
    }
  }

  TRI_UnlockMutex(&context->_lock); 
  // end critical section -----------------------------------------

  if (res == TRI_ERROR_NO_ERROR) {
    trx->_status = status;
  }

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief register a transaction
////////////////////////////////////////////////////////////////////////////////

static int RegisterTransaction (TRI_transaction_t* const trx) {
  TRI_transaction_context_t* context;
  TRI_transaction_list_t* list;
  int res;

  assert(trx->_status == TRI_TRANSACTION_CREATED);
  assert(trx->_collections._length > 0);

  context = trx->_context;

  trx->_status = TRI_TRANSACTION_RUNNING;

  if (trx->_type == TRI_TRANSACTION_READ) {
    // read-only transaction
    list = &context->_readTransactions;
  }
  else {
    list = &context->_writeTransactions;
  }

  // start critical section -----------------------------------------
  TRI_LockMutex(&context->_lock); 

  trx->_id._localId = NextLocalTransactionId(context);
  res = InsertTransactionList(list, trx);

  if (trx->_type == TRI_TRANSACTION_WRITE) {
    InsertCollectionsWriteTransaction(trx);
  }

  TRI_UnlockMutex(&context->_lock);
  // end critical section -----------------------------------------

  return res;
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
/// @brief create a new transaction container
////////////////////////////////////////////////////////////////////////////////

TRI_transaction_t* TRI_CreateTransaction (TRI_transaction_context_t* const context,
                                          const TRI_transaction_isolation_level_e isolationLevel) {
  TRI_transaction_t* trx;
  
  trx = (TRI_transaction_t*) TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_transaction_t), false);
  if (trx == NULL) {
    // out of memory
    return NULL;
  } 

  trx->_context        = context;
  trx->_id._serverId   = context->_id._serverId;
  trx->_id._localId    = 0;
  trx->_status         = TRI_TRANSACTION_CREATED;  
  trx->_type           = TRI_TRANSACTION_READ;
  trx->_isolationLevel = isolationLevel;
  
  TRI_InitVectorPointer(&trx->_collections, TRI_UNKNOWN_MEM_ZONE);

  return trx;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free a transaction container
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeTransaction (TRI_transaction_t* const trx) {
  size_t i;

  assert(trx);

  if (trx->_status == TRI_TRANSACTION_RUNNING) {
    TRI_AbortTransaction(trx);
  }

  ReleaseCollectionLocks(trx);

  // free all collections
  i = trx->_collections._length;
  while (i-- > 0) {
    TRI_transaction_collection_t* collection = TRI_AtVectorPointer(&trx->_collections, i);

    FreeCollection(collection);
  }

  TRI_DestroyVectorPointer(&trx->_collections);

  TRI_Free(TRI_UNKNOWN_MEM_ZONE, trx);
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
/// @brief return the local id of a transaction
////////////////////////////////////////////////////////////////////////////////

TRI_transaction_local_id_t TRI_LocalIdTransaction (const TRI_transaction_t* const trx) {
  return trx->_id._localId;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief dump information about a transaction
////////////////////////////////////////////////////////////////////////////////

void TRI_DumpTransaction (TRI_transaction_t* const trx) {
  size_t i, n;

  LOG_INFO("transaction id: %lu:%lu, type: %s, status: %s", 
           (unsigned long) trx->_id._serverId, 
           (unsigned long) trx->_id._localId, 
           TypeString(trx->_type), 
           StatusString(trx->_status));

  n = trx->_collections._length;
  for (i = 0; i < n; ++i) {
    TRI_transaction_collection_t* collection = TRI_AtVectorPointer(&trx->_collections, i);

    LOG_INFO("- collection: %s, type: %s", collection->_name, TypeString(collection->_type));
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief add a collection to a transaction
////////////////////////////////////////////////////////////////////////////////

bool TRI_AddCollectionTransaction (TRI_transaction_t* const trx,
                                   const char* const name,
                                   const TRI_transaction_type_e type) {
  TRI_transaction_collection_t* collection;
  size_t i, n;

  assert(trx->_status == TRI_TRANSACTION_CREATED);
  assert(name);

  // upgrade transaction type if required
  if (type == TRI_TRANSACTION_WRITE && trx->_type == TRI_TRANSACTION_READ) {
    trx->_type = TRI_TRANSACTION_WRITE;
  }

  // check if we already have got this collection in the vector
  n = trx->_collections._length;
  for (i = 0; i < n; ++i) {
    int res;
    
    collection = TRI_AtVectorPointer(&trx->_collections, i);
    res = strcmp(name, collection->_name);

    if (res < 0) {
      // collection is not contained in vector
      collection = CreateCollection(name, type);
      if (collection == NULL) {
        // out of memory
        return false;
      }

      TRI_InsertVectorPointer(&trx->_collections, collection, i);

      return true;
    }
    else if (res == 0) {
      // collection is already contained in vector

      // upgrade collection type if required
      if (type == TRI_TRANSACTION_WRITE && collection->_type == TRI_TRANSACTION_READ) {
        collection->_type = TRI_TRANSACTION_WRITE;
      }

      return true;
    }
  }
  
  // collection was not contained. now insert it
  collection = CreateCollection(name, type);
  if (collection == NULL) {
    // out of memory
    return false;
  }

  TRI_PushBackVectorPointer(&trx->_collections, collection);
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief start a transaction
////////////////////////////////////////////////////////////////////////////////

int TRI_StartTransaction (TRI_transaction_t* const trx) {
  int res;
  
  assert(trx->_status == TRI_TRANSACTION_CREATED);
  assert(trx->_collections._length > 0);

  res = AcquireCollectionLocks(trx);
  if (res != TRI_ERROR_NO_ERROR) {
    // free what we have got so far
    trx->_status = TRI_TRANSACTION_FAILED;
    ReleaseCollectionLocks(trx);

    return res;
  }

  res = RegisterTransaction(trx);
  if (res != TRI_ERROR_NO_ERROR) {
    // free what we have got so far
    trx->_status = TRI_TRANSACTION_FAILED;
    ReleaseCollectionLocks(trx);

    return res;
  }

  trx->_status = TRI_TRANSACTION_RUNNING;

  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief commit a transaction
////////////////////////////////////////////////////////////////////////////////

int TRI_CommitTransaction (TRI_transaction_t* const trx) {
  int res;

  assert(trx->_status == TRI_TRANSACTION_RUNNING);

  res = UpdateTransactionStatus(trx, TRI_TRANSACTION_COMMITTED);
  ReleaseCollectionLocks(trx);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief abort a transaction
////////////////////////////////////////////////////////////////////////////////

int TRI_AbortTransaction (TRI_transaction_t* const trx) {
  int res;

  assert(trx->_status == TRI_TRANSACTION_RUNNING);
  
  res = UpdateTransactionStatus(trx, TRI_TRANSACTION_ABORTED);
  ReleaseCollectionLocks(trx);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief finish a transaction
////////////////////////////////////////////////////////////////////////////////

int TRI_FinishTransaction (TRI_transaction_t* const trx) {
  int res;
  TRI_transaction_status_e status;

  assert(trx->_status == TRI_TRANSACTION_RUNNING);

  if (trx->_type == TRI_TRANSACTION_READ) {
    // read transactions
    status = TRI_TRANSACTION_FINISHED;
  }
  else {
    // write transactions
    status = TRI_TRANSACTION_COMMITTED;
  }
  
  res = UpdateTransactionStatus(trx, status);

  ReleaseCollectionLocks(trx);

  return res;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:
