#pragma once

#include <llvm/IR/Value.h>

// Gating condition

struct gating_cond_t
{
    virtual ~gating_cond_t();
};

struct simple_gating_cond_t : gating_cond_t
{
    llvm::Value const * predicate;
};

struct negated_gating_cond_t : gating_cond_t
{
    llvm::Value const * predicate;
};

struct conjuncted_gating_cond_t : gating_cond_t
{
    gating_cond_t const * lhs;
    gating_cond_t const * rhs;
};
