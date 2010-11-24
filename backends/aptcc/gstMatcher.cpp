// apt.cc
//
//  Copyright 1999-2008 Daniel Burrows
//  Copyright (C) 2009 Daniel Nicoletti <dantti85-pk@yahoo.com.br>
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation; either version 2 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; see the file COPYING.  If not, write to
//  the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
//  Boston, MA 02111-1307, USA.

#include "gstMatcher.h"
#include <stdio.h>
#include <iostream>

#include <gst/gst.h>

gstMatcher::gstMatcher(gchar **values)
{
    // The search term from PackageKit daemon:
    // gstreamer0.10(urisource-foobar)
    // gstreamer0.10(decoder-audio/x-wma)(wmaversion=3)

    // The optional field is more complicated, it can have
    // types, like int, float...
    // TODO use some GST helper :/
    regex_t pkre;

    const char *pkreg = "^gstreamer\\([0-9\\.]\\+\\)"
                "(\\(encoder\\|decoder\\|urisource\\|urisink\\|element\\)-\\([^)]\\+\\))"
                "\\((.*)\\)\\?";

    if(regcomp(&pkre, pkreg, 0) != 0) {
        g_debug("Regex compilation error: ", pkreg);
        return;
    }

    gchar *value;
    vector<pair<string, regex_t> > search;
    for (uint i = 0; i < g_strv_length(values); i++) {
        value = values[i];
        regmatch_t matches[5];
        if (regexec(&pkre, value, 5, matches, 0) == 0) {
            string version, type, data, opt;
            version = "\nGstreamer-Version: ";
            version.append(string(value, matches[1].rm_so, matches[1].rm_eo - matches[1].rm_so));
            type = string(value, matches[2].rm_so, matches[2].rm_eo - matches[2].rm_so);
            data = string(value, matches[3].rm_so, matches[3].rm_eo - matches[3].rm_so);
            if (matches[4].rm_so != -1) {
                // remove the '(' ')' that the regex matched
                opt = string(value, matches[4].rm_so + 1, matches[4].rm_eo - matches[4].rm_so - 2);
            } else {
                // if the 4th element did not match match everything
                opt = ".*";
            }

            if (type.compare("encoder") == 0) {
                type = "Gstreamer-Encoders";
            } else if (type.compare("decoder") == 0) {
                type = "Gstreamer-Decoders";
            } else if (type.compare("urisource") == 0) {
                type = "Gstreamer-Uri-Sources";
            } else if (type.compare("urisink") == 0) {
                type = "Gstreamer-Uri-Sinks";
            } else if (type.compare("element") == 0) {
                type = "Gstreamer-Elements";
            }
            cout << version << endl;
            cout << type << endl;
            cout << data << endl;
            cout << opt << endl;
            regex_t sre;
            gchar *itemreg;
            GstCaps *caps;
            caps = gst_caps_new_simple(data.c_str(), opt.c_str());
            itemreg = g_strdup_printf("^%s:.* %s\\(, %s\\(,.*\\|;.*\\|$\\)\\|;\\|$\\)",
                          type.c_str(),
                          data.c_str(),
                          opt.c_str());
            if(regcomp(&sre, itemreg, REG_NEWLINE | REG_NOSUB) == 0) {
                search.push_back(pair<string, regex_t>(version, sre));
            } else {
                g_debug("Search regex compilation error: ", itemreg);
            }
            g_free(itemreg);
        } else {
            g_debug("Did not match: %s", value);
        }
    }
    regfree(&pkre);

    // If nothing matched just return
    if (search.size() == 0) {
        return;
    }
}

gstMatcher::~gstMatcher()
{
}
