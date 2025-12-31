/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2025 Neal Gompa <neal@gompa.dev>
 *
 * Licensed under the GNU General Public License Version 2
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
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

#include "dnf5-backend-vendor.hpp"
#include <vector>
#include <string>

bool dnf5_validate_supported_repo(const std::string &id)
{
	const std::vector<std::string> valid_sourcesect = { "", "-core", "-nonfree", "-tainted" };
	const std::vector<std::string> valid_sourcetype = { "", "-debuginfo", "-source" };
	const std::vector<std::string> valid_arch = { "x86_64", "i586", "armv7hl", "aarch64" };
	const std::vector<std::string> valid_stage = { "", "-updates", "-testing" };
	const std::vector<std::string> valid = { "mageia", "updates", "testing", "cauldron" };

	for (const auto &v : valid) {
		for (const auto &s : valid_stage) {
			for (const auto &a : valid_arch) {
				for (const auto &sec : valid_sourcesect) {
					for (const auto &t : valid_sourcetype) {
						if (id == v + s + "-" + a + sec + t) {
							return true;
						}
					}
				}
			}
		}
	}
	return false;
}
