////////////////////////////////////////////////////////////////////////////////
/// @brief task for ssl communication
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
/// @author Dr. Frank Celler
/// @author Achim Brandt
/// @author Copyright 2010-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_GENERAL_SERVER_SSL_ASYNC_COMM_TASK_H
#define TRIAGENS_GENERAL_SERVER_SSL_ASYNC_COMM_TASK_H 1

#include "GeneralServer/GeneralAsyncCommTask.h"

#include <openssl/ssl.h>
#include <openssl/err.h>

#include "Basics/ssl-helper.h"

#include "Basics/MutexLocker.h"
#include "Basics/StringBuffer.h"
#include "BasicsC/logging.h"


namespace triagens {
  namespace rest {

////////////////////////////////////////////////////////////////////////////////
/// @brief task for ssl communication
////////////////////////////////////////////////////////////////////////////////

    template<typename S, typename HF, typename CT>
    class SslAsyncCommTask : public GeneralAsyncCommTask<S, HF, CT> {
      private:
        static size_t const READ_BLOCK_SIZE = 10000;

      public:

////////////////////////////////////////////////////////////////////////////////
/// @brief constructs a new task with a given socket
////////////////////////////////////////////////////////////////////////////////

        SslAsyncCommTask (S* server,
                          TRI_socket_t socket,
                          ConnectionInfo const& info,
                          double keepAliveTimeout,
                          BIO* bio)
        : Task("SslAsyncCommTask"),
          GeneralAsyncCommTask<S, HF, CT>(server, socket, info, keepAliveTimeout),
          accepted(false),
          readBlocked(false),
          readBlockedOnWrite(false),
          writeBlockedOnRead(false),
          ssl((SSL*) info.sslContext),
          bio(bio) {
          tmpReadBuffer = new char[READ_BLOCK_SIZE];
        }

      protected:

////////////////////////////////////////////////////////////////////////////////
/// @brief destructs a task
////////////////////////////////////////////////////////////////////////////////

        ~SslAsyncCommTask () {
          static int const SHUTDOWN_ITERATIONS = 10;
          bool ok = false;

          for (int i = 0;  i < SHUTDOWN_ITERATIONS;  ++i) {
            if (SSL_shutdown(ssl) != 0) {
              ok = true;
              break;
            }
          }

          if (! ok) {
            LOG_WARNING("cannot complete SSL shutdown");
          }

          SSL_free(ssl); // this will free bio as well
          delete[] tmpReadBuffer;
        }

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

        bool handleEvent (EventToken token, EventType revents) {
          bool result = GeneralAsyncCommTask<S, HF, CT>::handleEvent(token, revents);

          if (result) {
            if (readBlockedOnWrite) {
              this->scheduler->startSocketEvents(this->writeWatcher);
            }
          }

          return result;
        }

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

        bool fillReadBuffer (bool& closed) {
          closed = false;

          // is the handshake already done?
          if (! accepted) {
            if (! trySSLAccept()) {
              LOG_DEBUG("failed to establish SSL connection");
              return false;
            }

            return true;
          }

          // check if read is blocked by write
          if (writeBlockedOnRead) {
            return trySSLWrite(closed, ! this->hasWriteBuffer());
          }

          return trySSLRead(closed);
        }

////////////////////////////////////////////////////////////////////////////////
/// {@inheritDoc}
////////////////////////////////////////////////////////////////////////////////

        bool handleWrite (bool& closed, bool noWrite) {

          // is the handshake already done?
          if (! accepted) {
            if (! trySSLAccept()) {
              LOG_DEBUG("failed to establish SSL connection");
              return false;
            }

            return true;
          }

          // check if write is blocked by read
          if (readBlockedOnWrite) {
            if (! trySSLRead(closed)) {
              return false;
            }

            return this->handleRead(closed);
          }

          return trySSLWrite(closed, noWrite);
        }

      private:

        bool trySSLAccept () {
          int res = SSL_accept(ssl);

          // accept successful
          if (res == 1) {
            LOG_DEBUG("established SSL connection");
            accepted = true;
            return true;
          }

          // shutdown of connection
          else if (res == 0) {
            LOG_DEBUG("SSL_accept failed: %s", triagens::basics::lastSSLError().c_str());
            return false;
          }

          // maybe we need more data
          else {
            int err = SSL_get_error(ssl, res);

            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
              return true;
            }
            else {
              LOG_TRACE("error in SSL handshake: %s", triagens::basics::lastSSLError().c_str());
              return false;
            }
          }
        }

        bool trySSLRead (bool& closed) {
          closed = false;
          readBlocked = false;
          readBlockedOnWrite = false;

again:
          int nr = SSL_read(ssl, tmpReadBuffer, READ_BLOCK_SIZE);

          if (nr <= 0) {
            int res = SSL_get_error(ssl, nr);

            switch (res) {
              case SSL_ERROR_NONE:
                LOG_WARNING("unknown error in SSL_read");
                return false;

              case SSL_ERROR_ZERO_RETURN:
                closed = true;
                SSL_shutdown(ssl);
                return false;

              case SSL_ERROR_WANT_READ:
                readBlocked = true;
                break;

              case SSL_ERROR_WANT_WRITE:
                readBlockedOnWrite = true;
                break;

              case SSL_ERROR_WANT_CONNECT:
                LOG_WARNING("received SSL_ERROR_WANT_CONNECT");
                break;

              case SSL_ERROR_WANT_ACCEPT:
                LOG_WARNING("received SSL_ERROR_WANT_ACCEPT");
                break;

              case SSL_ERROR_SYSCALL:
                {
                  unsigned long err = ERR_peek_error();

                  if (err != 0) {
                    LOG_DEBUG("SSL_read returned syscall error with: %s", triagens::basics::lastSSLError().c_str());
                  }
                  else if (nr == 0) {
                    LOG_DEBUG("SSL_read returned syscall error because an EOF was received");
                  }
                  else {
                    LOG_DEBUG("SSL_read return syscall error: %d: %s", (int) errno, strerror(errno));
                  }

                  return false;
                }

              default:
                LOG_DEBUG("received error with %d and %d: %s", res, nr, triagens::basics::lastSSLError().c_str());
                return false;
            }
          }
          else {
            this->_readBuffer->appendText(tmpReadBuffer, nr);

            // we might have more data to read
            // if we do not iterate again, the reading process would stop
            goto again;
          }

          return true;
        }

        bool trySSLWrite (bool& closed, bool noWrite) {
          closed = false;

          // if no write buffer is left, return
          if (noWrite) {
            return true;
          }

          bool callCompletedWriteBuffer = false;

          {
            MUTEX_LOCKER(this->writeBufferLock);

            // size_t is unsigned, should never get < 0
            assert(this->_writeBuffer->length() >= this->writeLength);

            // write buffer to SSL connection
            size_t len = this->_writeBuffer->length() - this->writeLength;
            int nr = 0;

            if (0 < len) {
              writeBlockedOnRead = false;
              nr = SSL_write(ssl, this->_writeBuffer->begin() + this->writeLength, (int) len);

              if (nr <= 0) {
                int res = SSL_get_error(ssl, nr);

                switch (res) {
                  case SSL_ERROR_NONE:
                    LOG_WARNING("unknown error in SSL_write");
                    break;

                  case SSL_ERROR_ZERO_RETURN:
                    closed = true;
                    SSL_shutdown(ssl);
                    return false;
                    break;

                  case SSL_ERROR_WANT_CONNECT:
                    LOG_WARNING("received SSL_ERROR_WANT_CONNECT");
                    break;

                  case SSL_ERROR_WANT_ACCEPT:
                    LOG_WARNING("received SSL_ERROR_WANT_ACCEPT");
                    break;

                  case SSL_ERROR_WANT_WRITE:
                    return false;

                  case SSL_ERROR_WANT_READ:
                    writeBlockedOnRead = true;
                    return true;

                  case SSL_ERROR_SYSCALL:
                    {
                      unsigned long err = ERR_peek_error();

                      if (err != 0) {
                        LOG_DEBUG("SSL_read returned syscall error with: %s", triagens::basics::lastSSLError().c_str());
                      }
                      else if (nr == 0) {
                        LOG_DEBUG("SSL_read returned syscall error because an EOF was received");
                      }
                      else {
                        LOG_DEBUG("SSL_read return syscall error: %d: %s", errno, strerror(errno));
                      }

                      return false;
                    }

                  default:
                    LOG_DEBUG("received error with %d and %d: %s", res, nr, triagens::basics::lastSSLError().c_str());
                    return false;
                }
              }
              else {
                len -= nr;
              }
            }

            if (len == 0) {
              if (this->ownBuffer) {
                delete this->_writeBuffer;
              }

              callCompletedWriteBuffer = true;
            }
            else {
              this->writeLength += nr;
            }
          }

          // we have to release the lock, before calling completedWriteBuffer
          if (callCompletedWriteBuffer) {
            this->completedWriteBuffer(closed);

            if (closed) {
              return false;
            }
          }

          // we might have a new write buffer
          this->scheduler->sendAsync(this->SocketTask::watcher);

          return true;
        }

      private:
        bool accepted;
        bool readBlocked;
        bool readBlockedOnWrite;
        bool writeBlockedOnRead;

        char * tmpReadBuffer;

        SSL* ssl;
        BIO* bio;
    };
  }
}

#endif
