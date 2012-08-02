////////////////////////////////////////////////////////////////////////////////
/// @brief base class for input-output tasks from sockets
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
/// @author Achim Brandt
/// @author Copyright 2009-2012, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_SCHEDULER_SOCKET_TASK_H
#define TRIAGENS_SCHEDULER_SOCKET_TASK_H 1

#include "Scheduler/Task.h"

#include "Basics/Mutex.h"
#include "Basics/Thread.h"
#include "Statistics/StatisticsAgent.h"

// -----------------------------------------------------------------------------
// --SECTION--                                              forward declarations
// -----------------------------------------------------------------------------

namespace triagens {
  namespace basics {
    class StringBuffer;
  }

// -----------------------------------------------------------------------------
// --SECTION--                                                  class SocketTask
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Scheduler
/// @{
////////////////////////////////////////////////////////////////////////////////

  namespace rest {

////////////////////////////////////////////////////////////////////////////////
/// @brief base class for input-output tasks from sockets
////////////////////////////////////////////////////////////////////////////////

    class SocketTask : virtual public Task,
                       public ConnectionStatisticsAgent {
      private:
        SocketTask (SocketTask const&);
        SocketTask& operator= (SocketTask const&);

      private:
        static size_t const READ_BLOCK_SIZE = 10000;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Scheduler
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief constructs a new task with a given socket
////////////////////////////////////////////////////////////////////////////////

      public:

        explicit
        SocketTask (socket_t fd);

////////////////////////////////////////////////////////////////////////////////
/// @brief deletes a socket task
///
/// This method will close the underlying socket.
////////////////////////////////////////////////////////////////////////////////

      protected:

        ~SocketTask ();

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                         protected virtual methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Scheduler
/// @{
////////////////////////////////////////////////////////////////////////////////

      protected:

////////////////////////////////////////////////////////////////////////////////
/// @brief fills the read buffer
///
/// @param closed
///     will be set to true, if the system receives a close on the socket.
///
/// The function should be called by the input task if the scheduler has
/// indicated that new data is available. It will return true, if data could
/// be read and false if the connection has been closed.
////////////////////////////////////////////////////////////////////////////////

        virtual bool fillReadBuffer (bool& closed);

////////////////////////////////////////////////////////////////////////////////
/// @brief handles a read
///
/// @param closed
///     will be set to true, if the system receives a close on the socket.
////////////////////////////////////////////////////////////////////////////////

        virtual bool handleRead (bool& closed) = 0;

////////////////////////////////////////////////////////////////////////////////
/// @brief handles a write
///
/// @param closed
///     will be set to true, if the system receives a close on the socket.
///
/// @param noWrite
///     is true if no writeBuffer exists.
////////////////////////////////////////////////////////////////////////////////

        virtual bool handleWrite (bool& closed, bool noWrite);

////////////////////////////////////////////////////////////////////////////////
/// @brief called if write buffer has been sent
///
/// This called is called if the current write buffer has been sent
/// completly to the client.
////////////////////////////////////////////////////////////////////////////////

        virtual void completedWriteBuffer (bool& closed) = 0;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                 protected methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Scheduler
/// @{
////////////////////////////////////////////////////////////////////////////////

      protected:

////////////////////////////////////////////////////////////////////////////////
/// @brief sets an active write buffer
////////////////////////////////////////////////////////////////////////////////

        void setWriteBuffer (basics::StringBuffer*, TRI_request_statistics_t*, bool ownBuffer = true);

////////////////////////////////////////////////////////////////////////////////
/// @brief appends or creates an active write buffer
////////////////////////////////////////////////////////////////////////////////

        void appendWriteBuffer (basics::StringBuffer*);

////////////////////////////////////////////////////////////////////////////////
/// @brief checks for presence of an active write buffer
////////////////////////////////////////////////////////////////////////////////

        bool hasWriteBuffer () const;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                      Task methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Scheduler
/// @{
////////////////////////////////////////////////////////////////////////////////

      protected:

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

        void setup (Scheduler*, EventLoop);

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

        void cleanup ();

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

        bool handleEvent (EventToken token, EventType);

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                               protected variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Scheduler
/// @{
////////////////////////////////////////////////////////////////////////////////

      protected:

////////////////////////////////////////////////////////////////////////////////
/// @brief event for read
////////////////////////////////////////////////////////////////////////////////

        EventToken readWatcher;

////////////////////////////////////////////////////////////////////////////////
/// @brief event for write
////////////////////////////////////////////////////////////////////////////////

        EventToken writeWatcher;

////////////////////////////////////////////////////////////////////////////////
/// @brief event for async
////////////////////////////////////////////////////////////////////////////////

        EventToken watcher;

////////////////////////////////////////////////////////////////////////////////
/// @brief communication socket
////////////////////////////////////////////////////////////////////////////////

        socket_t commSocket;

////////////////////////////////////////////////////////////////////////////////
/// @brief lock on the write buffer
////////////////////////////////////////////////////////////////////////////////

        mutable basics::Mutex writeBufferLock;

////////////////////////////////////////////////////////////////////////////////
/// @brief the current write buffer
////////////////////////////////////////////////////////////////////////////////

        basics::StringBuffer* _writeBuffer;

////////////////////////////////////////////////////////////////////////////////
/// @brief the current write buffer statistics
////////////////////////////////////////////////////////////////////////////////

#ifdef TRI_ENABLE_FIGURES

        TRI_request_statistics_t* _writeBufferStatistics;

#endif

////////////////////////////////////////////////////////////////////////////////
/// @brief if true, the resource writeBuffer is owned by the write task
///
/// If true, the writeBuffer is deleted as soon as it has been sent to the
/// client. If false, the writeBuffer is keep alive.
////////////////////////////////////////////////////////////////////////////////

        bool ownBuffer;

////////////////////////////////////////////////////////////////////////////////
/// @brief number of bytes already written
////////////////////////////////////////////////////////////////////////////////

        size_t writeLength;

////////////////////////////////////////////////////////////////////////////////
/// @brief read buffer
///
/// The function fillReadBuffer stores the data in this buffer.
////////////////////////////////////////////////////////////////////////////////

        basics::StringBuffer* _readBuffer;

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                               protected variables
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Scheduler
/// @{
////////////////////////////////////////////////////////////////////////////////

      private:

////////////////////////////////////////////////////////////////////////////////
/// @brief current thread identifier
////////////////////////////////////////////////////////////////////////////////

        TRI_tid_t tid;

////////////////////////////////////////////////////////////////////////////////
/// @brief temporary static buffer for read requests
////////////////////////////////////////////////////////////////////////////////

        char * tmpReadBuffer;
    };
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

#endif

// -----------------------------------------------------------------------------
// --SECTION--                                                       END-OF-FILE
// -----------------------------------------------------------------------------

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}\\)"
// End:
