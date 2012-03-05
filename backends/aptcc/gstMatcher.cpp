/* gstMatcher.cpp - Match GStreamer packages
 *
 * Copyright (c) 2010 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
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

#include "gstMatcher.h"

#include <regex.h>
#include <gst/gst.h>
#include <iostream>

GstMatcher::GstMatcher(gchar **values)
{
    gst_init(NULL, NULL);

    // The search term from PackageKit daemon:
    // gstreamer0.10(urisource-foobar)
    // gstreamer0.10(decoder-audio/x-wma)(wmaversion=3)
    const char *pkreg = "^gstreamer\\([0-9\\.]\\+\\)"
                "(\\(encoder\\|decoder\\|urisource\\|urisink\\|element\\)-\\([^)]\\+\\))"
                "\\(([^\\(^\\)]*)\\)\\?";

    regex_t pkre;
    if(regcomp(&pkre, pkreg, 0) != 0) {
        g_debug("Regex compilation error: ", pkreg);
        return;
    }

    gchar *value;
    for (uint i = 0; i < g_strv_length(values); i++) {
        value = values[i];
        regmatch_t matches[5];
        if (regexec(&pkre, value, 5, matches, 0) != REG_NOMATCH) {
            Match values;
            string version, type, data, opt;

            // Appends the version "0.10"
            version = "\nGstreamer-Version: ";
            version.append(string(value, matches[1].rm_so, matches[1].rm_eo - matches[1].rm_so));

            // type (encode|decoder...)
            type = string(value, matches[2].rm_so, matches[2].rm_eo - matches[2].rm_so);

            // data "audio/x-wma"
            data = string(value, matches[3].rm_so, matches[3].rm_eo - matches[3].rm_so);

            // opt "wmaversion=3"
            if (matches[4].rm_so != -1) {
                // remove the '(' ')' that the regex matched
                opt = string(value, matches[4].rm_so + 1, matches[4].rm_eo - matches[4].rm_so - 2);
            }

            if (type.compare("encoder") == 0) {
                type = "Gstreamer-Encoders: ";
            } else if (type.compare("decoder") == 0) {
                type = "Gstreamer-Decoders: ";
            } else if (type.compare("urisource") == 0) {
                type = "Gstreamer-Uri-Sources: ";
            } else if (type.compare("urisink") == 0) {
                type = "Gstreamer-Uri-Sinks: ";
            } else if (type.compare("element") == 0) {
                type = "Gstreamer-Elements: ";
            }
//             cout << version << endl;
//             cout << type << endl;
//             cout << data << endl;
//             cout << opt << endl;

            gchar *capsString;
            if (opt.empty()) {
                capsString = g_strdup_printf("%s", data.c_str());
            } else {
                capsString = g_strdup_printf("%s, %s", data.c_str(), opt.c_str());
            }
            GstCaps *caps = gst_caps_from_string(capsString);
            g_free(capsString);

            if (caps == NULL) {
                continue;
            }

            values.version = version;
            values.type    = type;
            values.data    = data;
            values.opt     = opt;
            values.caps    = caps;

            m_matches.push_back(values);
        } else {
            g_debug("gstmatcher: Did not match: %s", value);
        }
    }
    regfree(&pkre);
}

GstMatcher::~GstMatcher()
{
    gst_deinit();

    for (vector<Match>::iterator i = m_matches.begin(); i != m_matches.end(); ++i) {
        gst_caps_unref(static_cast<GstCaps*>(i->caps));
    }
}

bool GstMatcher::matches(string record)
{
    for (vector<Match>::iterator i = m_matches.begin(); i != m_matches.end(); ++i) {
            // Tries to find "Gstreamer-version: xxx"
            if (record.find(i->version) != string::npos) {
                size_t found;
                found = record.find(i->type);
                // Tries to find the type "Gstreamer-Uri-Sinks: "
                if (found != string::npos) {
                    found += i->type.size(); // skips the "Gstreamer-Uri-Sinks: " string
                    size_t endOfLine;
                    endOfLine = record.find('\n', found);

                    GstCaps *caps;
                    caps = gst_caps_from_string(record.substr(found, endOfLine - found).c_str());
                    if (caps == NULL) {
                        continue;
                    }

                    // if the record is capable of intersect them we found the package
                    bool provides = gst_caps_can_intersect(static_cast<GstCaps*>(i->caps), caps);
                    gst_caps_unref(caps);

                    if (provides) {
                        return true;
                    }
                }
            }
        }
        return false;
}

bool GstMatcher::hasMatches() const
{
    return !m_matches.empty();
}

