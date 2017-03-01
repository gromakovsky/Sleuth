#pragma once

#include <unordered_map>
#include <unordered_set>

#include "common.h"
#include "symbolic.h"

using new_val_set_t = std::unordered_set<var_id>;

struct context_t
{
    std::unordered_map<var_id, sym_range> val_ranges;
    new_val_set_t new_val_set;
    std::unordered_map<var_id, bool> vulnerability_info;
};
