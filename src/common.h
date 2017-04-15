#pragma once

#include <boost/optional.hpp>

#include <llvm/IR/Value.h>
#include <llvm/ADT/APInt.h>
#include <llvm/IR/Constants.h>

using var_id = llvm::Value const *;
using scalar_t = std::int64_t;

scalar_t extract_const(llvm::ConstantInt const &);

boost::optional<scalar_t> extract_const_maybe(llvm::Value const *);

enum monotony_t {
    MONOTONY_NO,   // not monotonic
    MONOTONY_INC,  // increasing (not strictly)
    MONOTONY_DEC,  // decreasing (not strictly)
};

// This function detects situations when 'dependent = f(x)' and
// sequence of 'x f(x) f(f(x)) …' is monotonic
monotony_t does_monotonically_depend(var_id dependent, var_id x);
