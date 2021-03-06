////////////////////////////////////////////////////////////////////////////////
/// @brief abstract class for http handlers
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
/// @author Dr. Frank Celler
/// @author Copyright 2009-2014, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_HTTP_SERVER_HTTP_HANDLER_H
#define TRIAGENS_HTTP_SERVER_HTTP_HANDLER_H 1

#include "Rest/Handler.h"
#include "Rest/HttpResponse.h"

// -----------------------------------------------------------------------------
// --SECTION--                                              forward declarations
// -----------------------------------------------------------------------------

namespace triagens {
  namespace rest {
    class HttpHandlerFactory;
    class HttpRequest;
    class HttpServer;
    class HttpsServer;

// -----------------------------------------------------------------------------
// --SECTION--                                                 class HttpHandler
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief abstract class for http handlers
////////////////////////////////////////////////////////////////////////////////

    class HttpHandler : public Handler {
      private:
        HttpHandler (HttpHandler const&);
        HttpHandler& operator= (HttpHandler const&);

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief constructs a new handler
///
/// Note that the handler owns the request and the response. It is its
/// responsibility to destroy them both. See also the two steal methods.
////////////////////////////////////////////////////////////////////////////////

        explicit
        HttpHandler (HttpRequest*);

////////////////////////////////////////////////////////////////////////////////
/// @brief destructs a handler
////////////////////////////////////////////////////////////////////////////////

        ~HttpHandler ();

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief returns the response
////////////////////////////////////////////////////////////////////////////////

        HttpResponse* getResponse () const;

////////////////////////////////////////////////////////////////////////////////
/// @brief steal the response
////////////////////////////////////////////////////////////////////////////////

        HttpResponse* stealResponse ();

// -----------------------------------------------------------------------------
// --SECTION--                                                   Handler methods
// -----------------------------------------------------------------------------

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief register the server object
////////////////////////////////////////////////////////////////////////////////

        void setServer (HttpHandlerFactory* server) {
          _server = server;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief return a pointer to the request
////////////////////////////////////////////////////////////////////////////////

        HttpRequest const* getRequest () const {
          return _request;
        }

////////////////////////////////////////////////////////////////////////////////
/// @brief steal the pointer to the request
////////////////////////////////////////////////////////////////////////////////

        HttpRequest* stealRequest ();

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

        virtual Job* createJob (AsyncJobServer*,
                                bool);

////////////////////////////////////////////////////////////////////////////////
/// @brief add the response of a sub handler
////////////////////////////////////////////////////////////////////////////////

        virtual void addResponse (HttpHandler*) {
          // nothing by default
        }

// -----------------------------------------------------------------------------
// --SECTION--                                                 protected methods
// -----------------------------------------------------------------------------

      protected:

////////////////////////////////////////////////////////////////////////////////
/// @brief ensure the handler has only one response, otherwise we'd have a leak
////////////////////////////////////////////////////////////////////////////////

        void removePreviousResponse ();

////////////////////////////////////////////////////////////////////////////////
/// @brief create a new HTTP response
////////////////////////////////////////////////////////////////////////////////

        HttpResponse* createResponse (HttpResponse::HttpResponseCode);

// -----------------------------------------------------------------------------
// --SECTION--                                               protected variables
// -----------------------------------------------------------------------------

      protected:

////////////////////////////////////////////////////////////////////////////////
/// @brief the request
////////////////////////////////////////////////////////////////////////////////

        HttpRequest* _request;

////////////////////////////////////////////////////////////////////////////////
/// @brief the response
////////////////////////////////////////////////////////////////////////////////

        HttpResponse* _response;

////////////////////////////////////////////////////////////////////////////////
/// @brief the server
////////////////////////////////////////////////////////////////////////////////

        HttpHandlerFactory* _server;

    };
  }
}

#endif

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End:
