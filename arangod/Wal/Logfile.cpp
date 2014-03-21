////////////////////////////////////////////////////////////////////////////////
/// @brief Write-ahead log logfile
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
/// @author Copyright 2011-2013, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "Logfile.h"

using namespace triagens::wal;

// -----------------------------------------------------------------------------
// --SECTION--                                      constructors and destructors
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief create the logfile
////////////////////////////////////////////////////////////////////////////////

Logfile::Logfile (TRI_datafile_t* df,
                  SealStatusType sealStatus,
                  CollectionStatusType collectionStatus) 
  : _df(df),
    _sealStatus(sealStatus),
    _collectionStatus(collectionStatus) {
}

////////////////////////////////////////////////////////////////////////////////
/// @brief destroy the logfile
////////////////////////////////////////////////////////////////////////////////

Logfile::~Logfile () {
  this->close();
}

// -----------------------------------------------------------------------------
// --SECTION--                                                    public methods
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @brief create a new logfile
////////////////////////////////////////////////////////////////////////////////

Logfile* Logfile::create (std::string const& filename,
                          Logfile::IdType id,
                          uint32_t size) {
  TRI_datafile_t* df = TRI_CreateDatafile(filename.c_str(), id, static_cast<TRI_voc_size_t>(size));

  if (df == nullptr) {
    int res = TRI_errno();

    if (res != TRI_ERROR_NO_ERROR) {
      LOG_ERROR("unable to create logfile '%s': %s", 
                filename.c_str(), 
                TRI_errno_string(res));
      return nullptr;
    }
  }

  Logfile* logfile = new Logfile(df, SealStatusType::UNKNOWN, CollectionStatusType::UNCOLLECTED);
  return logfile;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief open an existing logfile
////////////////////////////////////////////////////////////////////////////////

Logfile* Logfile::open (std::string const& filename) {
  TRI_datafile_t* df = TRI_OpenDatafile(filename.c_str());

  if (df == nullptr) {
    int res = TRI_errno();

    if (res != TRI_ERROR_NO_ERROR) {
      LOG_ERROR("unable to open logfile '%s': %s", 
                filename.c_str(), 
                TRI_errno_string(res));
      return nullptr;
    }
  }

  Logfile* logfile = new Logfile(df, SealStatusType::UNKNOWN, CollectionStatusType::UNCOLLECTED);
  return logfile;
}

////////////////////////////////////////////////////////////////////////////////
/// @brief close a logfile
////////////////////////////////////////////////////////////////////////////////

void Logfile::close () {
  if (_df != nullptr) {
    TRI_CloseDatafile(_df);
    _df = nullptr;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// @brief seal a logfile
////////////////////////////////////////////////////////////////////////////////

void Logfile::seal () {
  LOG_INFO("sealing logfile %llu", 
           (unsigned long long) id());
  assert(_sealStatus == SealStatusType::UNSEALED);
  _sealStatus = SealStatusType::REQUESTED;
}

// Local Variables:
// mode: outline-minor
// outline-regexp: "/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|/// @page\\|// --SECTION--\\|/// @\\}"
// End: