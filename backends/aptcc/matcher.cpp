/* matcher.cpp
 *
 * Copyright (c) 1999-2008 Daniel Burrows
 * Copyright (c) 2009-2016 Daniel Nicoletti <dantti12@gmail.com>
 *               2012 Matthias Klumpp <matthias@tenstral.net>
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

#include "matcher.h"
#include <stdio.h>
#include <iostream>

Matcher::Matcher(const string &matcher) :
    m_hasError(false)
{
    parse_pattern(matcher);
    if (m_hasError) {
        cerr << "ERROR: " << m_error << endl;
    }
}

Matcher::~Matcher()
{
    if (!m_hasError) {
        regfree(&m_matcher);
    }
}

bool Matcher::do_compile(const string &_pattern,
                         int cflags)
{
    return !regcomp(&m_matcher, _pattern.c_str(), cflags);
}

bool Matcher::matches(const string &s)
{
    return !regexec(&m_matcher, s.c_str(), 0, NULL, 0);
}

bool Matcher::parse_pattern(const string &matcher)
{
    if (!do_compile (matcher, REG_ICASE|REG_EXTENDED|REG_NOSUB)) {
        regfree(&m_matcher);
        m_error = string("Regex compilation error");
        m_hasError = true;
        return false;
    }

    return true;
}

bool Matcher::hasError() const
{
    return m_hasError;
}
