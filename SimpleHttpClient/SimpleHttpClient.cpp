////////////////////////////////////////////////////////////////////////////////
/// @brief simple http client
///
/// @file
///
/// DISCLAIMER
///
/// Copyright by triAGENS GmbH - All rights reserved.
///
/// The Programs (which include both the software and documentation)
/// contain proprietary information of triAGENS GmbH; they are
/// provided under a license agreement containing restrictions on use and
/// disclosure and are also protected by copyright, patent and other
/// intellectual and industrial property laws. Reverse engineering,
/// disassembly or decompilation of the Programs, except to the extent
/// required to obtain interoperability with other independently created
/// software or as specified by law, is prohibited.
///
/// The Programs are not intended for use in any nuclear, aviation, mass
/// transit, medical, or other inherently dangerous applications. It shall
/// be the licensee's responsibility to take all appropriate fail-safe,
/// backup, redundancy, and other measures to ensure the safe use of such
/// applications if the Programs are used for such purposes, and triAGENS
/// GmbH disclaims liability for any damages caused by such use of
/// the Programs.
///
/// This software is the confidential and proprietary information of
/// triAGENS GmbH. You shall not disclose such confidential and
/// proprietary information and shall use it only in accordance with the
/// terms of the license agreement you entered into with triAGENS GmbH.
///
/// Copyright holder is triAGENS GmbH, Cologne, Germany
///
/// @author Dr. Frank Celler
/// @author Achim Brandt
/// @author Copyright 2008-2011, triagens GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "SimpleHttpClient.h"
#include "SimpleHttpResult.h"
#include "SimpleHttpConnection.h"

#include "Basics/StringUtils.h"
#include "Logger/Logger.h"

using namespace triagens::basics;
using namespace std;

///////////////////////////////////////////////////////////////////////////////
/// constructor and destructor
////////////////////////////////////////////////////////////////////////////////

namespace triagens {
  namespace httpclient {

    SimpleHttpClient::SimpleHttpClient (
                        const std::string& hostname, 
                        int port, 
                        double requestTimeout,
                        size_t retries,
                        double connectionTimeout) :
                        _requestTimeout(requestTimeout),
                        _retries(retries) {
      
      _connection = new SimpleHttpConnection(hostname, port, connectionTimeout);
    }

    SimpleHttpClient::~SimpleHttpClient () {
      delete _connection;
    }


    ////////////////////////////////////////////////////////////////////////////////
    /// public methods
    ////////////////////////////////////////////////////////////////////////////////

    SimpleHttpResult* SimpleHttpClient::request (
            int method, 
            const std::string& location,
            const char* body, 
            size_t bodyLength,
            const map<string, string>& headerFields) {

      SimpleHttpResult* result = new SimpleHttpResult();
      SimpleHttpResult::resultTypes type = SimpleHttpResult::UNKNOWN;

      size_t retries = 0;

      // build request
      stringstream requestBuffer;
      fillRequestBuffer(requestBuffer, method, location, body, bodyLength, headerFields);
      LOGGER_TRACE << "Request: " << requestBuffer.str();

      double start = _connection->now();
      double runtime = 0.0;

      // TODO requestTimeout
      while (retries++ <= _retries && runtime < _requestTimeout) {

        // check connection
        if (!checkConnection()) {
          type = SimpleHttpResult::COULD_NOT_CONNECT;
          // LOGGER_WARNING << "could not connect to '" << _connection->getHostname() << ":" << _connection->getPort() << "'";
        }

        // write
        else if (!write(requestBuffer, _requestTimeout - runtime)) {
          type = SimpleHttpResult::WRITE_ERROR;
          // LOGGER_WARNING << "write error";
        }

        // read
        else if (!read(result, _requestTimeout - runtime)) {
          type = SimpleHttpResult::READ_ERROR;
          // LOGGER_WARNING << "read error";
        }
        else {
          type = SimpleHttpResult::COMPLETE; 
          break;
        }
        
        closeConnection();
        runtime = _connection->now() - start;
      }

      result->setResultType(type);
      
      return result;
    }

    const string& SimpleHttpClient::getErrorMessage () {
      static const string nc = "not connected";
      
      if (!_connection) {
        return nc;
      }      
      
      return _connection->getErrorMessage();
    }

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief get the hostname 
    ////////////////////////////////////////////////////////////////////////////////

    const std::string& SimpleHttpClient::getHostname () {
      return _connection->getHostname();
    }

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief get the port
    ////////////////////////////////////////////////////////////////////////////////

    int SimpleHttpClient::getPort () {
      return _connection->getPort();
    }

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief returns true if the client is connected
    ////////////////////////////////////////////////////////////////////////////////

    bool SimpleHttpClient::isConnected () {
      if (!_connection) {
        return false;
      }
      return _connection->isConnected();
    }

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief sets username and password 
    ////////////////////////////////////////////////////////////////////////////////
      
    void SimpleHttpClient::setUserNamePassword (
            const string& prefix, 
            const string& username, 
            const string& password) {
      
      string value = triagens::basics::StringUtils::encodeBase64(username + ":" + password);
      
      _pathToBasicAuth.push_back(make_pair(prefix, value));      
    }
    
    ////////////////////////////////////////////////////////////////////////////////
    /// private methods
    ////////////////////////////////////////////////////////////////////////////////
    
    bool SimpleHttpClient::read (SimpleHttpResult* httpResult, double timeout) {
      httpResult->clear();
              
      double start = _connection->now();
      
      // read the http header
      if (!readHeader(httpResult, timeout)) {
        return false;
      }
      
      double runtime = _connection->now() - start;
      
      if (runtime > timeout) {
        LOGGER_WARNING << "read timeout";
        return false;
      }
      
      // read the body
      return readBody(httpResult, timeout - runtime);
    }

    bool SimpleHttpClient::readHeader (SimpleHttpResult* httpResult, double timeout) {
      double start = _connection->now();
      double runtime = 0.0;

      bool ok = false;
      string line;
      
      while (runtime < timeout && _connection->readLn(line, timeout)) {
        LOGGER_TRACE << "read header line: " << line;
        
        if (line == "\r" || line == "") {
          // end of header found
          ok = true;
          break;
        }        

        httpResult->addHeaderField(line);
        runtime = _connection->now() - start;
      }

      return (ok && (httpResult->getHttpReturnCode() > 0));
    }
    
    
    bool SimpleHttpClient::readBody (SimpleHttpResult* result, double timeout) {
      if (result->isChunked()) {
        return readBodyChunked(result, timeout);
      }
      else {
        return readBodyContentLength(result, timeout);
      }
    }

    bool SimpleHttpClient::readBodyContentLength (SimpleHttpResult* result, double timeout) {
      int contentLength = result->getContenLength();

      if (contentLength <= 0) {
        // nothing to read
        //_logger->TraceLogger("Result has no content.");
        return true;
      }

      int len_read = _connection->read(result->getBody(), contentLength, timeout);
      LOGGER_TRACE << "body: " << result->getBody().str();
      
      if (len_read == contentLength) {
        return true;
      }
      return false;      
    }
    
    bool SimpleHttpClient::readBodyChunked (SimpleHttpResult* result, double timeout) {
      string line;
      
      double start = _connection->now();
      double runtime = 0.0;
      
      while (runtime < timeout && _connection->readLn(line, timeout)) {
        
        if (line == "\r" || line == "") {
          // ignore empty line
          continue;
        }

        string trimed = StringUtils::trim(line);
        
        uint32_t contentLength;
        sscanf(trimed.c_str(), "%x", &contentLength);
        
        if (contentLength == 0) {
          // OK: last content length found
          LOGGER_TRACE << "body chunked: " << result->getBody().str();
          return true;
        }
        
        if (contentLength > 1000000) {
          // failed: too many bytes
          LOGGER_WARNING << "read body chunked faild: too many bytes.";
          return false;
        }
        
        runtime = _connection->now() - start;

        if (runtime > timeout) {
          // failed: timeout
          LOGGER_WARNING << "read body chunked faild: timeout.";
          return false;
        }
        
        size_t len_read = _connection->read(result->getBody(), contentLength, timeout - runtime);
        
        if (len_read < contentLength) {
          // failed: could not read all data
          LOGGER_WARNING << "read body chunked faild: not enough bytes.";
          return false;
        }
        
        runtime = _connection->now() - start;
      }  
      
      LOGGER_WARNING << "readln faild.";
      return false;
    }
        
    bool SimpleHttpClient::write (stringstream &buffer, double timeout) {
      return _connection->write(buffer.str(), timeout);
    }

    void SimpleHttpClient::fillRequestBuffer (stringstream &requestBuffer, 
            int method,
            const std::string& location, 
            const char* body, 
            size_t bodyLength,
            const map<string, string>& headerFields) {

      switch (method) {
        case GET:
          requestBuffer << "GET ";
          break;
        case POST:
          requestBuffer << "POST ";
          break;
        case PUT:
          requestBuffer << "PUT ";
          break;
        case DELETE:
          requestBuffer << "DELETE ";
          break;
        case HEAD:
          requestBuffer << "HEAD ";
          break;
        default:
          requestBuffer << "POST ";
          break;
      }

      string l = location;
      if (location.length() == 0 || location[0] != '/') {
        l = "/" + location;
      }

      requestBuffer << l << " HTTP/1.1\r\n";      
      
      requestBuffer << "Host: ";
      if (_connection->getHostname().find(':') != string::npos) {
        requestBuffer << "[" << _connection->getHostname() << "]";        
      }
      else {
        requestBuffer << _connection->getHostname();        
      }
      requestBuffer << ':' << _connection->getPort() << "\r\n";
      requestBuffer << "Connection: Keep-Alive\r\n";
      requestBuffer << "User-Agent: VOC-Client/1.0\r\n";
      
      //requestBuffer << "Accept: application/json\r\n";      
      //if (bodyLength > 0) {
      //  requestBuffer << "Content-Type: application/json; charset=utf-8\r\n";
      //}

      // do basic authorization
      if (_pathToBasicAuth.size() > 0) {        
        string foundPrefix;
        string foundValue;
        std::vector< std::pair<std::string, std::string> >::iterator i = _pathToBasicAuth.begin();
        for (; i != _pathToBasicAuth.end(); ++i) {
          string f = i->first;
          if (l.find(f) == 0) {
            // f is prefix of l
            if ( f.length() > foundPrefix.length() ) {
              foundPrefix = f;
              foundValue = i->second;
            }
          }           
        }
        if (foundValue.length() > 0) {
          requestBuffer << "Authorization: Basic " << foundValue << "\r\n";          
        }
      }
      
      for (map<string, string>::const_iterator i = headerFields.begin(); i != headerFields.end(); ++i) {
        // TODO: check Header name and value
        requestBuffer << i->first << ": " << i->second << "\r\n";
      }

      requestBuffer << "Content-Length: " << bodyLength << "\r\n\r\n";
      requestBuffer.write(body, bodyLength);
    }

    bool SimpleHttpClient::checkConnection () {      
      return _connection->connect();
    }

    void SimpleHttpClient::closeConnection () {
      _connection->close();
    }
    
  }
}
