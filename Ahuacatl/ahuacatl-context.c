////////////////////////////////////////////////////////////////////////////////
/// @brief Ahuacatl, query context
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2010-2012 triagens GmbH, Cologne, Germany
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

#include "Ahuacatl/ahuacatl-context.h"
#include "Ahuacatl/ahuacatl-access-optimiser.h"
#include "Ahuacatl/ahuacatl-ast-node.h"
#include "Ahuacatl/ahuacatl-bind-parameter.h"
#include "Ahuacatl/ahuacatl-collections.h"
#include "Ahuacatl/ahuacatl-optimiser.h"
#include "Ahuacatl/ahuacatl-parser-functions.h"
#include "Ahuacatl/ahuacatl-scope.h"
#include "Ahuacatl/ahuacatl-tree-dump.h"

// -----------------------------------------------------------------------------
// --SECTION--                                                    private macros
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Ahuacatl
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief shortcut macro for signalling out of memory
////////////////////////////////////////////////////////////////////////////////

#define ABORT_OOM \
  TRI_SetErrorContextAql(context, TRI_ERROR_OUT_OF_MEMORY, NULL); \
  return NULL;

////////////////////////////////////////////////////////////////////////////////
/// @} 
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Ahuacatl
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief free all collection memory
////////////////////////////////////////////////////////////////////////////////
  
static void FreeCollections (TRI_aql_context_t* const context) {
  size_t i = context->_collections._length;

  while (i--) {
    TRI_aql_collection_t* collection = (TRI_aql_collection_t*) context->_collections._buffer[i];
    if (collection) {
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, collection);
    }
  }
  TRI_DestroyVectorPointer(&context->_collections);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free all scopes
////////////////////////////////////////////////////////////////////////////////
  
static void FreeScopes (TRI_aql_context_t* const context) {
  while (context->_scopes._length) {
    TRI_EndScopeContextAql(context);
  }
  TRI_DestroyVectorPointer(&context->_scopes);
  
  // free scopes allocated by optimiser
  TRI_DestroyVectorPointer(&context->_optimiser._scopes);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free all strings
////////////////////////////////////////////////////////////////////////////////
  
static void FreeStrings (TRI_aql_context_t* const context) {
  size_t i = context->_memory._strings._length;

  while (i--) {
    void* string = context->_memory._strings._buffer[i];

    if (string) {
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, context->_memory._strings._buffer[i]);
    }
  }
  TRI_DestroyVectorPointer(&context->_memory._strings);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free all nodes
////////////////////////////////////////////////////////////////////////////////
  
static void FreeNodes (TRI_aql_context_t* const context) {
  size_t i = context->_memory._nodes._length;

  while (i--) {
    TRI_aql_node_t* node = (TRI_aql_node_t*) context->_memory._nodes._buffer[i];

    if (node) {
      TRI_DestroyVectorPointer(&node->_members);

      if (node->_type == AQL_NODE_FOR && node->_value._value._data != NULL) {
        TRI_FreeAccessesAql((TRI_vector_pointer_t*) node->_value._value._data);
      }
      // free node itself
      TRI_Free(TRI_UNKNOWN_MEM_ZONE, node);
    }
  }

  TRI_DestroyVectorPointer(&context->_memory._nodes);
}

////////////////////////////////////////////////////////////////////////////////
/// @} 
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                        constructors / destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Ahuacatl
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief create and initialize a context
////////////////////////////////////////////////////////////////////////////////

TRI_aql_context_t* TRI_CreateContextAql (TRI_vocbase_t* vocbase,
                                         const char* const query) {
  TRI_aql_context_t* context;

  assert(vocbase);
  assert(query);

  context = (TRI_aql_context_t*) TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_aql_context_t), false);
  if (!context) {
    return NULL;
  }

  context->_vocbase = vocbase;

  context->_variableIndex = 0;
  
  // actual bind parameter values
  TRI_InitAssociativePointer(&context->_parameters._values,
                             TRI_UNKNOWN_MEM_ZONE, 
                             &TRI_HashStringKeyAssociativePointer,
                             &TRI_HashBindParameterAql,
                             &TRI_EqualBindParameterAql,
                             0);

  // bind parameter names used in the query
  TRI_InitAssociativePointer(&context->_parameters._names,
                             TRI_UNKNOWN_MEM_ZONE, 
                             &TRI_HashStringKeyAssociativePointer,
                             &TRI_HashStringKeyAssociativePointer,
                             &TRI_EqualStringKeyAssociativePointer,
                             0);
  
  // collections
  TRI_InitAssociativePointer(&context->_collectionNames,
                             TRI_UNKNOWN_MEM_ZONE, 
                             &TRI_HashStringKeyAssociativePointer,
                             &TRI_HashStringKeyAssociativePointer,
                             &TRI_EqualStringKeyAssociativePointer,
                             0);
  
  TRI_InitVectorPointer(&context->_memory._nodes, TRI_UNKNOWN_MEM_ZONE);
  TRI_InitVectorPointer(&context->_memory._strings, TRI_UNKNOWN_MEM_ZONE);
  TRI_InitVectorPointer(&context->_collections, TRI_UNKNOWN_MEM_ZONE);
  TRI_InitVectorPointer(&context->_scopes, TRI_UNKNOWN_MEM_ZONE); // TODO: remove
  TRI_InitScopesAql(context);

  TRI_InitVectorPointer(&context->_optimiser._scopes, TRI_UNKNOWN_MEM_ZONE);

  TRI_InitErrorAql(&context->_error);

  context->_parser = NULL;
  context->_first = NULL;

  context->_query = query;
  if (!context->_query) {
    TRI_FreeContextAql(context);
    return NULL;
  }

  context->_parser = TRI_CreateParserAql(context->_query);

  if (!context->_parser) {
    // could not create the parser
    TRI_FreeContextAql(context);
    return NULL;
  }

  if (!TRI_InitParserAql(context)) {
    // could not initialise the lexer
    TRI_FreeContextAql(context);
    return NULL;
  }

  return context;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free a context
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeContextAql (TRI_aql_context_t* const context) {
  assert(context);

  // remove barriers for all collections used
  TRI_RemoveBarrierCollectionsAql(context);

  // read-unlock all collections used
  TRI_ReadUnlockCollectionsAql(context);

  // release all collections used
  TRI_UnlockCollectionsAql(context);

  // release all scopes
  TRI_FreeScopesAql(context);

  FreeScopes(context);
  FreeStrings(context);
  FreeNodes(context); 

  // free parameter names hash
  TRI_DestroyAssociativePointer(&context->_parameters._names);
  
  // free collection names
  TRI_DestroyAssociativePointer(&context->_collectionNames);
  
  FreeCollections(context);
  
  // free parameter values
  TRI_FreeBindParametersAql(context);
  TRI_DestroyAssociativePointer(&context->_parameters._values);

  // free parser/lexer
  TRI_FreeParserAql(context->_parser);

  // free error struct
  TRI_DestroyErrorAql(&context->_error);

  TRI_Free(TRI_UNKNOWN_MEM_ZONE, context);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief parse & validate the query string
////////////////////////////////////////////////////////////////////////////////
  
bool TRI_ValidateQueryContextAql (TRI_aql_context_t* const context) {
  if (context->_parser->_length == 0) {
    // query is empty, no need to parse it
    TRI_SetErrorContextAql(context, TRI_ERROR_QUERY_EMPTY, NULL);
    return false;
  }

  // parse the query
  if (!TRI_ParseAql(context)) {
    // lexing/parsing failed
    return false;
  }

  if (context->_error._code) {
    return false;
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief add bind parameters to the query context
////////////////////////////////////////////////////////////////////////////////
 
bool TRI_BindQueryContextAql (TRI_aql_context_t* const context,
                              const TRI_json_t* const parameters) {

  // add the bind parameters
  if (!TRI_AddParameterValuesAql(context, parameters)) {
    // adding parameters failed
    return false;
  }
  
  // validate the bind parameters used/passed
  if (!TRI_ValidateBindParametersAql(context)) {
    // invalid bind parameters
    return false;
  }

  // inject the bind parameter values into the query AST
  if (!TRI_InjectBindParametersAql(context, (TRI_aql_node_t*) context->_first)) {
    // bind parameter injection failed
    return false;
  }
  
  if (context->_error._code) {
    return false;
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief perform some AST optimisations
////////////////////////////////////////////////////////////////////////////////

bool TRI_OptimiseQueryContextAql (TRI_aql_context_t* const context) {
  // do some basic optimisations in the AST
  if (!TRI_OptimiseAql(context, (TRI_aql_node_t*) context->_first)) {
    // constant folding failed
    return false;
  }

  if (context->_error._code) {
    return false;
  }

  // TRI_DumpTreeAql((TRI_aql_node_t*) context->_first);

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief acquire all locks necessary for the query
////////////////////////////////////////////////////////////////////////////////

bool TRI_LockQueryContextAql (TRI_aql_context_t* const context) {
  // mark all used collections as being used
  if (!TRI_LockCollectionsAql(context)) {
    return false;
  }

  // acquire read locks on all collections used
  if (!TRI_ReadLockCollectionsAql(context)) {
    return false;
  }
  
  // add barriers for all collections used
  if (!TRI_AddBarrierCollectionsAql(context)) {
    return false;
  }
  
  if (context->_error._code) {
    return false;
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create a new variable scope
////////////////////////////////////////////////////////////////////////////////

TRI_aql_scope2_t* TRI_CreateScopeAql (void) {
  TRI_aql_scope2_t* scope;

  scope = (TRI_aql_scope2_t*) TRI_Allocate(TRI_UNKNOWN_MEM_ZONE, sizeof(TRI_aql_scope2_t), false);
  if (!scope) {
    return NULL;
  }

  TRI_InitAssociativePointer(&scope->_variables, 
                             TRI_UNKNOWN_MEM_ZONE, 
                             &TRI_HashStringKeyAssociativePointer,
                             &TRI_HashVariableAql,
                             &TRI_EqualVariableAql, 
                             0);
  
  scope->_first = NULL;
  scope->_last = NULL;

  return scope;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief free a variable scope
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeScopeAql (TRI_aql_scope2_t* const scope) {
  size_t i, length;

  assert(scope);
 
  // free variables lookup hash
  length = scope->_variables._nrAlloc;
  for (i = 0; i < length; ++i) {
    TRI_aql_variable_t* variable = scope->_variables._table[i];

    if (variable) {
      TRI_FreeVariableAql(variable);
    }
  }

  TRI_DestroyAssociativePointer(&scope->_variables);

  TRI_Free(TRI_UNKNOWN_MEM_ZONE, scope);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief register a node
////////////////////////////////////////////////////////////////////////////////

bool TRI_RegisterNodeContextAql (TRI_aql_context_t* const context,
                                 void* const node) {
  assert(context);
  assert(node);

  TRI_PushBackVectorPointer(&context->_memory._nodes, node);

  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief register an error
////////////////////////////////////////////////////////////////////////////////

void TRI_SetErrorContextAql (TRI_aql_context_t* const context, 
                             const int code,
                             const char* const data) {

  assert(context);
  assert(code > 0);

  if (context->_error._code == 0) {
    // do not overwrite previous error
    TRI_set_errno(code);
    context->_error._code = code;
    context->_error._message = (char*) TRI_last_error();
    if (data) {
      context->_error._data = TRI_DuplicateStringZ(TRI_UNKNOWN_MEM_ZONE, data);
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get first statement in the current scope
////////////////////////////////////////////////////////////////////////////////

void* TRI_GetFirstStatementAql (TRI_aql_context_t* const context) {
  TRI_aql_scope2_t* scope;
  size_t length;

  assert(context);

  length = context->_scopes._length;

  assert(length > 0);

  scope = (TRI_aql_scope2_t*) context->_scopes._buffer[length - 1];
  assert(scope);

  return scope->_first;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief add a statement to the current scope
////////////////////////////////////////////////////////////////////////////////

bool TRI_AddStatementAql (TRI_aql_context_t* const context, 
                          const void* const statement) {

  TRI_aql_scope2_t* scope;
  size_t n;
 
  assert(context);

  if (!statement) {
    return false;
  }
        
  n = context->_scopes._length;
  assert(n > 0);

  scope = (TRI_aql_scope2_t*) context->_scopes._buffer[n - 1];
          
  if (!scope->_first) {
    if (n == 1 && !context->_first) {
      // first ever statement on outermost scope
      context->_first = (void*) statement;
    }

    scope->_first = (void*) statement;
    scope->_last = (void*) statement;
  }
  else {
    TRI_aql_node_t* node = (TRI_aql_node_t*) scope->_last;
    node->_next = (void*) statement;
    scope->_last = (void*) statement;
  }
                  
  return true;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief create a new variable scope and stack it in the context
////////////////////////////////////////////////////////////////////////////////

TRI_aql_scope2_t* TRI_StartScopeContextAql (TRI_aql_context_t* const context) {
  TRI_aql_scope2_t* scope;

  assert(context);
  scope = TRI_CreateScopeAql();
  if (!scope) {
    ABORT_OOM
  }

  TRI_PushBackVectorPointer(&context->_scopes, scope);
  return scope;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief remove a variable scope from context scopes stack
////////////////////////////////////////////////////////////////////////////////

void TRI_EndScopeContextAql (TRI_aql_context_t* const context) {
  TRI_aql_scope2_t* scope;
  size_t n;

  assert(context);

  n = context->_scopes._length;
  assert(n > 0);
  scope = (TRI_aql_scope2_t*) TRI_RemoveVectorPointer(&context->_scopes, n - 1);
  assert(scope);

  TRI_FreeScopeAql(scope);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief register a string
////////////////////////////////////////////////////////////////////////////////

char* TRI_RegisterStringAql (TRI_aql_context_t* const context, 
                             const char* const value, 
                             const size_t length,
                             const bool deescape) {
  char* copy;

  if (!value) {
    ABORT_OOM
  }

  if (deescape) {
    size_t outLength;
    copy = TRI_UnescapeUtf8String(value, length, &outLength);
  }
  else {
    copy = TRI_DuplicateStringZ(TRI_UNKNOWN_MEM_ZONE, value);
  }

  if (!copy) {
    ABORT_OOM
  }

  TRI_PushBackVectorPointer(&context->_memory._strings, copy);

  return copy;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:
