////////////////////////////////////////////////////////////////////////////////
/// @brief methods to do things in a cluster
///
/// @file ClusterMethods.cpp
///
/// DISCLAIMER
///
/// Copyright 2010-2014 triagens GmbH, Cologne, Germany
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
/// @author Max Neunhoeffer
/// @author Copyright 2014, triagens GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "Cluster/ClusterMethods.h"

#include "BasicsC/json.h"
#include "BasicsC/json-utilities.h"
#include "Basics/StringUtils.h"
#include "VocBase/server.h"

using namespace std;
using namespace triagens::basics;
using namespace triagens::rest;
using namespace triagens::arango;

namespace triagens {
  namespace arango {

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief check if a list of attributes have the same values in two JSON
/// documents
////////////////////////////////////////////////////////////////////////////////

bool shardKeysChanged (std::string const& dbname,
                       std::string const& collname,
                       TRI_json_t const* oldJson,
                       TRI_json_t const* newJson,
                       bool isPatch) {

  TRI_json_t nullJson;
  TRI_InitNullJson(&nullJson);
  
  if (! TRI_IsArrayJson(oldJson) || ! TRI_IsArrayJson(newJson)) {
    // expecting two arrays. everything else is an error
    return true;
  }

  ClusterInfo* ci = ClusterInfo::instance();
  TRI_shared_ptr<CollectionInfo> const& c = ci->getCollection(dbname, collname);
  const std::vector<std::string>& shardKeys = c->shardKeys();

  for (size_t i = 0; i < shardKeys.size(); ++i) {
    TRI_json_t const* n = TRI_LookupArrayJson(newJson, shardKeys[i].c_str());

    if (n == 0 && isPatch) {
      // attribute not set in patch document. this means no update
      continue;
    }

    TRI_json_t const* o = TRI_LookupArrayJson(oldJson, shardKeys[i].c_str());

    if (o == 0) {
      // if attribute is undefined, use "null" instead
      o = &nullJson;
    }
      
    if (n == 0) {
      // if attribute is undefined, use "null" instead
      n = &nullJson;
    }

    if (! TRI_CheckSameValueJson(o, n)) {
      return true;
    }
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief counts number of documents in a coordinator
////////////////////////////////////////////////////////////////////////////////

int countOnCoordinator (
                string const& dbname,
                string const& collname,
                uint64_t& result) {

  // Set a few variables needed for our work:
  ClusterInfo* ci = ClusterInfo::instance();
  ClusterComm* cc = ClusterComm::instance();
  
  result = 0;
  
  // First determine the collection ID from the name:
  TRI_shared_ptr<CollectionInfo> collinfo = ci->getCollection(dbname, collname);
  if (collinfo->empty()) {
    return TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND;
  }
  
  // If we get here, the sharding attributes are not only _key, therefore
  // we have to contact everybody:
  ClusterCommResult* res;
  map<ShardID, ServerID> shards = collinfo->shardIds();
  map<ShardID, ServerID>::iterator it;
  CoordTransactionID coordTransactionID = TRI_NewTickServer();
  for (it = shards.begin(); it != shards.end(); ++it) {
    map<string, string>* headers = new map<string, string>;
    res = cc->asyncRequest("", coordTransactionID, "shard:"+it->first,
                          triagens::rest::HttpRequest::HTTP_REQUEST_GET,
                          "/_db/"+dbname+"/_api/collection/"+
                          StringUtils::urlEncode(it->first)+"/count",
                          0, false, headers, NULL, 300.0);
    delete res;
  }
  // Now listen to the results:
  int count;
  int nrok = 0;
  for (count = shards.size(); count > 0; count--) {
    res = cc->wait( "", coordTransactionID, 0, "", 0.0);
    if (res->status == CL_COMM_RECEIVED) {
      if (res->answer_code == triagens::rest::HttpResponse::OK) {
        TRI_json_t* json = TRI_JsonString(TRI_UNKNOWN_MEM_ZONE, res->answer->body());

        if (JsonHelper::isArray(json)) {
          // add to the total
          result += JsonHelper::getNumericValue<uint64_t>(json, "count", 0);
          nrok++;
        }

        if (json != 0) {
          TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
        }
      }
    }
    delete res;
  }

  if (nrok != (int) shards.size()) {
    return TRI_ERROR_INTERNAL;
  }
  
  return TRI_ERROR_NO_ERROR;   // the cluster operation was OK, however,
                               // the DBserver could have reported an error.
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a document in a coordinator
////////////////////////////////////////////////////////////////////////////////

int createDocumentOnCoordinator (
                string const& dbname,
                string const& collname,
                bool waitForSync,
                TRI_json_t* json,
                triagens::rest::HttpResponse::HttpResponseCode& responseCode,
                string& contentType,
                string& resultBody) {

  // Set a few variables needed for our work:
  ClusterInfo* ci = ClusterInfo::instance();
  ClusterComm* cc = ClusterComm::instance();

  // First determine the collection ID from the name:
  TRI_shared_ptr<CollectionInfo> collinfo = ci->getCollection(dbname, collname);
  if (collinfo->empty()) {
    return TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND;
  }
  string collid = StringUtils::itoa(collinfo->id());

  // Sort out the _key attribute:
  // The user is allowed to specify _key, provided that _key is the one
  // and only sharding attribute, because in this case we can delegate
  // the responsibility to make _key attributes unique to the responsible
  // shard. Otherwise, we ensure uniqueness here and now by taking a
  // cluster-wide unique number. Note that we only know the sharding 
  // attributes a bit further down the line when we have determined
  // the responsible shard.
  TRI_json_t* subjson = TRI_LookupArrayJson(json, "_key");
  bool userSpecifiedKey = false;
  string _key;
  if (0 == subjson) {
    // The user did not specify a key, let's create one:
    uint64_t uid = ci->uniqid();
    _key = triagens::basics::StringUtils::itoa(uid);
    TRI_InsertArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "_key",
                        TRI_CreateStringReference2Json(TRI_UNKNOWN_MEM_ZONE, 
                                                     _key.c_str(), _key.size()));
  }
  else {
    userSpecifiedKey = true;
  }

  // Now find the responsible shard:
  bool usesDefaultShardingAttributes;
  ShardID shardID;
  int error = ci->getResponsibleShard( collid, json, true, shardID,
                                       usesDefaultShardingAttributes );
  if (error == TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    return TRI_ERROR_CLUSTER_SHARD_GONE;
  }

  // Now perform the above mentioned check:
  if (userSpecifiedKey && !usesDefaultShardingAttributes) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    return TRI_ERROR_CLUSTER_MUST_NOT_SPECIFY_KEY;
  }

  string body = JsonHelper::toString(json);
  TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);

  // Send a synchronous request to that shard using ClusterComm:
  ClusterCommResult* res;
  map<string, string> headers;
  res = cc->syncRequest("", TRI_NewTickServer(), "shard:"+shardID,
                        triagens::rest::HttpRequest::HTTP_REQUEST_POST,
                        "/_db/"+dbname+"/_api/document?collection="+
                        StringUtils::urlEncode(shardID)+"&waitForSync="+
                        (waitForSync ? "true" : "false"), body, headers, 60.0);
  
  if (res->status == CL_COMM_TIMEOUT) {
    // No reply, we give up:
    delete res;
    return TRI_ERROR_CLUSTER_TIMEOUT;
  }
  if (res->status == CL_COMM_ERROR) {
    // This could be a broken connection or an Http error:
    if (res->result == 0) {
      // there is not result
      delete res;
      return TRI_ERROR_CLUSTER_CONNECTION_LOST;
    }
    if (!res->result->isComplete()) {
      delete res;
      return TRI_ERROR_CLUSTER_CONNECTION_LOST;
    }
    // In this case a proper HTTP error was reported by the DBserver,
    // this can be 400 or 404, we simply forward the result.
    // We intentionally fall through here.
  }
  responseCode = static_cast<triagens::rest::HttpResponse::HttpResponseCode>
                            (res->result->getHttpReturnCode());
  contentType = res->result->getContentType(false);
  resultBody = res->result->getBody().str();
  delete res;
  return TRI_ERROR_NO_ERROR;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief deletes a document in a coordinator
////////////////////////////////////////////////////////////////////////////////

int deleteDocumentOnCoordinator (
                string const& dbname,
                string const& collname,
                string const& key,
                TRI_voc_rid_t const rev,
                TRI_doc_update_policy_e policy,
                bool waitForSync,
                triagens::rest::HttpResponse::HttpResponseCode& responseCode,
                string& contentType,
                string& resultBody) {

  // Set a few variables needed for our work:
  ClusterInfo* ci = ClusterInfo::instance();
  ClusterComm* cc = ClusterComm::instance();

  // First determine the collection ID from the name:
  TRI_shared_ptr<CollectionInfo> collinfo = ci->getCollection(dbname, collname);
  if (collinfo->empty()) {
    return TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND;
  }
  string collid = StringUtils::itoa(collinfo->id());

  // If _key is the one and only sharding attribute, we can do this quickly,
  // because we can easily determine which shard is responsible for the 
  // document. Otherwise we have to contact all shards and ask them to
  // delete the document. All but one will not know it.
  // Now find the responsible shard:
  TRI_json_t* json = TRI_CreateArrayJson(TRI_UNKNOWN_MEM_ZONE);
  if (0 == json) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }
  TRI_Insert2ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "_key", 
                       TRI_CreateStringReference2Json(TRI_UNKNOWN_MEM_ZONE,
                                 key.c_str(), key.size()));
  bool usesDefaultShardingAttributes;
  ShardID shardID;
  int error = ci->getResponsibleShard( collid, json, true, shardID,
                                       usesDefaultShardingAttributes );
  TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);

  // Some stuff to prepare cluster-intern requests:
  ClusterCommResult* res;
  string revstr;
  if (rev != 0) {
    revstr = "&rev="+StringUtils::itoa(rev);
  }
  string policystr;
  if (policy == TRI_DOC_UPDATE_LAST_WRITE) {
    policystr = "&policy=last";
  }

  if (usesDefaultShardingAttributes) {
    // OK, this is the fast method, we only have to ask one shard:
    if (error == TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND) {
      return TRI_ERROR_CLUSTER_SHARD_GONE;
    }

    // Send a synchronous request to that shard using ClusterComm:
    map<string, string> headers;
    res = cc->syncRequest("", TRI_NewTickServer(), "shard:"+shardID,
                          triagens::rest::HttpRequest::HTTP_REQUEST_DELETE,
                          "/_db/"+dbname+"/_api/document/"+
                          StringUtils::urlEncode(shardID)+"/"+key+
                          "?waitForSync="+(waitForSync ? "true" : "false")+
                          revstr+policystr, "", headers, 60.0);
  
    if (res->status == CL_COMM_TIMEOUT) {
      // No reply, we give up:
      delete res;
      return TRI_ERROR_CLUSTER_TIMEOUT;
    }
    if (res->status == CL_COMM_ERROR) {
      // This could be a broken connection or an Http error:
      if (!res->result->isComplete()) {
        delete res;
        return TRI_ERROR_CLUSTER_CONNECTION_LOST;
      }
      // In this case a proper HTTP error was reported by the DBserver,
      // this can be 400 or 404, we simply forward the result.
      // We intentionally fall through here.
    }
    responseCode = static_cast<triagens::rest::HttpResponse::HttpResponseCode>
                              (res->result->getHttpReturnCode());
    contentType = res->result->getContentType(false);
    resultBody = res->result->getBody().str();
    delete res;
    return TRI_ERROR_NO_ERROR;
  }

  // If we get here, the sharding attributes are not only _key, therefore
  // we have to contact everybody:
  map<ShardID, ServerID> shards = collinfo->shardIds();
  map<ShardID, ServerID>::iterator it;
  CoordTransactionID coordTransactionID = TRI_NewTickServer();
  for (it = shards.begin(); it != shards.end(); ++it) {
    map<string, string>* headers = new map<string, string>;
    res = cc->asyncRequest("", coordTransactionID, "shard:"+it->first,
                          triagens::rest::HttpRequest::HTTP_REQUEST_DELETE,
                          "/_db/"+dbname+"/_api/document/"+
                          StringUtils::urlEncode(it->first)+"/"+key+
                          "?waitForSync="+(waitForSync ? "true" : "false")+
                          revstr+policystr, 0, false, headers, NULL, 60.0);
    delete res;
  }
  // Now listen to the results:
  int count;
  int nrok = 0;
  for (count = shards.size(); count > 0; count--) {
    res = cc->wait( "", coordTransactionID, 0, "", 0.0);
    if (res->status == CL_COMM_RECEIVED) {
      if (res->answer_code != triagens::rest::HttpResponse::NOT_FOUND ||
          (nrok == 0 && count == 1)) {
        nrok++;
        responseCode = res->answer_code;
        contentType = res->answer->header("content-type");
        resultBody = string(res->answer->body(), res->answer->bodySize());
      }
    }
    delete res;
  }
  // Note that nrok is always at least 1!
  if (nrok > 1) {
    return TRI_ERROR_CLUSTER_GOT_CONTRADICTING_ANSWERS;
  }
  return TRI_ERROR_NO_ERROR;   // the cluster operation was OK, however,
                               // the DBserver could have reported an error.
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get a document in a coordinator
////////////////////////////////////////////////////////////////////////////////

int getDocumentOnCoordinator (
                string const& dbname,
                string const& collname,
                string const& key,
                TRI_voc_rid_t const rev,
                bool notthisref,
                bool generateDocument,
                triagens::rest::HttpResponse::HttpResponseCode& responseCode,
                string& contentType,
                string& resultBody) {

  // Set a few variables needed for our work:
  ClusterInfo* ci = ClusterInfo::instance();
  ClusterComm* cc = ClusterComm::instance();

  // First determine the collection ID from the name:
  TRI_shared_ptr<CollectionInfo> collinfo = ci->getCollection(dbname, collname);
  if (collinfo->empty()) {
    return TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND;
  }
  string collid = StringUtils::itoa(collinfo->id());

  // If _key is the one and only sharding attribute, we can do this quickly,
  // because we can easily determine which shard is responsible for the 
  // document. Otherwise we have to contact all shards and ask them to
  // delete the document. All but one will not know it.
  // Now find the responsible shard:
  TRI_json_t* json = TRI_CreateArrayJson(TRI_UNKNOWN_MEM_ZONE);
  if (0 == json) {
    return TRI_ERROR_OUT_OF_MEMORY;
  }
  TRI_Insert2ArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "_key", 
                       TRI_CreateStringReference2Json(TRI_UNKNOWN_MEM_ZONE,
                                 key.c_str(), key.size()));
  bool usesDefaultShardingAttributes;
  ShardID shardID;
  int error = ci->getResponsibleShard( collid, json, true, shardID,
                                       usesDefaultShardingAttributes );
  TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);

  // Some stuff to prepare cluster-intern requests:
  ClusterCommResult* res;
  string revstr;
  if (rev != 0) {
    revstr = "?rev="+StringUtils::itoa(rev);
  }
  triagens::rest::HttpRequest::HttpRequestType reqType;
  if (generateDocument) {
    reqType = triagens::rest::HttpRequest::HTTP_REQUEST_GET;
  }
  else {
    reqType = triagens::rest::HttpRequest::HTTP_REQUEST_HEAD;
  }

  if (usesDefaultShardingAttributes) {
    // OK, this is the fast method, we only have to ask one shard:
    if (error == TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND) {
      return TRI_ERROR_CLUSTER_SHARD_GONE;
    }

    // Set up revision string or header:
    map<string, string> headers;
    if (notthisref) {
      headers["If-None-Match"] = revstr;
      revstr = "";
    }

    // Send a synchronous request to that shard using ClusterComm:
    res = cc->syncRequest("", TRI_NewTickServer(), "shard:"+shardID, reqType,
                          "/_db/"+dbname+"/_api/document/"+
                          StringUtils::urlEncode(shardID)+"/"+key+
                          revstr, "", headers, 60.0);
  
    if (res->status == CL_COMM_TIMEOUT) {
      // No reply, we give up:
      delete res;
      return TRI_ERROR_CLUSTER_TIMEOUT;
    }
    if (res->status == CL_COMM_ERROR) {
      // This could be a broken connection or an Http error:
      if (!res->result->isComplete()) {
        delete res;
        return TRI_ERROR_CLUSTER_CONNECTION_LOST;
      }
      // In this case a proper HTTP error was reported by the DBserver,
      // this can be 400 or 404, we simply forward the result.
      // We intentionally fall through here.
    }
    responseCode = static_cast<triagens::rest::HttpResponse::HttpResponseCode>
                              (res->result->getHttpReturnCode());
    contentType = res->result->getContentType(false);
    resultBody = res->result->getBody().str();
    delete res;
    return TRI_ERROR_NO_ERROR;
  }

  // If we get here, the sharding attributes are not only _key, therefore
  // we have to contact everybody:
  map<ShardID, ServerID> shards = collinfo->shardIds();
  map<ShardID, ServerID>::iterator it;
  CoordTransactionID coordTransactionID = TRI_NewTickServer();
  for (it = shards.begin(); it != shards.end(); ++it) {
    map<string, string>* headers = new map<string, string>;

    // Set up revision string or header:
    if (notthisref) {
      (*headers)["If-None-Match"] = revstr;
      revstr = "";
    }

    res = cc->asyncRequest("", coordTransactionID, "shard:"+it->first, reqType,
                          "/_db/"+dbname+"/_api/document/"+
                          StringUtils::urlEncode(it->first)+"/"+key+
                          revstr, 0, false, headers, NULL, 60.0);
    delete res;
  }
  // Now listen to the results:
  int count;
  int nrok = 0;
  for (count = shards.size(); count > 0; count--) {
    res = cc->wait( "", coordTransactionID, 0, "", 0.0);
    if (res->status == CL_COMM_RECEIVED) {
      if (res->answer_code != triagens::rest::HttpResponse::NOT_FOUND ||
          (nrok == 0 && count == 1)) {
        nrok++;
        responseCode = res->answer_code;
        contentType = res->answer->header("content-type");
        resultBody = string(res->answer->body(), res->answer->bodySize());
      }
    }
    delete res;
  }
  // Note that nrok is always at least 1!
  if (nrok > 1) {
    return TRI_ERROR_CLUSTER_GOT_CONTRADICTING_ANSWERS;
  }
  return TRI_ERROR_NO_ERROR;   // the cluster operation was OK, however,
                               // the DBserver could have reported an error.
}

////////////////////////////////////////////////////////////////////////////////
/// @brief get all documents in a coordinator
////////////////////////////////////////////////////////////////////////////////

    int getAllDocumentsOnCoordinator ( 
                 string const& dbname,
                 string const& collname,
                 triagens::rest::HttpResponse::HttpResponseCode& responseCode,
                 string& contentType,
                 string& resultBody ) {

  // Set a few variables needed for our work:
  ClusterInfo* ci = ClusterInfo::instance();
  ClusterComm* cc = ClusterComm::instance();

  // First determine the collection ID from the name:
  TRI_shared_ptr<CollectionInfo> collinfo = ci->getCollection(dbname, collname);
  if (collinfo->empty()) {
    return TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND;
  }
  string collid = StringUtils::itoa(collinfo->id());

  ClusterCommResult* res;

  map<ShardID, ServerID> shards = collinfo->shardIds();
  map<ShardID, ServerID>::iterator it;
  CoordTransactionID coordTransactionID = TRI_NewTickServer();
  for (it = shards.begin(); it != shards.end(); ++it) {
    map<string, string>* headers = new map<string, string>;

    res = cc->asyncRequest("", coordTransactionID, "shard:"+it->first,
                          triagens::rest::HttpRequest::HTTP_REQUEST_GET,
                          "/_db/"+dbname+"/_api/document?collection="+
                          it->first, 0, false, headers, NULL, 3600.0);
    delete res;
  }
  // Now listen to the results:
  int count;
  responseCode = triagens::rest::HttpResponse::OK;
  contentType = "application/json; charset=utf-8";
  resultBody.clear();
  resultBody.reserve(1024*1024);
  resultBody += "{ \"documents\" : [\n";
  char const* p;
  char const* q;
  for (count = shards.size(); count > 0; count--) {
    res = cc->wait( "", coordTransactionID, 0, "", 0.0);
    if (res->status == CL_COMM_TIMEOUT) {
      delete res;
      cc->drop( "", coordTransactionID, 0, "");
      return TRI_ERROR_CLUSTER_TIMEOUT;
    }
    if (res->status == CL_COMM_ERROR || res->status == CL_COMM_DROPPED ||
        res->answer_code == triagens::rest::HttpResponse::NOT_FOUND) {
      delete res;
      cc->drop( "", coordTransactionID, 0, "");
      return TRI_ERROR_INTERNAL;
    }
    p = res->answer->body();
    q = p+res->answer->bodySize();
    while (*p != '\n' && *p != 0) p++;
    p++;
    while (*q != '\n' && q > p) q--;
    if (p != q) {
      resultBody.append(p,q-p);
      resultBody += ",\n";
    }
    delete res;
  }
  if (resultBody[resultBody.size()-2] == ',') {
    resultBody.erase(resultBody.size()-2);
  }
  resultBody += "\n] }\n";
  return TRI_ERROR_NO_ERROR;
}


////////////////////////////////////////////////////////////////////////////////
/// @brief modify a document in a coordinator
////////////////////////////////////////////////////////////////////////////////

int modifyDocumentOnCoordinator (
                 string const& dbname,
                 string const& collname,
                 string const& key,
                 TRI_voc_rid_t const rev,
                 TRI_doc_update_policy_e policy,
                 bool waitForSync,
                 bool isPatch,
                 bool keepNull,   // only counts for isPatch == true
                 TRI_json_t* json,
                 triagens::rest::HttpResponse::HttpResponseCode& responseCode,
                 string& contentType,
                 string& resultBody) {

  // Set a few variables needed for our work:
  ClusterInfo* ci = ClusterInfo::instance();
  ClusterComm* cc = ClusterComm::instance();

  // First determine the collection ID from the name:
  TRI_shared_ptr<CollectionInfo> collinfo = ci->getCollection(dbname, collname);
  if (collinfo->empty()) {
    return TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND;
  }
  string collid = StringUtils::itoa(collinfo->id());

  // We have a fast path and a slow path. The fast path only asks one shard
  // to do the job and the slow path asks them all and expects to get
  // "not found" from all but one shard. We have to cover the following
  // cases:
  //   isPatch == false    (this is a "replace" operation)
  //     Here, the complete new document is given, we assume that we
  //     can read off the responsible shard, therefore can use the fast
  //     path, this is always true if _key is the one and only sharding
  //     attribute, however, if there is any other sharding attribute,
  //     it is possible that the user has changed the values in any of 
  //     them, in that case we will get a "not found" or a "sharding
  //     attributes changed answer" in the fast path. In the latter case
  //     we have to delegate to the slow path.
  //   isPatch == true     (this is an "update" operation)
  //     In this case we might or might not have all sharding attributes
  //     specified in the partial document given. If _key is the one and
  //     only sharding attribute, it is always given, if not all sharding
  //     attributes are explicitly given (at least as value `null`), we must
  //     assume that the fast path cannot be used. If all sharding attributes
  //     are given, we first try the fast path, but might, as above, 
  //     have to use the slow path after all.

  bool usesDefaultShardingAttributes;
  ShardID shardID;
  int error;
  if (isPatch) {
    error = ci->getResponsibleShard( collid, json, false, shardID,
                                     usesDefaultShardingAttributes );
  }
  else {
    error = ci->getResponsibleShard( collid, json, true, shardID,
                                     usesDefaultShardingAttributes );
  }

  if (error == TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND) {
    return error;
  }

  // Some stuff to prepare cluster-internal requests:
  ClusterCommResult* res;
  string revstr;
  if (rev != 0) {
    revstr = "?rev="+StringUtils::itoa(rev);
  }
  triagens::rest::HttpRequest::HttpRequestType reqType;
  if (isPatch) {
    reqType = triagens::rest::HttpRequest::HTTP_REQUEST_PATCH;
    if (!keepNull) {
      if (revstr.empty()) {
        revstr += "?keepNull=";
      }
      else {
        revstr += "&keepNull=";
      }
      revstr += "false";
    }
  }
  else {
    reqType = triagens::rest::HttpRequest::HTTP_REQUEST_PUT;
  }

  string body = JsonHelper::toString(json);
  TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);

  if (!isPatch || 
      error != TRI_ERROR_CLUSTER_NOT_ALL_SHARDING_ATTRIBUTES_GIVEN) {
    // This is the fast method, we only have to ask one shard, unless
    // the we are in isPatch==false and the user has actually changed the
    // sharding attributes

    // Set up revision string or header:
    map<string, string> headers;

    // Send a synchronous request to that shard using ClusterComm:
    res = cc->syncRequest("", TRI_NewTickServer(), "shard:"+shardID, reqType,
                          "/_db/"+dbname+"/_api/document/"+
                          StringUtils::urlEncode(shardID)+"/"+key+
                          revstr, body, headers, 60.0);
  
    if (res->status == CL_COMM_TIMEOUT) {
      // No reply, we give up:
      delete res;
      return TRI_ERROR_CLUSTER_TIMEOUT;
    }
    if (res->status == CL_COMM_ERROR) {
      // This could be a broken connection or an Http error:
      if (!res->result->isComplete()) {
        delete res;
        return TRI_ERROR_CLUSTER_CONNECTION_LOST;
      }
      // In this case a proper HTTP error was reported by the DBserver,
      // this can be 400 or 404, we simply forward the result.
      // We intentionally fall through here.
    }
    // Now we have to distinguish whether we still have to go the slow way:
    responseCode = static_cast<triagens::rest::HttpResponse::HttpResponseCode>
                              (res->result->getHttpReturnCode());
    if (responseCode < triagens::rest::HttpResponse::BAD) {
      // OK, we are done, let's report:
      contentType = res->result->getContentType(false);
      resultBody = res->result->getBody().str();
      delete res;
      return TRI_ERROR_NO_ERROR;
    }
    delete res;
  }

  // If we get here, we have to do it the slow way and contact everybody:
  map<ShardID, ServerID> shards = collinfo->shardIds();
  map<ShardID, ServerID>::iterator it;
  CoordTransactionID coordTransactionID = TRI_NewTickServer();
  for (it = shards.begin(); it != shards.end(); ++it) {
    map<string, string>* headers = new map<string, string>;

    res = cc->asyncRequest("", coordTransactionID, "shard:"+it->first, reqType,
                          "/_db/"+dbname+"/_api/document/"+
                          StringUtils::urlEncode(it->first)+"/"+key+revstr, 
                          &body, false, headers, NULL, 60.0);
    delete res;
  }
  // Now listen to the results:
  int count;
  int nrok = 0;
  for (count = shards.size(); count > 0; count--) {
    res = cc->wait( "", coordTransactionID, 0, "", 0.0);
    if (res->status == CL_COMM_RECEIVED) {
      if (res->answer_code != triagens::rest::HttpResponse::NOT_FOUND ||
          (nrok == 0 && count == 1)) {
        nrok++;
        responseCode = res->answer_code;
        contentType = res->answer->header("content-type");
        resultBody = string(res->answer->body(), res->answer->bodySize());
      }
    }
    delete res;
  }
  // Note that nrok is always at least 1!
  if (nrok > 1) {
    return TRI_ERROR_CLUSTER_GOT_CONTRADICTING_ANSWERS;
  }
  return TRI_ERROR_NO_ERROR;   // the cluster operation was OK, however,
                               // the DBserver could have reported an error.
}

////////////////////////////////////////////////////////////////////////////////
/// @brief creates an edge in a coordinator
////////////////////////////////////////////////////////////////////////////////

int createEdgeOnCoordinator ( 
                 string const& dbname,
                 string const& collname,
                 bool waitForSync,
                 TRI_json_t* json,
                 char const* from,
                 char const* to,
                 triagens::rest::HttpResponse::HttpResponseCode& responseCode,
                 string& contentType,
                 string& resultBody) {

  // Set a few variables needed for our work:
  ClusterInfo* ci = ClusterInfo::instance();
  ClusterComm* cc = ClusterComm::instance();

  // First determine the collection ID from the name:
  TRI_shared_ptr<CollectionInfo> collinfo = ci->getCollection(dbname, collname);
  if (collinfo->empty()) {
    return TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND;
  }
  string collid = StringUtils::itoa(collinfo->id());

  // Sort out the _key attribute:
  // The user is allowed to specify _key, provided that _key is the one
  // and only sharding attribute, because in this case we can delegate
  // the responsibility to make _key attributes unique to the responsible
  // shard. Otherwise, we ensure uniqueness here and now by taking a
  // cluster-wide unique number. Note that we only know the sharding 
  // attributes a bit further down the line when we have determined
  // the responsible shard.
  TRI_json_t* subjson = TRI_LookupArrayJson(json, "_key");
  bool userSpecifiedKey = false;
  string _key;
  if (0 == subjson) {
    // The user did not specify a key, let's create one:
    uint64_t uid = ci->uniqid();
    _key = triagens::basics::StringUtils::itoa(uid);
    TRI_InsertArrayJson(TRI_UNKNOWN_MEM_ZONE, json, "_key",
                        TRI_CreateStringReference2Json(TRI_UNKNOWN_MEM_ZONE, 
                                                     _key.c_str(), _key.size()));
  }
  else {
    userSpecifiedKey = true;
  }

  // Now find the responsible shard:
  bool usesDefaultShardingAttributes;
  ShardID shardID;
  int error = ci->getResponsibleShard( collid, json, true, shardID,
                                       usesDefaultShardingAttributes );
  if (error == TRI_ERROR_ARANGO_COLLECTION_NOT_FOUND) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    return TRI_ERROR_CLUSTER_SHARD_GONE;
  }

  // Now perform the above mentioned check:
  if (userSpecifiedKey && !usesDefaultShardingAttributes) {
    TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);
    return TRI_ERROR_CLUSTER_MUST_NOT_SPECIFY_KEY;
  }

  string body = JsonHelper::toString(json);
  TRI_FreeJson(TRI_UNKNOWN_MEM_ZONE, json);

  // Send a synchronous request to that shard using ClusterComm:
  ClusterCommResult* res;
  map<string, string> headers;
  res = cc->syncRequest("", TRI_NewTickServer(), "shard:"+shardID,
                        triagens::rest::HttpRequest::HTTP_REQUEST_POST,
                        "/_db/"+dbname+"/_api/edge?collection="+
                        StringUtils::urlEncode(shardID)+"&waitForSync="+
                        (waitForSync ? "true" : "false")+
                        "&from="+from+"&to="+to, body, headers, 60.0);
  
  if (res->status == CL_COMM_TIMEOUT) {
    // No reply, we give up:
    delete res;
    return TRI_ERROR_CLUSTER_TIMEOUT;
  }
  if (res->status == CL_COMM_ERROR) {
    // This could be a broken connection or an Http error:
    if (!res->result->isComplete()) {
      delete res;
      return TRI_ERROR_CLUSTER_CONNECTION_LOST;
    }
    // In this case a proper HTTP error was reported by the DBserver,
    // this can be 400 or 404, we simply forward the result.
    // We intentionally fall through here.
  }
  responseCode = static_cast<triagens::rest::HttpResponse::HttpResponseCode>
                            (res->result->getHttpReturnCode());
  contentType = res->result->getContentType(false);
  resultBody = res->result->getBody().str();
  delete res;
  return TRI_ERROR_NO_ERROR;
}

  }  // namespace arango
}  // namespace triagens

////////////////////////////////////////////////////////////////////////////////
/// @brief
////////////////////////////////////////////////////////////////////////////////

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:

