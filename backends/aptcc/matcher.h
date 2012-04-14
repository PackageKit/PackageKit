/* matcher.h
 *
 * Copyright (c) 1999-2008 Daniel Burrows
 * Copyright (c) 2009 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef MATCHER_H
#define MATCHER_H

#include <regex.h>

#include <vector>
#include <map>
#include <string>

using namespace std;

class Matcher
{
public:
    Matcher(const string &matchers);
    ~Matcher();

    bool matches(const string &s);
    bool matchesFile(const string &s, map<int, bool> &matchers_used);
    bool hasError() const;

private:
    bool m_hasError;
    string m_error;
    bool parse_pattern(string::const_iterator &start,
                       const std::string::const_iterator &end);
    string parse_substr(string::const_iterator &start,
                        const string::const_iterator &end);
    string parse_literal_string_tail(string::const_iterator &start,
                                     const string::const_iterator end);
    vector<regex_t> m_matches;
};

#endif
