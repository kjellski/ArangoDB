/*jslint indent: 2, nomen: true, maxlen: 120, sloppy: true, vars: true, white: true, regexp: true, plusplus: true, continue: true */
/*global require, exports */

////////////////////////////////////////////////////////////////////////////////
/// @brief Foxx Authentication
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2013 triagens GmbH, Cologne, Germany
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
/// @author Copyright 2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

var arangodb = require("org/arangodb");
var db = require("org/arangodb").db;
var crypto = require("org/arangodb/crypto");
var internal = require("internal");

// -----------------------------------------------------------------------------
// --SECTION--                                                  helper functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Foxx
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief constructor
////////////////////////////////////////////////////////////////////////////////

function generateToken () {
  'use strict';

  return internal.genRandomAlphaNumbers(32);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief deep-copies a document
////////////////////////////////////////////////////////////////////////////////

function cloneDocument (obj) {
  "use strict";

  if (obj === null || typeof(obj) !== "object") {
    return obj;
  }
 
  var copy, a; 
  if (Array.isArray(obj)) {
    copy = [ ];
    obj.forEach(function (i) {
      copy.push(cloneDocument(i));
    });
  }
  else if (obj instanceof Object) {
    copy = { };
    for (a in obj) {
      if (obj.hasOwnProperty(a)) {
        copy[a] = cloneDocument(obj[a]);
      }
    }
  }

  return copy;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief checks whether the plain text password matches the encoded one
////////////////////////////////////////////////////////////////////////////////

function checkPassword (plain, encoded) {
  'use strict';

  var salted = encoded.substr(3, 8) + plain;
  var hex = crypto.sha256(salted);

  return (encoded.substr(12) === hex);
}

////////////////////////////////////////////////////////////////////////////////
/// @brief encodes a password
////////////////////////////////////////////////////////////////////////////////

function encodePassword (password) {
  'use strict';

  var salt;
  var encoded;

  var random = crypto.rand();
  if (random === undefined) {
    random = "time:" + internal.time();
  }
  else {
    random = "random:" + random;
  }

  salt = crypto.sha256(random);
  salt = salt.substr(0,8);

  encoded = "$1$" + salt + "$" + crypto.sha256(salt + password);
   
  return encoded;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                        FOXX-USERS
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Foxx
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief constructor
////////////////////////////////////////////////////////////////////////////////

function FoxxUsers (applicationContext) {
  'use strict';

  this._collectionName = applicationContext.collectionName("users");
  this._collection = null;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Foxx
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the collection
////////////////////////////////////////////////////////////////////////////////

FoxxUsers.prototype.storage = function () {
  'use strict';

  if (this._collection === null) {
    this._collection = db._collection(this._collectionName);
  
    if (! this._collection) { 
      throw new Error("users collection not found");
    }
  }

  return this._collection;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief validate a user identifier
////////////////////////////////////////////////////////////////////////////////

FoxxUsers.prototype._validateIdentifier = function (identifier, allowObject) {
  'use strict';

  if (allowObject) {
    if (typeof identifier === 'object' && identifier.hasOwnProperty("identifier")) {
      identifier = identifier.identifier;
    }
  }

  if (typeof identifier !== 'string') {
    throw new TypeError("invalid type for 'identifier'");
  }

  if (identifier.length === 0) {
    throw new Error("invalid user identifier");
  }

  return identifier;
};

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Foxx
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief sets up the users collection
////////////////////////////////////////////////////////////////////////////////

FoxxUsers.prototype.setup = function (options) {
  'use strict';

  var journalSize;

  if (typeof options === 'object' && options.hasOwnProperty('journalSize')) {
    journalSize = options.journalSize;
  }

  var createOptions = {
    journalSize : journalSize || 2 * 1024 * 1024
  };

  if (! db._collection(this._collectionName)) {
    db._create(this._collectionName, createOptions);
  }
 
  this.storage().ensureUniqueConstraint("identifier");
};

////////////////////////////////////////////////////////////////////////////////
/// @brief tears down the users collection
////////////////////////////////////////////////////////////////////////////////

FoxxUsers.prototype.teardown = function () {
  'use strict';

  var c = db._collection(this._collectionName);

  if (c) {
    c.drop();
  }
};

////////////////////////////////////////////////////////////////////////////////
/// @brief flushes all users
////////////////////////////////////////////////////////////////////////////////

FoxxUsers.prototype.flush = function () {
  'use strict';
 
  this.storage().truncate();
};

////////////////////////////////////////////////////////////////////////////////
/// @brief add a user
////////////////////////////////////////////////////////////////////////////////

FoxxUsers.prototype.add = function (identifier, password, active, data) {
  'use strict';
 
  var c = this.storage();
  identifier = this._validateIdentifier(identifier, false);
  
  if (typeof password !== 'string') {
    throw new TypeError("invalid type for 'password'");
  }

  if (active !== undefined && typeof active !== "boolean") {
    throw new TypeError("invalid type for 'active'");
  }

  var user = {
    identifier: identifier,
    password:   encodePassword(password),
    active:     active || true,
    data:       data || { }
  };

  db._executeTransaction({
    collections: {
      write: c.name()
    },
    action: function (params) {
      var c = db._collection(params.cn);
      var u = c.firstExample({ identifier: params.user.identifier });

      if (u === null) {
        c.save(params.user);
      }
      else {
        c.replace(u._key, params.user);
      }
    },
    params: {
      cn: c.name(),
      user: user
    }
  });

  delete user.password;

  return user;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief update a user
////////////////////////////////////////////////////////////////////////////////

FoxxUsers.prototype.updateData = function (identifier, data) {
  'use strict';

  var c = this.storage();
  identifier = this._validateIdentifier(identifier, true);

  db._executeTransaction({
    collections: {
      write: c.name()
    },
    action: function (params) {
      var c = db._collection(params.cn);
      var u = c.firstExample({ identifier: params.identifier });

      if (u === null) {
        throw new Error("user not found");
      }
      c.update(u._key, params.data, true, false);
    },
    params: {
      cn: c.name(),
      identifier: identifier,
      data: data || { }
    }
  });

  return true;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief set activity flag
////////////////////////////////////////////////////////////////////////////////

FoxxUsers.prototype.setActive = function (identifier, active) {
  'use strict';
 
  var c = this.storage();
  identifier = this._validateIdentifier(identifier, true);

  var user = c.firstExample({ identifier: identifier });

  if (user === null) {
    return false;
  }

  // must clone because shaped json cannot be modified
  var doc = cloneDocument(user);
  doc.active = active;
  c.update(doc._key, doc, true, false);

  return true;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief set password
////////////////////////////////////////////////////////////////////////////////

FoxxUsers.prototype.setPassword = function (identifier, password) {
  'use strict';
 
  var c = this.storage();
  identifier = this._validateIdentifier(identifier, true);
  
  if (typeof password !== 'string') {
    throw new TypeError("invalid type for 'password'");
  }

  var user = c.firstExample({ identifier: identifier });

  if (user === null) {
    return false;
  }

  var doc = cloneDocument(user);
  doc.password = encodePassword(password);
  c.update(doc._key, doc, true, false);

  return true;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief remove a user
////////////////////////////////////////////////////////////////////////////////

FoxxUsers.prototype.remove = function (identifier) {
  'use strict';
 
  var c = this.storage();
  identifier = this._validateIdentifier(identifier, true);

  var user = c.firstExample({ identifier: identifier });

  if (user === null) {
    return false;
  }

  try {
    c.remove(user._key);
  }
  catch (err) {
  }

  return true;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief returns a user
////////////////////////////////////////////////////////////////////////////////

FoxxUsers.prototype.get = function (identifier) {
  'use strict';
 
  var c = this.storage();
  identifier = this._validateIdentifier(identifier, true);

  var user = c.firstExample({ identifier: identifier });

  if (user === null) {
    throw new Error("user not found");
  }

  delete user.password;
  
  return user;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief check whether a user is valid 
////////////////////////////////////////////////////////////////////////////////

FoxxUsers.prototype.isValid = function (identifier, password) {
  'use strict';
 
  var c = this.storage();
  identifier = this._validateIdentifier(identifier, false);

  var user = c.firstExample({ identifier: identifier });

  if (user === null) {
    return false;
  }

  if (! user.active) {
    return false;
  }

  if (! checkPassword(password, user.password)) {
    return false;
  }

  delete user.password;
  
  return user;
};

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                          SESSIONS
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Foxx
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief constructor
////////////////////////////////////////////////////////////////////////////////
  
function FoxxSessions (applicationContext, options) {
  'use strict';

  this._applicationContext = applicationContext;
  this._options = options || { };
  this._collection = null;

  if (! this._options.hasOwnProperty("minUpdateResoultion")) {
    this._options.minUpdateResolution = 10;
  }
  
  if (this._options.hasOwnProperty("collectionName")) {
    this._collectionName = this._options.collectionName;
  }
  else {
    this._collectionName = applicationContext.collectionName("sessions");
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                 private functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Foxx
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief create a session object from a document
////////////////////////////////////////////////////////////////////////////////

FoxxSessions.prototype._toObject = function (session) {
  var that = this;

  return {
    _changed: false,
    _key: session._key,
    identifier: session.identifier,
    expires: session.expires,
    data: session.data,

    set: function (key, value) {
      this.data[key] = value;
      this._changed = true;
    },

    get: function (key) {
      return this.data[key];
    },

    update: function () {
      if (! this._changed) {
        var oldExpires = this.expires;
        var newExpires = internal.time() + that._options.lifetime;

        if (newExpires - oldExpires > that._options.minUpdateResolution) {
          this.expires = newExpires;
          this._changed = true;
        }
      }

      if (this._changed) {
        that.update(this._key, this.data);
        this._changed = false;
      }
    }
  };
};

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Foxx
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief sets up the sessions
////////////////////////////////////////////////////////////////////////////////

FoxxSessions.prototype.setup = function (options) {
  'use strict';
  
  var journalSize;

  if (typeof options === 'object' && options.hasOwnProperty('journalSize')) {
    journalSize = options.journalSize;
  }

  var createOptions = {
    journalSize : journalSize || 4 * 1024 * 1024
  };

  if (! db._collection(this._collectionName)) {
    db._create(this._collectionName, createOptions);
  }

  this.storage().ensureHashIndex("identifier");
};

////////////////////////////////////////////////////////////////////////////////
/// @brief tears down the sessions
////////////////////////////////////////////////////////////////////////////////

FoxxSessions.prototype.teardown = function () {
  'use strict';
  
  var c = db._collection(this._collectionName);

  if (c) {
    c.drop();
  }
};

////////////////////////////////////////////////////////////////////////////////
/// @brief return the collection
////////////////////////////////////////////////////////////////////////////////

FoxxSessions.prototype.storage = function () {
  'use strict';

  if (this._collection === null) {
    this._collection = db._collection(this._collectionName);
    
    if (! this._collection) { 
      throw new Error("sessions collection not found");
    }
  }

  return this._collection;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief generate a new session
////////////////////////////////////////////////////////////////////////////////

FoxxSessions.prototype.generate = function (identifier, data) {
  'use strict';

  if (typeof identifier !== "string" || identifier.length === 0) {
    throw new TypeError("invalid type for 'identifier'");
  }
  
  if (! this._options.hasOwnProperty("lifetime")) {
    throw new Error("no value specified for 'lifetime'");
  }

  var storage = this.storage();

  if (! this._options.allowMultiple) {
    // remove previous existing sessions
    storage.byExample({ identifier: identifier }).toArray().forEach(function (s) {
      storage.remove(s);
    });
  }

  while (true) { 
    var token = generateToken();
    var session = {
      _key: token, 
      expires: internal.time() + this._options.lifetime,
      identifier: identifier,
      data: data || { }
    };

    try {
      storage.save(session);
      return this._toObject(session);
    }
    catch (err) {
      // we might have generated the same key again
      if (err.hasOwnProperty("errorNum") &&
          err.errorNum === internal.errors.ERROR_ARANGO_UNIQUE_CONSTRAINT_VIOLATED) {
        // duplicate key, try again
        continue;
      }

      throw err;
    }
  }
};

////////////////////////////////////////////////////////////////////////////////
/// @brief update a session
////////////////////////////////////////////////////////////////////////////////

FoxxSessions.prototype.update = function (token, data) {
  'use strict';
      
  this.storage().update(token, { 
    expires: internal.time() + this._options.lifetime,
    data: data 
  }, true, false);
};

////////////////////////////////////////////////////////////////////////////////
/// @brief terminate a session
////////////////////////////////////////////////////////////////////////////////

FoxxSessions.prototype.terminate = function (token) {
  'use strict';

  try {
    this.storage().remove(token);
  }
  catch (err) {
    // some error, e.g. document not found. we don't care
  }
};

////////////////////////////////////////////////////////////////////////////////
/// @brief get an existing session
////////////////////////////////////////////////////////////////////////////////

FoxxSessions.prototype.get = function (token) {
  'use strict';

  var storage = this.storage();

  try {
    var session = storage.document(token);

    if (session.expires >= internal.time()) {
      // session still valid

      var lifetime = this._options.lifetime;

      return {
        errorNum: internal.errors.ERROR_NO_ERROR,
        session : this._toObject(session)
      };
    }

    // expired
    return { 
      errorNum: internal.errors.ERROR_SESSION_EXPIRED.code
    };
  }
  catch (err) {
    // document not found etc.
  }

  return {
    errorNum: internal.errors.ERROR_SESSION_UNKNOWN.code
  };
};

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                             COOKIE AUTHENTICATION
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Foxx
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief constructor
////////////////////////////////////////////////////////////////////////////////

function FoxxCookieAuthentication (applicationContext, options) {
  'use strict';

  options = options || { };
    
  this._applicationContext = applicationContext;

  this._options = {
    name: options.name || this._applicationContext.name + "-session",
    lifetime: options.lifetime || 3600,
    path: options.path || this._applicationContext.mount,
    domain: options.path || undefined,
    secure: options.secure || false,
    httpOnly: options.httpOnly || false
  };
  
  this._collectionName = applicationContext.collectionName("sessions");
  this._collection = null;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Foxx
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief get a cookie from the request
////////////////////////////////////////////////////////////////////////////////

FoxxCookieAuthentication.prototype.getTokenFromRequest = function (req) {
  'use strict';

  if (! req.hasOwnProperty("cookies")) {
    return null;
  }

  if (! req.cookies.hasOwnProperty(this._options.name)) {
    return null;
  }

  return req.cookies[this._options.name];
};

////////////////////////////////////////////////////////////////////////////////
/// @brief register a cookie in the request
////////////////////////////////////////////////////////////////////////////////

FoxxCookieAuthentication.prototype.setCookie = function (res, value) {
  'use strict';
  
  var name = this._options.name;
  var cookie = {
    name: name,
    value: value,
    lifeTime: (value === null || value === "") ? 0 : this._options.lifetime,
    path: this._options.path,
    secure: this._options.secure,
    domain: this._options.domain,
    httpOnly: this._options.httpOnly
  };

  if (! res.hasOwnProperty("cookies")) {
    res.cookies = [ ];
  }

  if (! Array.isArray(res.cookies)) {
    res.cookies = [ res.cookies ];
  }

  var i, n = res.cookies.length;
  for (i = 0; i < n; ++i) {
    if (res.cookies[i].name === name) {
      // found existing cookie. overwrite it
      res.cookies[i] = cookie;
      return;
    }
  }

  res.cookies.push(cookie);
};

////////////////////////////////////////////////////////////////////////////////
/// @brief check whether the request contains authentication data
////////////////////////////////////////////////////////////////////////////////

FoxxCookieAuthentication.prototype.getAuthenticationData = function (req) {
  'use strict';

  var token = this.getTokenFromRequest(req);

  if (token === null) {
    return null;
  }

  return {
    token: token
  };
};

////////////////////////////////////////////////////////////////////////////////
/// @brief generate authentication data
////////////////////////////////////////////////////////////////////////////////

FoxxCookieAuthentication.prototype.beginSession = function (req, res, token, identifier, data) {
  'use strict';
  
  this.setCookie(res, token);
};

////////////////////////////////////////////////////////////////////////////////
/// @brief delete authentication data
////////////////////////////////////////////////////////////////////////////////

FoxxCookieAuthentication.prototype.endSession = function (req, res) {
  'use strict';

  this.setCookie(res, "");
};

////////////////////////////////////////////////////////////////////////////////
/// @brief check whether the authentication handler is responsible
////////////////////////////////////////////////////////////////////////////////

FoxxCookieAuthentication.prototype.isResponsible = function (req) {
  'use strict';

  return true;
};

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                               FOXX AUTHENTICATION
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Foxx
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief constructor
////////////////////////////////////////////////////////////////////////////////

function FoxxAuthentication (applicationContext, sessions, authenticators) {
  'use strict';

  this._applicationContext = applicationContext;
  this._sessions = sessions;

  if (! Array.isArray(authenticators)) {
    authenticators = [ authenticators ];
  }

  this._authenticators = authenticators;
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Foxx
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief runs the authentication
////////////////////////////////////////////////////////////////////////////////

FoxxAuthentication.prototype.authenticate = function (req) {
  'use strict';

  var i, n = this._authenticators.length;

  for (i = 0; i < n; ++i) {
    var authenticator = this._authenticators[i];

    var data = authenticator.getAuthenticationData(req);

    if (data !== null) {
      var session = this._sessions.get(data.token);

      if (session) {
        return session;
      }
    }
  }

  return {
    errorNum: internal.errors.ERROR_SESSION_UNKNOWN.code
  };
};

////////////////////////////////////////////////////////////////////////////////
/// @brief generate authentication data
////////////////////////////////////////////////////////////////////////////////

FoxxAuthentication.prototype.beginSession = function (req, res, identifier, data) {
  'use strict';

  var session = this._sessions.generate(identifier, data);
  var i, n = this._authenticators.length;

  for (i = 0; i < n; ++i) {
    var authenticator = this._authenticators[i];

    if (authenticator.isResponsible(req)) {
      authenticator.beginSession(req, res, session._key, identifier, data);
    }
  }

  return session;
};

////////////////////////////////////////////////////////////////////////////////
/// @brief delete authentication data
////////////////////////////////////////////////////////////////////////////////

FoxxAuthentication.prototype.endSession = function (req, res, token) {
  'use strict';

  var i, n = this._authenticators.length;

  for (i = 0; i < n; ++i) {
    var authenticator = this._authenticators[i];

    if (authenticator.isResponsible(req)) {
      authenticator.endSession(req, res);
    }
  }

  this._sessions.terminate(token);
};

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    module exports
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Foxx
/// @{
////////////////////////////////////////////////////////////////////////////////

exports.FoxxUsers                = FoxxUsers;
exports.FoxxSessions             = FoxxSessions;
exports.FoxxCookieAuthentication = FoxxCookieAuthentication;
exports.FoxxAuthentication       = FoxxAuthentication;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

/// Local Variables:
/// mode: outline-minor
/// outline-regexp: "/// @brief\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}\\|/\\*jslint"
/// End:
