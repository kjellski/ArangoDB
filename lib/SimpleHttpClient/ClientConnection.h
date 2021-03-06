////////////////////////////////////////////////////////////////////////////////
/// @brief client connection
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2004-2013 triAGENS GmbH, Cologne, Germany
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
/// @author Copyright 2009-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_SIMPLE_HTTP_CLIENT_CLIENT_CONNECTION_H
#define TRIAGENS_SIMPLE_HTTP_CLIENT_CLIENT_CONNECTION_H 1

#include "BasicsC/socket-utils.h"
#include "SimpleHttpClient/GeneralClientConnection.h"

// -----------------------------------------------------------------------------
// --SECTION--                                                  ClientConnection
// -----------------------------------------------------------------------------

namespace triagens {
  namespace httpclient {

////////////////////////////////////////////////////////////////////////////////
/// @brief client connection
////////////////////////////////////////////////////////////////////////////////

    class ClientConnection : public GeneralClientConnection {

// -----------------------------------------------------------------------------
// --SECTION--                                        constructors / destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup httpclient
/// @{
////////////////////////////////////////////////////////////////////////////////

      private:

        ClientConnection (ClientConnection const&);
        ClientConnection& operator= (ClientConnection const&);

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief creates a new client connection
////////////////////////////////////////////////////////////////////////////////

        ClientConnection (triagens::rest::Endpoint* endpoint,
                          double,
                          double,
                          size_t);

////////////////////////////////////////////////////////////////////////////////
/// @brief destroys a client connection
////////////////////////////////////////////////////////////////////////////////

        virtual ~ClientConnection ();

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                   private methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup httpclient
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief check whether the socket is still alive
////////////////////////////////////////////////////////////////////////////////

        bool checkSocket ();

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                         protected virtual methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup httpclient
/// @{
////////////////////////////////////////////////////////////////////////////////

      protected:

////////////////////////////////////////////////////////////////////////////////
/// @brief connect
////////////////////////////////////////////////////////////////////////////////

        bool connectSocket ();

////////////////////////////////////////////////////////////////////////////////
/// @brief disconnect
////////////////////////////////////////////////////////////////////////////////

        void disconnectSocket ();

////////////////////////////////////////////////////////////////////////////////
/// @brief prepare connection for read/write I/O
////////////////////////////////////////////////////////////////////////////////

        bool prepare (const double, const bool) const;

////////////////////////////////////////////////////////////////////////////////
/// @brief write data to the connection
////////////////////////////////////////////////////////////////////////////////

        bool writeClientConnection (void*, size_t, size_t*);

////////////////////////////////////////////////////////////////////////////////
/// @brief read data from the connection
////////////////////////////////////////////////////////////////////////////////

        bool readClientConnection (triagens::basics::StringBuffer&);

////////////////////////////////////////////////////////////////////////////////
/// @brief return whether the connection is readable
////////////////////////////////////////////////////////////////////////////////

        bool readable ();

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                 private variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup httpclient
/// @{
////////////////////////////////////////////////////////////////////////////////

      private:

////////////////////////////////////////////////////////////////////////////////
/// @brief the underlying socket
////////////////////////////////////////////////////////////////////////////////

        TRI_socket_t _socket;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

    };
  }
}

#endif
