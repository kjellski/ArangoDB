////////////////////////////////////////////////////////////////////////////////
/// @brief html result generator
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
/// @author Achim Brandt
/// @author Copyright 2008-2011, triAGENS GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#ifndef TRIAGENS_RESULT_GENERATOR_HTML_RESULT_GENERATOR_H
#define TRIAGENS_RESULT_GENERATOR_HTML_RESULT_GENERATOR_H 1

#include "ResultGenerator/ResultGenerator.h"

namespace triagens {
  namespace rest {

    ////////////////////////////////////////////////////////////////////////////////
    /// @brief html result generator
    ////////////////////////////////////////////////////////////////////////////////

    class HtmlResultGenerator : public ResultGenerator {
      public:

        ////////////////////////////////////////////////////////////////////////////////
        /// @brief set ups generating functions
        ////////////////////////////////////////////////////////////////////////////////

        static void initialise ();

      public:

        ////////////////////////////////////////////////////////////////////////////////
        /// {@inheritDoc}
        ////////////////////////////////////////////////////////////////////////////////

        ResultGeneratorType type () const {
          return RESULT_GENERATOR_HTML;
        }

        ////////////////////////////////////////////////////////////////////////////////
        /// {@inheritDoc}
        ////////////////////////////////////////////////////////////////////////////////

        string contentType () const {
          return "text/html; charset=utf-8";
        }

      public:

        ////////////////////////////////////////////////////////////////////////////////
        /// {@inheritDoc}
        ////////////////////////////////////////////////////////////////////////////////

        void generateAtom (basics::StringBuffer&, char const*) const;

        ////////////////////////////////////////////////////////////////////////////////
        /// {@inheritDoc}
        ////////////////////////////////////////////////////////////////////////////////

        void generateAtom (basics::StringBuffer&, char const*, size_t, bool quote) const;

        ////////////////////////////////////////////////////////////////////////////////
        /// {@inheritDoc}
        ////////////////////////////////////////////////////////////////////////////////

        void generateAtom (basics::StringBuffer&, bool) const;

        ////////////////////////////////////////////////////////////////////////////////
        /// {@inheritDoc}
        ////////////////////////////////////////////////////////////////////////////////

        void generateAtom (basics::StringBuffer&, double) const;

        ////////////////////////////////////////////////////////////////////////////////
        /// {@inheritDoc}
        ////////////////////////////////////////////////////////////////////////////////

        void generateAtom (basics::StringBuffer&, float) const;

        ////////////////////////////////////////////////////////////////////////////////
        /// {@inheritDoc}
        ////////////////////////////////////////////////////////////////////////////////

        void generateAtom (basics::StringBuffer&, int16_t) const;

        ////////////////////////////////////////////////////////////////////////////////
        /// {@inheritDoc}
        ////////////////////////////////////////////////////////////////////////////////

        void generateAtom (basics::StringBuffer&, int32_t) const;

        ////////////////////////////////////////////////////////////////////////////////
        /// {@inheritDoc}
        ////////////////////////////////////////////////////////////////////////////////

        void generateAtom (basics::StringBuffer&, int64_t) const;

        ////////////////////////////////////////////////////////////////////////////////
        /// {@inheritDoc}
        ////////////////////////////////////////////////////////////////////////////////

        void generateAtom (basics::StringBuffer&, string const&) const;

        ////////////////////////////////////////////////////////////////////////////////
        /// {@inheritDoc}
        ////////////////////////////////////////////////////////////////////////////////

        void generateAtom (basics::StringBuffer&, uint16_t) const;

        ////////////////////////////////////////////////////////////////////////////////
        /// {@inheritDoc}
        ////////////////////////////////////////////////////////////////////////////////

        void generateAtom (basics::StringBuffer&, uint32_t) const;

        ////////////////////////////////////////////////////////////////////////////////
        /// {@inheritDoc}
        ////////////////////////////////////////////////////////////////////////////////

        void generateAtom (basics::StringBuffer&, uint64_t) const;

      protected:

        ////////////////////////////////////////////////////////////////////////////////
        /// {@inheritDoc}
        ////////////////////////////////////////////////////////////////////////////////

        void generateResultBegin (basics::StringBuffer&, basics::VariantObject*) const;

        ////////////////////////////////////////////////////////////////////////////////
        /// {@inheritDoc}
        ////////////////////////////////////////////////////////////////////////////////

        void generateResultEnd (basics::StringBuffer&, basics::VariantObject*) const;
    };
  }
}

#endif
