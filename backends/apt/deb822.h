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

#pragma once

#include <vector>
#include <string>
#include <optional>

/**
 * Read & write a Deb822 file.
 * This is the simplest possible parser for Deb822 files.
 * Unlike pkgTagFile, it retains all comments and allows for
 * non-destructive editing of Deb822 files.
 */
class Deb822File
{
public:
    explicit Deb822File();

    bool load(const std::string &filename);
    bool loadFromStream(std::istream &stream);
    bool loadFromString(const std::string &content);

    bool save(const std::string &filename);

    [[nodiscard]] std::string lastError() const;

    [[nodiscard]] size_t stanzaCount() const;
    [[nodiscard]] std::optional<std::string> getFieldValue(
        size_t stanzaIndex,
        const std::string &field,
        std::optional<std::string> defaultValue = std::nullopt);
    bool updateField(size_t stanza_index, const std::string &field, const std::string &newValue);
    bool deleteField(size_t stanzaIndex, const std::string &key);
    bool deleteStanza(size_t index);
    int duplicateStanza(size_t index);

    std::string toString() const;

private:
    struct Line {
        std::string content;
        std::string key;   /// empty if comment or continuation
        std::string value; /// only valid if key is non-empty

        bool isContinuation = false;
        [[nodiscard]] bool isField() const
        {
            return !key.empty();
        }
    };

    using Stanza = std::vector<Line>;

    std::string m_filename;
    std::string m_lastError;
    std::vector<Stanza> m_allStanzas;         // all stanzas, including comment-only ones
    std::vector<size_t> m_fieldStanzaIndices; // map for stanzas with fields

    static bool isFieldStanza(const Stanza &stanza);
    Line parseDeb822Line(const std::string &line) const;
};
