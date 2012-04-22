/* gstMatcher.h - Match GStreamer package names
 *
 * Copyright (c) 2010 Daniel Nicoletti <dantti12@gmail.com>
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

#ifndef GST_MATCHER_H
#define GST_MATCHER_H

#include <glib.h>

#include <vector>
#include <string>

using namespace std;

typedef struct {
    string   version;
    string   type;
    string   data;
    string   opt;
    void    *caps;
} Match;

class GstMatcher
{
public:
    GstMatcher(gchar **values);
    ~GstMatcher();

    bool matches(string record);
    bool hasMatches() const;

private:
    vector<Match> m_matches;
};

#endif
