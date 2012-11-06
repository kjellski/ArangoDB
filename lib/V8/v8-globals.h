////////////////////////////////////////////////////////////////////////////////
/// @brief V8-vocbase bridge
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2012 triAGENS GmbH, Cologne, Germany
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
/// @author Copyright 2011-2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_V8_V8_GLOBALS_H
#define TRIAGENS_V8_V8_GLOBALS_H 1

#include "BasicsC/common.h"

#include <regex.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include <v8.h>

#include "Basics/ReadWriteLock.h"

// -----------------------------------------------------------------------------
// --SECTION--                                                      public types
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8Globals
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief globals stored in the isolate
////////////////////////////////////////////////////////////////////////////////

typedef struct TRI_v8_global_s {

////////////////////////////////////////////////////////////////////////////////
/// @brief constructor
////////////////////////////////////////////////////////////////////////////////

  TRI_v8_global_s ()
    : JSGeneralCursors(),
      JSTransactions(),
      JSBarriers(),
      ErrorTempl(),
      GeneralCursorTempl(),
      TransactionTempl(),
      VocbaseColTempl(),
      VocbaseTempl(),
      DictionaryTempl(),
      CollectionQueryType(),
      BidirectionalKey(),
      DidKey(),
      FromKey(),
      IidKey(),
      OldRevKey(),
      RevKey(),
      ToKey(),
      BodyKey(),
      ContentTypeKey(),
      JournalSizeKey(),
      ParametersKey(),
      PathKey(),
      PrefixKey(),
      ResponseCodeKey(),
      SuffixKey(),
      UserKey(),
      WaitForSyncKey(),
      DocumentIdRegex(),
      DocumentKeyRegex(),
      IndexIdRegex() {
  }

  ~TRI_v8_global_s () {
    regfree(&DocumentIdRegex);
    regfree(&DocumentKeyRegex);
    regfree(&IndexIdRegex);
  }

////////////////////////////////////////////////////////////////////////////////
/// @brief general cursor mapping for weak pointers
////////////////////////////////////////////////////////////////////////////////

  std::map< void*, v8::Persistent<v8::Value> > JSGeneralCursors;

////////////////////////////////////////////////////////////////////////////////
/// @brief transaction mapping for weak pointers
////////////////////////////////////////////////////////////////////////////////

  std::map< void*, v8::Persistent<v8::Value> > JSTransactions;

////////////////////////////////////////////////////////////////////////////////
/// @brief barrier mapping for weak pointers
////////////////////////////////////////////////////////////////////////////////

  std::map< void*, v8::Persistent<v8::Value> > JSBarriers;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                       JAVASCRIPT OBJECT TEMPLATES
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8Globals
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief error template
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::ObjectTemplate> ErrorTempl;

////////////////////////////////////////////////////////////////////////////////
/// @brief general cursor template
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::ObjectTemplate> GeneralCursorTempl;

////////////////////////////////////////////////////////////////////////////////
/// @brief transaction template
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::ObjectTemplate> TransactionTempl;

////////////////////////////////////////////////////////////////////////////////
/// @brief TRI_vocbase_col_t template
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::ObjectTemplate> VocbaseColTempl;

////////////////////////////////////////////////////////////////////////////////
/// @brief TRI_vocbase_t template
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::ObjectTemplate> VocbaseTempl;

////////////////////////////////////////////////////////////////////////////////
/// @brief TRI_shaped_json_t template
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::ObjectTemplate> ShapedJsonTempl;

////////////////////////////////////////////////////////////////////////////////
/// @brief dictionary template
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::ObjectTemplate> DictionaryTempl;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                              JAVASCRIPT CONSTANTS
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8Globals
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief "collection" query type
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> CollectionQueryType;

////////////////////////////////////////////////////////////////////////////////
/// @brief "DELETE" function name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> DeleteConstant;

////////////////////////////////////////////////////////////////////////////////
/// @brief "GET" function name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> GetConstant;

////////////////////////////////////////////////////////////////////////////////
/// @brief "HEAD" function name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> HeadConstant;

////////////////////////////////////////////////////////////////////////////////
/// @brief "PATCH" function name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> PatchConstant;

////////////////////////////////////////////////////////////////////////////////
/// @brief "POST" function name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> PostConstant;

////////////////////////////////////////////////////////////////////////////////
/// @brief "PUT" function name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> PutConstant;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                              JAVASCRIPT KEY NAMES
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8Globals
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief "_bidirectional" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> BidirectionalKey;

////////////////////////////////////////////////////////////////////////////////
/// @brief "_id" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> DidKey;

////////////////////////////////////////////////////////////////////////////////
/// @brief "_key" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> KeyKey;
  
////////////////////////////////////////////////////////////////////////////////
/// @brief "_from" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> FromKey;

////////////////////////////////////////////////////////////////////////////////
/// @brief "id" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> IidKey;

////////////////////////////////////////////////////////////////////////////////
/// @brief "_oldRev" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> OldRevKey;

////////////////////////////////////////////////////////////////////////////////
/// @brief "_rev" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> RevKey;

////////////////////////////////////////////////////////////////////////////////
/// @brief "_to" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> ToKey;

////////////////////////////////////////////////////////////////////////////////
/// @brief "body" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> BodyKey;

////////////////////////////////////////////////////////////////////////////////
/// @brief "contentType" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> ContentTypeKey;

////////////////////////////////////////////////////////////////////////////////
/// @brief "headers" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> HeadersKey;

////////////////////////////////////////////////////////////////////////////////
/// @brief "journalSize" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> JournalSizeKey;

////////////////////////////////////////////////////////////////////////////////
/// @brief "parameters" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> ParametersKey;

////////////////////////////////////////////////////////////////////////////////
/// @brief "path" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> PathKey;

////////////////////////////////////////////////////////////////////////////////
/// @brief "prefix" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> PrefixKey;

////////////////////////////////////////////////////////////////////////////////
/// @brief "requestBody" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> RequestBodyKey;

////////////////////////////////////////////////////////////////////////////////
/// @brief "requestType" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> RequestTypeKey;

////////////////////////////////////////////////////////////////////////////////
/// @brief "responseCode" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> ResponseCodeKey;

////////////////////////////////////////////////////////////////////////////////
/// @brief "suffix" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> SuffixKey;

////////////////////////////////////////////////////////////////////////////////
/// @brief "transformations" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> TransformationsKey;

////////////////////////////////////////////////////////////////////////////////
/// @brief "user" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> UserKey;

////////////////////////////////////////////////////////////////////////////////
/// @brief "waitForSync" key name
////////////////////////////////////////////////////////////////////////////////

  v8::Persistent<v8::String> WaitForSyncKey;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                               REGULAR EXPRESSIONS
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup V8Globals
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief document identifier as collection-id:document-id
////////////////////////////////////////////////////////////////////////////////

  regex_t DocumentIdRegex;

////////////////////////////////////////////////////////////////////////////////
/// @brief document identifier 
////////////////////////////////////////////////////////////////////////////////

  regex_t DocumentKeyRegex;

////////////////////////////////////////////////////////////////////////////////
/// @brief index identifier as collection-id:index-id
////////////////////////////////////////////////////////////////////////////////

  regex_t IndexIdRegex;
}
TRI_v8_global_t;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

#endif

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}\\)"
// End:
