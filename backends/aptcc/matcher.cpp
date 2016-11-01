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

Matcher::Matcher(const string &matchers) :
    m_hasError(false)
{
    string::const_iterator start = matchers.begin();
    parse_pattern(start, matchers.end());
    if (m_hasError) {
        cerr << "ERROR: " << m_error << endl;
    }
}

Matcher::~Matcher()
{
    for (regex_t &rx : m_matches) {
        regfree(&rx);
    }
}

bool do_compile(const string &_pattern,
                regex_t &pattern,
                int cflags)
{
    return !regcomp(&pattern, _pattern.c_str(), cflags);
}

bool string_matches(const char *s, regex_t &pattern_nogroup)
{
    return !regexec(&pattern_nogroup, s, 0, NULL, 0);
}

bool Matcher::matches(const string &s)
{
    int matchesCount = 0;
    for (regex_t &rx : m_matches) {
        if (string_matches(s.c_str(), rx)) {
            matchesCount++;
        }
    }

    return m_matches.size() == matchesCount;
}

bool Matcher::parse_pattern(string::const_iterator &start,
                            const std::string::const_iterator &end)
{
    // Just filter blank strings out immediately.
    while (start != end && isspace(*start)) {
        ++start;
    }

    if (start == end) {
        return false;
    }

    while (start != end && *start != '|' && *start != ')') {
        string subString = parse_substr(start, end);

        if (subString.empty()) {
            continue;
        }

        regex_t pattern_nogroup;
        if (do_compile(subString, pattern_nogroup, REG_ICASE|REG_EXTENDED|REG_NOSUB)) {
            m_matches.push_back(pattern_nogroup);
        } else {
            regfree(&pattern_nogroup);
            m_error = string("Regex compilation error");
            m_hasError = true;
            return false;
        }
    }

    return true;
}

string Matcher::parse_literal_string_tail(string::const_iterator &start,
                                          const string::const_iterator end)
{
    std::string rval;

    while (start != end && *start != '"') {
        if (*start == '\\') {
            ++start;
            if (start != end) {
                switch (*start) {
                case 'n':
                    rval += '\n';
                    break;
                case 't':
                    rval += '\t';
                    break;
                default:
                    rval += *start;
                    break;
                }
                ++start;
            }
        } else {
            rval += *start;
            ++start;
        }
    }

    if (start == end || *start != '"') {
        m_error = string("Unterminated literal string after " + rval);
        m_hasError = true;
        return string();
    }

    ++start;

    return rval;
}

// Returns a substring up to the first metacharacter, including escaped
// metacharacters (parentheses, ~, |, and !)
//
// Advances loc to the first character of 's' following the escaped string.
string Matcher::parse_substr(string::const_iterator &start,
                             const string::const_iterator &end)
{
    std::string rval;
    bool done=false;

    // Strip leading whitespace.
    while (start != end && isspace(*start))
        ++start;

    do {
        while (start != end &&
               *start != '(' &&
               *start != ')' &&
               *start != '!' &&
               *start != '~' &&
               *start != '|' &&
               *start != '"' &&
               !isspace(*start)) {
            rval += *start;
            ++start;
        }

        if (start != end && *start == '"') {
            ++start;

            rval += parse_literal_string_tail(start, end);
            if (m_hasError) {
                return string();
            }
        }

        // We quit because we ran off the end of the string or saw a
        // metacharacter.  If the latter case and it was a tilde-escape,
        // add the escaped character to the string and continue.
        if (start != end && start+1 != end && *start == '~') {
            const char next = *(start+1);

            if (next == '(' || next == ')' ||
                    next == '!' || next == '~' ||
                    next == '|' || next == '"' ||
                    isspace(next)) {
                rval += next;
                start += 2;
            } else {
                done = true;
            }
        } else {
            done = true;
        }
    } while(!done);

    return rval;
}

bool Matcher::hasError() const
{
    return m_hasError;
}
