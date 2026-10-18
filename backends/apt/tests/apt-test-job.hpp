// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>
#include <pk-backend.h>

struct AptTestJob {
    struct ItemProgress {
        std::string package_id;
        PkStatusEnum status;
        guint percentage;
    };
    std::vector<PkStatusEnum> statuses;
    std::vector<guint> percentages;
    std::vector<ItemProgress> items;
    std::vector<PkErrorEnum> errors;

    void reset()
    {
        *this = {};
    }
};

extern AptTestJob apt_test_job;
