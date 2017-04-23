#pragma once

#include <unordered_map>
#include <unordered_set>

#include <boost/logic/tribool.hpp>

#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/IR/Dominators.h>

#include "common.h"
#include "symbolic.h"

using new_val_set_t = std::unordered_set<var_id>;

struct vulnerability_info_t {
    // true = vulnerable
    // false = not vulnerable
    // interminated = may be vulnerable, but we don't know for sure
    boost::tribool decision;
    sym_range idx_range;
    sym_range size_range;
};

struct context_t
{
    std::unordered_map<var_id, sym_range> def_ranges;
    new_val_set_t new_val_set;
    std::unordered_map<var_id, vulnerability_info_t> vulnerability_info;
    llvm::TargetLibraryInfoWrapperPass tliwp;
    llvm::DominatorTreeWrapperPass dtwp;
};
