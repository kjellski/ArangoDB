////////////////////////////////////////////////////////////////////////////////
/// @brief V8 enigne configuration
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2012 triagens GmbH, Cologne, Germany
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

#ifndef TRIAGENS_REST_SERVER_APPLICATION_V8_H
#define TRIAGENS_REST_SERVER_APPLICATION_V8_H 1

#include "ApplicationServer/ApplicationFeature.h"

namespace triagens {
  namespace arango {

// -----------------------------------------------------------------------------
// --SECTION--                                               class ApplicationV8
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief application simple user and session management feature
////////////////////////////////////////////////////////////////////////////////

    class ApplicationV8 : public rest::ApplicationFeature {
      private:
        ApplicationV8 (ApplicationV8 const&);
        ApplicationV8& operator= (ApplicationV8 const&);

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief constructor
////////////////////////////////////////////////////////////////////////////////

        ApplicationV8 (string const& binaryPath);

////////////////////////////////////////////////////////////////////////////////
/// @brief destructor
////////////////////////////////////////////////////////////////////////////////

        ~ApplicationV8 ();

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                        ApplicationFeature methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

      public:

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

        void setupOptions (map<string, basics::ProgramOptionsDescription>&);

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup ArangoDB
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief path to the directory containing alternate startup scripts
///
/// @CMDOPT{--javascript.directory @CA{directory}}
///
/// Specifies the @CA{directory} path to alternate startup JavaScript files.
/// Normally, the server will start using built-in JavaScript core
/// functionality. To override the core functionality with a different
/// implementation, this option can be used.
////////////////////////////////////////////////////////////////////////////////

        string _startupPathJS;

////////////////////////////////////////////////////////////////////////////////
/// @brief semicolon separated list of module directories
///
/// @CMDOPT{--javascript.modules-path @CA{directory}}
///
/// Specifies the @CA{directory} path with user defined JavaScript modules.
/// Multiple paths can be specified separated with commas.
////////////////////////////////////////////////////////////////////////////////

        string _startupModulesJS;

////////////////////////////////////////////////////////////////////////////////
/// @brief path to the system action directory
///
/// @CMDOPT{--javascript.action-directory @CA{directory}}
///
/// Specifies the @CA{directory} containg the system defined JavaScript files
/// that can be invoked as actions.
////////////////////////////////////////////////////////////////////////////////

        string _actionPathJS;

////////////////////////////////////////////////////////////////////////////////
/// @brief JavaScript garbage collection interval (each x requests)
///
/// @CMDOPT{--javascript.gc-interval @CA{interval}}
///
/// Specifies the interval (approximately in number of requests) that the
/// garbage collection for JavaScript objects will be run in each thread.
////////////////////////////////////////////////////////////////////////////////

        uint64_t _gcIntervalJS;
    };
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

#endif

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:
