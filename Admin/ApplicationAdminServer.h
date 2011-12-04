////////////////////////////////////////////////////////////////////////////////
/// @brief application simple user and session management feature
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
/// @author Copyright 2011, triagens GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_ADMIN_APPLICATION_ADMIN_SERVER_H
#define TRIAGENS_ADMIN_APPLICATION_ADMIN_SERVER_H 1

#include <Rest/ApplicationFeature.h>

#include <Admin/Right.h>
#include <Rest/AddressPort.h>

namespace triagens {
  namespace rest {
    class ApplicationServer;
    class HttpHandlerFactory;
    class HttpResponse;
    class HttpRequest;
  }

  namespace admin {

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief application simple user and session management feature
    ////////////////////////////////////////////////////////////////////////////////

    class ApplicationAdminServer : public rest::ApplicationFeature {
      private:
        ApplicationAdminServer (ApplicationAdminServer const&);
        ApplicationAdminServer& operator= (ApplicationAdminServer const&);

      public:

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief creates a new feature
        ////////////////////////////////////////////////////////////////////////////////

        static ApplicationAdminServer* create (rest::ApplicationServer*);

      public:

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief destructor
        ////////////////////////////////////////////////////////////////////////////////

        ~ApplicationAdminServer ();

      public:

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief adds the http handlers
        ///
        /// Note that the server does not claim ownership of the factory.
        ////////////////////////////////////////////////////////////////////////////////

        void addBasicHandlers (rest::HttpHandlerFactory*);

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief adds the http handlers for administration
        ///
        /// Note that the server does not claim ownership of the factory.
        ////////////////////////////////////////////////////////////////////////////////

        void addHandlers (rest::HttpHandlerFactory*, string const& prefix);

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief sets the right of an anonymous session
        ////////////////////////////////////////////////////////////////////////////////

        void setAnonymousRights (vector<right_t> const&);

      public:

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief allows for session and user management
        ////////////////////////////////////////////////////////////////////////////////

        void allowSessionManagement () {
          _allowSessionManagement = true;
        }

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief add a log viewer
        ////////////////////////////////////////////////////////////////////////////////

        void allowLogViewer () {
          _allowLogViewer = true;
        }

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief allows for a webadmin directory
        ////////////////////////////////////////////////////////////////////////////////

        void allowAdminDirectory () {
          _allowAdminDirectory = true;
        }

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief allows for a front-end configuration
        ////////////////////////////////////////////////////////////////////////////////

        void allowFeConfiguration () {
          _allowFeConfiguration = true;
        }

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief allows for a version handler using the default version
        ////////////////////////////////////////////////////////////////////////////////

        void allowVersion ();

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief allows for a version handler
        ////////////////////////////////////////////////////////////////////////////////

        void allowVersion (string name, string version);


      public:

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief creates a role
        ////////////////////////////////////////////////////////////////////////////////

        bool createRole (string const& name, vector<right_t> const&, right_t rightToManage);

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief creates a user
        ////////////////////////////////////////////////////////////////////////////////

        bool createUser (string const& name, string const& role);

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief loads the user database
        ////////////////////////////////////////////////////////////////////////////////

        bool loadUser ();


      public:

        ////////////////////////////////////////////////////////////////////////////////
        /// {@inheritDoc}
        ////////////////////////////////////////////////////////////////////////////////

        void setupOptions (map<string, basics::ProgramOptionsDescription>&);

      protected:

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief constructor
        ////////////////////////////////////////////////////////////////////////////////

        ApplicationAdminServer ();

      private:
        static string optionAdminDirectory;
        static string optionUserDatabase;

      private:
        bool _allowSessionManagement;
        bool _allowLogViewer;
        bool _allowAdminDirectory;
        bool _allowFeConfiguration;
        bool _allowVersion;

        string _adminDirectory;
        void* _pathOptions;

        string _feConfiguration;
        string _name;
        string _version;
    };
  }
}

#endif
