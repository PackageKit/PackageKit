/* gst-matcher.h - Match GStreamer package names
 *
 * Copyright (c) 2010 Daniel Nicoletti <dantti12@gmail.com>
 * Copyright (c) 2026 Matthias Klumpp <mak@debian.org>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the license, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef GST_MATCHER_H
#define GST_MATCHER_H

#include <glib.h>

#include <vector>
#include <string>

typedef struct {
    std::string version;
    std::string type;
    std::string data;
    std::string opt;
    void *caps;
    bool native;
} Match;

class GstMatcher
{
public:
    GstMatcher(gchar **values);
    ~GstMatcher();

    bool matches(std::string record, bool arch);
    bool hasMatches() const;

private:
    std::vector<Match> m_matches;
};

#endif
