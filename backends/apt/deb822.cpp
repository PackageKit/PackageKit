/*
 * Copyright (c) 2024-2025 Matthias Klumpp <matthias@tenstral.net>
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

#include "deb822.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <algorithm>


Deb822File::Deb822File()
{
}

bool Deb822File::isFieldStanza(const Stanza& stanza)
{
    return std::any_of(stanza.begin(), stanza.end(),
                       [](const Line& l) { return l.isField(); });
}

Deb822File::Line Deb822File::parseDeb822Line(const std::string& line) const
{
    Deb822File::Line l;
    l.content = line;

    // we return empty and comment-lines verbatim
    if (line.empty() || line[0] == '#')
        return l;

    if (std::isspace(line[0])) {
        l.isContinuation = true;
        return l;
    }

    const auto colonPos = line.find(':');
    if (colonPos != std::string::npos && colonPos > 0) {
        l.key = line.substr(0, colonPos);

        // strip leading space
        size_t valueStart = colonPos + 1;
        while (valueStart < line.size() && std::isspace(line[valueStart]))
            ++valueStart;

        l.value = line.substr(valueStart);
    }

    return l;
}

bool Deb822File::loadFromStream(std::istream& stream)
{
    m_allStanzas.clear();
    m_fieldStanzaIndices.clear();

    Stanza stanza;
    std::string line;
    Line *lastField = nullptr;

    while (std::getline(stream, line)) {
        if (line.empty()) {
            if (!stanza.empty()) {
                size_t index = m_allStanzas.size();
                m_allStanzas.push_back(stanza);
                if (isFieldStanza(stanza)) {
                    m_fieldStanzaIndices.push_back(index);
                }

                stanza.clear();
                lastField = nullptr;
            }
            continue;
        }

        auto parsed = parseDeb822Line(line);

        if (parsed.isContinuation && lastField) {
            // append to last field value (with newline)
            lastField->value += "\n" + parsed.content;
        } else {
            stanza.push_back(parsed);
            if (parsed.isField())
                lastField = &stanza.back();
            else
                lastField = nullptr;
        }
    }

    if (!stanza.empty()) {
        size_t index = m_allStanzas.size();
        m_allStanzas.push_back(stanza);
        if (isFieldStanza(stanza)) {
            m_fieldStanzaIndices.push_back(index);
        }
    }

    return true;
}

bool Deb822File::loadFromString(const std::string& content)
{
    std::istringstream stream(content);
    return loadFromStream(stream);
}

bool Deb822File::load(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file) {
        m_lastError = "Failed to open file: " + filename;
        return false;
    }
    m_filename = filename;

    return loadFromStream(file);
}

bool Deb822File::save(const std::string& filename)
{
    std::ofstream file(filename);
    if (!file) {
        m_lastError = "Failed to write file: " + filename;
        return false;
    }

    for (size_t i = 0; i < m_allStanzas.size(); ++i) {
        for (const auto& line : m_allStanzas[i])
            file << line.content << "\n";

        if (i + 1 < m_allStanzas.size())
            file << "\n";
    }

    return true;
}

std::string Deb822File::lastError() const
{
    return m_lastError;
}

std::optional<std::string> Deb822File::getFieldValue(size_t stanzaIndex, const std::string& field, std::optional<std::string> defaultValue)
{
    if (stanzaIndex >= m_fieldStanzaIndices.size()) {
        m_lastError = "Stanza index out of range";
        return std::nullopt;
    }

    const Stanza& stanza = m_allStanzas[m_fieldStanzaIndices[stanzaIndex]];
    for (const auto& line : stanza) {
        if (line.key == field) {
            return line.value;
        }
    }

    return defaultValue;
}

bool Deb822File::modifyField(size_t stanzaIndex, const std::string& field, const std::string& newValue)
{
    if (stanzaIndex >= m_fieldStanzaIndices.size()) {
        m_lastError = "Stanza index out of range";
        return false;
    }

    Stanza& stanza = m_allStanzas[m_fieldStanzaIndices[stanzaIndex]];
    for (auto it = stanza.begin(); it != stanza.end(); ++it) {
        if (it->key == field) {
            auto next = std::next(it);
            while (next != stanza.end() && next->isContinuation)
                next = stanza.erase(next);

            it->value = newValue;
            std::istringstream valstream(newValue);
            std::string line;
            std::getline(valstream, line);
            it->content = field + ": " + line;

            while (std::getline(valstream, line))
                stanza.insert(next, Line{" " + line, "", "", true});

            return true;
        }
    }

    std::istringstream valstream(newValue);
    std::string line;
    std::getline(valstream, line);
    stanza.push_back(Line{field + ": " + line, field, newValue, false});
    while (std::getline(valstream, line))
        stanza.push_back(Line{" " + line, "", "", true});

    return true;
}

size_t Deb822File::stanzaCount() const
{
    return m_fieldStanzaIndices.size();
}

std::string Deb822File::toString() const
{
    std::ostringstream out;
    for (size_t i = 0; i < m_allStanzas.size(); ++i) {
        for (const auto& line : m_allStanzas[i])
            out << line.content << "\n";

        if (i + 1 < m_allStanzas.size())
            out << "\n";
    }

    return out.str();
}
