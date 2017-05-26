#pragma once

#include <llvm/IR/Value.h>

// Gating condition

struct gating_cond_t
{
};

struct simple_gating_cond_t : gating_cond_t
{
    llvm::Value * predicate_;
};

struct negated_gating_cond_t : gating_cond_t
{
    llvm::Value * predicate_;
};

struct conjuncted_gating_cond_t : gating_cond_t
{
    llvm::Value * lhs_, rhs_;
};
