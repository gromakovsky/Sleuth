#pragma once

#include <unordered_map>

#include "common.h"
#include "symbolic.h"

struct context_t
{
    std::unordered_map<var_id, sym_range> def_ranges;
};
