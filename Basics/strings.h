////////////////////////////////////////////////////////////////////////////////
/// @brief basic string functions
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

#ifndef TRIAGENS_BASICS_STRINGS_H
#define TRIAGENS_BASICS_STRINGS_H 1

#include <Basics/Common.h>

#include <Basics/vector.h>

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Strings
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief tests if ASCII strings are equal
////////////////////////////////////////////////////////////////////////////////

bool TRI_EqualString (char const* left, char const* right);

////////////////////////////////////////////////////////////////////////////////
/// @brief tests if ASCII strings are equal
////////////////////////////////////////////////////////////////////////////////

bool TRI_EqualString2 (char const* left, char const* right, size_t n);

////////////////////////////////////////////////////////////////////////////////
/// @brief tests if ASCII strings are equal ignoring case
////////////////////////////////////////////////////////////////////////////////

bool TRI_CaseEqualString (char const* left, char const* right);

////////////////////////////////////////////////////////////////////////////////
/// @brief duplicates a string
////////////////////////////////////////////////////////////////////////////////

char* TRI_DuplicateString (char const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief duplicates a string of given length
////////////////////////////////////////////////////////////////////////////////

char* TRI_DuplicateString2 (char const*, size_t length);

////////////////////////////////////////////////////////////////////////////////
/// @brief appends text to a string
////////////////////////////////////////////////////////////////////////////////

void TRI_AppendString (char**, char const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief copies a string
///
/// Copy string of maximal length. Always append a '\0'.
////////////////////////////////////////////////////////////////////////////////

void TRI_CopyString (char* dst, char const* src, size_t length);

////////////////////////////////////////////////////////////////////////////////
/// @brief concatenate two strings
////////////////////////////////////////////////////////////////////////////////

char* TRI_Concatenate2String (char const*, char const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief concatenate three strings
////////////////////////////////////////////////////////////////////////////////

char* TRI_Concatenate3String (char const*, char const*, char const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief concatenate four strings
////////////////////////////////////////////////////////////////////////////////

char* TRI_Concatenate4String (char const*, char const*, char const*, char const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief concatenate five strings
////////////////////////////////////////////////////////////////////////////////

char* TRI_Concatenate5String (char const*, char const*, char const*, char const*, char const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief concatenate six strings
////////////////////////////////////////////////////////////////////////////////

char* TRI_Concatenate6String (char const*, char const*, char const*, char const*, char const*, char const*);

////////////////////////////////////////////////////////////////////////////////
/// @brief splits a string
////////////////////////////////////////////////////////////////////////////////

TRI_vector_string_t TRI_SplitString (char const* source, char delim);

////////////////////////////////////////////////////////////////////////////////
/// @brief frees a string
////////////////////////////////////////////////////////////////////////////////

void TRI_FreeString (char*);

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// --SECTION--                                                     STRING ESCAPE
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// --SECTION--                                                  public functions
// -----------------------------------------------------------------------------

////////////////////////////////////////////////////////////////////////////////
/// @addtogroup Strings
/// @{
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// @brief converts a single hex to an integer
////////////////////////////////////////////////////////////////////////////////

int TRI_IntHex (char ch, int errorValue);

////////////////////////////////////////////////////////////////////////////////
/// @brief escapes special characters using C escapes
///
/// This method escapes a character string by replacing the unprintable
/// characters by a C sequences.
////////////////////////////////////////////////////////////////////////////////

char* TRI_EscapeCString (char const* in, size_t inLength, size_t* outLength);

////////////////////////////////////////////////////////////////////////////////
/// @brief escapes special characters using unicode escapes
///
/// This method escapes an UTF-8 character string by replacing the unprintable
/// characters by a \uXXXX sequence. Set escapeSlash to true in order to also
/// escape the character '/'.
////////////////////////////////////////////////////////////////////////////////

char* TRI_EscapeUtf8String (char const* in, size_t inLength, bool escapeSlash, size_t* outLength);

////////////////////////////////////////////////////////////////////////////////
/// @brief unescapes unicode escape sequences
///
/// This method decodes a UTF-8 character string by replacing the \uXXXX
/// sequence by unicode characters and representing them as UTF-8 sequences.
////////////////////////////////////////////////////////////////////////////////

char* TRI_UnescapeUtf8String (char const* in, size_t inLength, size_t* outLength);

////////////////////////////////////////////////////////////////////////////////
/// @}
////////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

#endif

// Local Variables:
// mode: outline-minor
// outline-regexp: "^\\(/// @brief\\|/// {@inheritDoc}\\|/// @addtogroup\\|// --SECTION--\\|/// @\\}\\)"
// End:
