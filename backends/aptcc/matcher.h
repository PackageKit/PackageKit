/* matcher.h
 *
 * Copyright (c) 1999-2008 Daniel Burrows
 * Copyright (c) 2009-2016 Daniel Nicoletti <dantti12@gmail.com>
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
    Matcher(const string &matcher);
    ~Matcher();

    bool matches(const string &s);
    bool hasError() const;

private:
    bool m_hasError;
    string m_error;
    bool do_compile(const string &_pattern, int cflags);
    bool parse_pattern(const string &matcher);
    regex_t m_matcher;
};

#endif
