/*jslint indent: 2, nomen: true, maxlen: 80 */
/*global require, db, assertEqual, assertTrue, ArangoCollection */

////////////////////////////////////////////////////////////////////////////////
/// @brief test the unique constraint
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
/// @author Dr. Frank Celler, Lucas Dohmen
/// @author Copyright 2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

var jsunity = require("jsunity");
var internal = require("internal");
var errors = internal.errors;

// -----------------------------------------------------------------------------
// --SECTION--                                                     basic methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief test suite: Creation
////////////////////////////////////////////////////////////////////////////////

function HashIndexSuite() {
  var cn = "UnitTestsCollectionHash";
  var collection = null;

  var sorter = function (l, r) {
    if (l.length != r.length) {
      return l.length - r.length < 0 ? -1 : 1;
    }

    // length is equal
    for (i = 0; i < l.length; ++i) {
      if (l[i] != r[i]) {
        return l[i] < r[i] ? -1 : 1;
      }
    }

    return 0;
  };

  return {

////////////////////////////////////////////////////////////////////////////////
/// @brief set up
////////////////////////////////////////////////////////////////////////////////

    setUp : function () {
      internal.db._drop(cn);
      collection = internal.db._create(cn, { waitForSync : false });
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief tear down
////////////////////////////////////////////////////////////////////////////////

    tearDown : function () {
      // try...catch is necessary as some tests delete the collection itself!
      try {
        collection.unload();
        collection.drop();
      }
      catch (err) {
      }

      collection = null;
      internal.wait(0.0);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test: hash index creation
////////////////////////////////////////////////////////////////////////////////

    testCreationUniqueConstraint : function () {
      var idx = collection.ensureHashIndex("a");
      var id = idx.id;

      assertNotEqual(0, id);
      assertEqual("hash", idx.type);
      assertEqual(false, idx.unique);
      assertEqual(["a"], idx.fields);
      assertEqual(true, idx.isNewlyCreated);

      idx = collection.ensureHashIndex("a");

      assertEqual(id, idx.id);
      assertEqual("hash", idx.type);
      assertEqual(false, idx.unique);
      assertEqual(["a"], idx.fields);
      assertEqual(false, idx.isNewlyCreated);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test: permuted attributes
////////////////////////////////////////////////////////////////////////////////

    testCreationPermutedUniqueConstraint : function () {
      var idx = collection.ensureHashIndex("a", "b");
      var id = idx.id;

      assertNotEqual(0, id);
      assertEqual("hash", idx.type);
      assertEqual(false, idx.unique);
      assertEqual(["a","b"].sort(), idx.fields.sort());
      assertEqual(true, idx.isNewlyCreated);

      idx = collection.ensureHashIndex("b", "a");

      assertEqual(id, idx.id);
      assertEqual("hash", idx.type);
      assertEqual(false, idx.unique);
      assertEqual(["a","b"].sort(), idx.fields.sort());
      assertEqual(false, idx.isNewlyCreated);

    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test: documents
////////////////////////////////////////////////////////////////////////////////

    testUniqueDocuments : function () {
      var idx = collection.ensureHashIndex("a", "b");

      assertEqual("hash", idx.type);
      assertEqual(false, idx.unique);
      assertEqual(["a","b"].sort(), idx.fields.sort());
      assertEqual(true, idx.isNewlyCreated);

      collection.save({ a : 1, b : 1 });
      collection.save({ a : 1, b : 1 });

      collection.save({ a : 1 });
      collection.save({ a : 1 });
      collection.save({ a : null, b : 1 });
      collection.save({ a : null, b : 1 });
      collection.save({ c : 1 });
      collection.save({ c : 1 });
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test: documents
////////////////////////////////////////////////////////////////////////////////

    testReadDocuments : function () {
      var idx = collection.ensureHashIndex("a", "b");
      var fun = function(d) { return d._id; };

      assertEqual("hash", idx.type);
      assertEqual(false, idx.unique);
      assertEqual(["a", "b"].sort(), idx.fields.sort());
      assertEqual(true, idx.isNewlyCreated);

      var d1 = collection.save({ a : 1, b : 1 })._id;
      var d2 = collection.save({ a : 2, b : 1 })._id;
      collection.save({ a : 3, b : 1 })._id;
      collection.save({ a : 4, b : 1 })._id;
      collection.save({ a : 4, b : 2 })._id;
      var d6 = collection.save({ a : 1, b : 1 })._id;

      var d7 = collection.save({ a : 1 })._id;
      var d8 = collection.save({ a : 1 })._id;
      collection.save({ a : null, b : 1 })._id;
      collection.save({ a : null, b : 1 })._id;
      var d11 = collection.save({ c : 1 })._id;
      var d12 = collection.save({ c : 1 })._id;

      var s = collection.byExampleHash(idx.id, { a : 2, b : 1 });
      assertEqual(1, s.count());
      assertEqual([d2], s.toArray().map(fun));
      
      s = collection.byExampleHash(idx.id, { b : 1, a : 2 });
      assertEqual(1, s.count());
      assertEqual([d2], s.toArray().map(fun));

      s = collection.byExampleHash(idx.id, { a : 1, b : 1 });
      assertEqual(2, s.count());
      assertEqual([d1, d6], s.toArray().map(fun).sort(sorter));
      
      s = collection.byExampleHash(idx.id, { b : 1, a : 1 });
      assertEqual(2, s.count());
      assertEqual([d1, d6], s.toArray().map(fun).sort(sorter));

      s = collection.byExampleHash(idx.id, { a : 1, b : null });
      assertEqual(2, s.count());
      assertEqual([d7, d8], s.toArray().map(fun).sort(sorter));
      
      s = collection.byExampleHash(idx.id, { b : null, a : 1 });
      assertEqual(2, s.count());
      assertEqual([d7, d8], s.toArray().map(fun).sort(sorter));

      try {
        collection.byExampleHash(idx.id, { a : 1 }).toArray();
        fail();
      }
      catch (err1) {
        assertEqual(errors.ERROR_ARANGO_NO_INDEX.code, err1.errorNum);
      }

      try {
        collection.byExampleHash(idx.id, { a : null }).toArray();
        fail();
      }
      catch (err2) {
        assertEqual(errors.ERROR_ARANGO_NO_INDEX.code, err2.errorNum);
      }

      try {
        collection.byExampleHash(idx.id, { c : 1 }).toArray();
      }
      catch (err3) {
        assertEqual(errors.ERROR_ARANGO_NO_INDEX.code, err3.errorNum);
      }
      
      idx = collection.ensureHashIndex("c");
      s = collection.byExampleHash(idx.id, { c : 1 });

      assertEqual(2, s.count());
      assertEqual([d11, d12], s.toArray().map(fun).sort(sorter));
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test: documents of an unloaded collection
////////////////////////////////////////////////////////////////////////////////

    testReadDocumentsUnloaded : function () {
      var idx = collection.ensureHashIndex("a");

      collection.save({ a : 1, b : 1 })._id;
      var d2 = collection.save({ a : 2, b : 1 })._id;
      collection.save({ a : 3, b : 1 })._id;

      collection.unload();
      internal.wait(4);

      var s = collection.byExampleHash(idx.id, { a : 2 });
      assertEqual(1, s.count());
      assertEqual(d2, s.toArray()[0]._id);
      
      collection.drop();

      try {
        s = collection.byExampleHash(idx.id, { a : 2, b : 1 }).toArray();
        fail();
      }
      catch (err) {
        assertEqual(errors.ERROR_ARANGO_COLLECTION_NOT_FOUND.code, err.errorNum);
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test: combination of indexes
////////////////////////////////////////////////////////////////////////////////

    testMultiIndexViolation1 : function () {
      collection.ensureUniqueConstraint("a");
      collection.ensureSkiplist("b");

      collection.save({ a : "test1", b : 1});
      try {
        collection.save({ a : "test1", b : 1});
        fail();
      }
      catch (err1) {
      }
        
      var doc1 = collection.save({ a : "test2", b : 1});
      assertTrue(doc1._key !== "");
      
      try {
        collection.save({ a : "test1", b : 1});
        fail();
      }
      catch (err2) {
      }
      
      var doc2 = collection.save({ a : "test3", b : 1});
      assertTrue(doc2._key !== "");
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test: combination of indexes
////////////////////////////////////////////////////////////////////////////////

    testMultiIndexViolation2 : function () {
      collection.ensureUniqueSkiplist("a");
      collection.ensureHashIndex("b");

      collection.save({ a : "test1", b : 1});
      try {
        collection.save({ a : "test1", b : 1});
        fail();
      }
      catch (err1) {
      }
        
      var doc1 = collection.save({ a : "test2", b : 1});
      assertTrue(doc1._key !== "");
      
      try {
        collection.save({ a : "test1", b : 1});
        fail();
      }
      catch (err2) {
      }
      
      var doc2 = collection.save({ a : "test3", b : 1});
      assertTrue(doc2._key !== "");
    }

  };
}

// -----------------------------------------------------------------------------
// --SECTION--                                                              main
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief executes the test suites
////////////////////////////////////////////////////////////////////////////////

jsunity.run(HashIndexSuite);

return jsunity.done();

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// @addtogroup\\|// --SECTION--\\|/// @page\\|/// @}\\)"
// End:
