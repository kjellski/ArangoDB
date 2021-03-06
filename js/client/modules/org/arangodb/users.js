/*jslint indent: 2, nomen: true, maxlen: 120, sloppy: true, vars: true, white: true, plusplus: true */
/*global require, exports */

////////////////////////////////////////////////////////////////////////////////
/// @brief User management
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
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
/// @author Copyright 2012-2014, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

var internal = require("internal");
var arangodb = require("org/arangodb");
var arangosh = require("org/arangodb/arangosh");

// -----------------------------------------------------------------------------
// --SECTION--                                       module "org/arangodb/users"
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a new user
////////////////////////////////////////////////////////////////////////////////

exports.save = function (user, passwd, active, extra, changePassword) {
  var db = internal.db;

  var uri = "_api/user/";
  var data = {user: user};

  if (passwd !== undefined) {
    data.passwd = passwd;
  }

  if (active !== undefined) {
    data.active = active;
  }

  if (extra !== undefined) {
    data.extra = extra;
  }

  if (changePassword !== undefined) {
    data.changePassword = changePassword;
  }

  var requestResult = db._connection.POST(uri, JSON.stringify(data));
  return arangosh.checkRequestResult(requestResult);
};

////////////////////////////////////////////////////////////////////////////////
/// @brief replaces an existing user
////////////////////////////////////////////////////////////////////////////////

exports.replace = function (user, passwd, active, extra, changePassword) {
  var db = internal.db;

  var uri = "_api/user/" + encodeURIComponent(user);
  var data = {
    passwd: passwd,
    active: active,
    extra: extra,
    changePassword: changePassword
  };

  var requestResult = db._connection.PUT(uri, JSON.stringify(data));
  return arangosh.checkRequestResult(requestResult);
};

////////////////////////////////////////////////////////////////////////////////
/// @brief updates an existing user
////////////////////////////////////////////////////////////////////////////////

exports.update = function (user, passwd, active, extra, changePassword) {
  var db = internal.db;

  var uri = "_api/user/" + encodeURIComponent(user);
  var data = {};

  if (passwd !== undefined) {
    data.passwd = passwd;
  }

  if (active !== undefined) {
    data.active = active;
  }

  if (extra !== undefined) {
    data.extra = extra;
  }

  if (changePassword !== undefined) {
    data.changePassword = changePassword;
  }

  var requestResult = db._connection.PATCH(uri, JSON.stringify(data));
  return arangosh.checkRequestResult(requestResult);
};

////////////////////////////////////////////////////////////////////////////////
/// @brief deletes an existing user
////////////////////////////////////////////////////////////////////////////////

exports.remove = function (user) {
  var db = internal.db;

  var uri = "_api/user/" + encodeURIComponent(user);

  var requestResult = db._connection.DELETE(uri);
  arangosh.checkRequestResult(requestResult);
};

////////////////////////////////////////////////////////////////////////////////
/// @brief gets an existing user
////////////////////////////////////////////////////////////////////////////////

exports.document = function (user) {
  var db = internal.db;

  var uri = "_api/user/" + encodeURIComponent(user);

  var requestResult = db._connection.GET(uri);
  return arangosh.checkRequestResult(requestResult);
};

////////////////////////////////////////////////////////////////////////////////
/// @brief checks whether a combination of username / password is valid.
////////////////////////////////////////////////////////////////////////////////

exports.isValid = function (user, password) {
  var db = internal.db;

  var uri = "_api/user/" + encodeURIComponent(user);
  var data = { passwd: password };

  var requestResult = db._connection.POST(uri, JSON.stringify(data));

  if (requestResult.error !== undefined && requestResult.error) {
    if (requestResult.errorNum === arangodb.errors.ERROR_USER_NOT_FOUND.code) {
      return false;
    }

    return arangosh.checkRequestResult(requestResult);
  }

  return requestResult.result;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief gets all existing users
////////////////////////////////////////////////////////////////////////////////

exports.all = function () {
  var db = internal.db;

  var uri = "_api/user";

  var requestResult = db._connection.GET(uri);
  return arangosh.checkRequestResult(requestResult).result;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief reloads the user authentication data
////////////////////////////////////////////////////////////////////////////////

exports.reload = function () {
  var db = internal.db;

  var requestResult = db._connection.GET("_admin/auth/reload");
  arangosh.checkRequestResult(requestResult);
};

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}\\|/\\*jslint"
// End:
