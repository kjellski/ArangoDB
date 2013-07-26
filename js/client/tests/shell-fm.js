////////////////////////////////////////////////////////////////////////////////
/// @brief test the foxx manager
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
/// @author Copyright 2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

var jsunity = require("jsunity");
var fm = require("org/arangodb/foxx-manager");
var arango = require("org/arangodb").arango;

// -----------------------------------------------------------------------------
// --SECTION--                                                foxx manager tests
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief test suite
////////////////////////////////////////////////////////////////////////////////

function FoxxManagerSuite () {

  return {

////////////////////////////////////////////////////////////////////////////////
/// @brief test update
////////////////////////////////////////////////////////////////////////////////

    testUpdate : function () {
      fm.update();

      var result = fm.availableJson();
      var i, n;

      n = result.length;
      assertTrue(n > 0);

      // validate the results structure
      for (i = 0; i < n; ++i) {
        var entry = result[i];

        assertTrue(entry.hasOwnProperty("description"));
        assertTrue(entry.hasOwnProperty("name"));
        assertTrue(entry.hasOwnProperty("author"));
        assertTrue(entry.hasOwnProperty("latestVersion"));
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test available
////////////////////////////////////////////////////////////////////////////////
/*
    testFetch : function () {
      fm.update();
      fm.purge("itzpapalotl");

      fm.fetch("github", "jsteemann/itzpapalotl");
    },
*/
////////////////////////////////////////////////////////////////////////////////
/// @brief test search
////////////////////////////////////////////////////////////////////////////////

    testSearchEmpty : function () {
      var result = fm.searchJson();

      var i, j, n;

      n = result.length;
      assertTrue(n > 0);

      // validate the results structure
      for (i = 0; i < n; ++i) {
        var entry = result[i];

        assertTrue(entry.hasOwnProperty("_key"));
        assertTrue(entry.hasOwnProperty("_rev"));
        assertTrue(entry.hasOwnProperty("description"));
        assertTrue(entry.hasOwnProperty("name"));
        assertTrue(entry.hasOwnProperty("author"));
        assertTrue(entry.hasOwnProperty("versions"));

        versions = entry.versions;

        for (version in entry.versions) {
          if (entry.versions.hasOwnProperty(version)) {
            assertMatch(/^\d+(\.\d+)*$/, version);
            assertTrue(entry.versions[version].hasOwnProperty("type"));
            assertTrue(entry.versions[version].hasOwnProperty("location"));
          }
        }
      }
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test search existings
////////////////////////////////////////////////////////////////////////////////

    testSearchAztec : function () {
      var result = fm.searchJson("itzpapalotl");

      var i, j, n;

      n = result.length;
      assertTrue(n > 0);

      // validate the results structure, simply take the first match
      var entry = result.pop();

      assertEqual("jsteemann", entry.author);
      assertEqual("itzpapalotl", entry.name);
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test install
////////////////////////////////////////////////////////////////////////////////

    testInstallSingle : function () {
      fm.update();

      try {
        fm.uninstall("/itz");
      }
      catch (err) {
      }

      fm.install("itzpapalotl", "/itz");
 
      var url = '/itz/random';
      var fetched = arango.GET(url);

      assertTrue(fetched.hasOwnProperty("name"));

      fm.uninstall("/itz");
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test install
////////////////////////////////////////////////////////////////////////////////

    testInstallMulti : function () {
      fm.update();

      try {
        fm.uninstall("/itz1");
        fm.uninstall("/itz2");
      }
      catch (err) {
      }

      fm.install("itzpapalotl", "/itz1");
      fm.install("itzpapalotl", "/itz2");
 
      var url, fetched;

      url  = '/itz1/random';
      fetched = arango.GET(url);
      assertTrue(fetched.hasOwnProperty("name"));
      
      url  = '/itz2/random';
      fetched = arango.GET(url);

      assertTrue(fetched.hasOwnProperty("name"));

      fm.uninstall("/itz1");
      fm.uninstall("/itz2");
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test uninstall
////////////////////////////////////////////////////////////////////////////////

    testUninstallInstalled : function () {
      fm.update();

      try {
        fm.uninstall("/itz");
      }
      catch (err) {
      }

      fm.install("itzpapalotl", "/itz");
      fm.uninstall("/itz");
    },

////////////////////////////////////////////////////////////////////////////////
/// @brief test uninstall
////////////////////////////////////////////////////////////////////////////////

    testUninstallUninstalled : function () {
      fm.update();

      try {
        fm.uninstall("/itz");
      }
      catch (err) {
      }

      try {
        // already uninstalled
        fm.uninstall("/itz");
        fail();
      }
      catch (err) {
      }
    }

  };
}

// -----------------------------------------------------------------------------
// --SECTION--                                                              main
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief executes the test suite
////////////////////////////////////////////////////////////////////////////////

jsunity.run(FoxxManagerSuite);

return jsunity.done();

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// @addtogroup\\|// --SECTION--\\|/// @page\\|/// @}\\)"
// End:
