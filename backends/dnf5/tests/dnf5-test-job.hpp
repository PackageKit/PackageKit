/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
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

#pragma once

#include <pk-backend.h>
#include <string>
#include <vector>

// What the backend reported to its job, in the order it was reported.
struct Dnf5TestJob {
	struct ItemProgress {
		std::string package_id;
		PkStatusEnum status;
		guint percentage;
	};
	struct PackageStatus {
		std::string package_id;
		PkInfoEnum info;
	};

	std::vector<PkStatusEnum> statuses;
	std::vector<guint> percentages;
	std::vector<guint> speeds;
	std::vector<guint64> download_size_remaining;
	std::vector<ItemProgress> item_progress;
	std::vector<PackageStatus> package_statuses;

	void reset();
};

extern Dnf5TestJob dnf5_test_job;
