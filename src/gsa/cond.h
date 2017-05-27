#pragma once

#include <memory>

#include <llvm/IR/Value.h>

// Gating condition

struct gating_cond_t
{
    virtual ~gating_cond_t();
};

using gating_cond_ptr_t = std::shared_ptr<gating_cond_t>;

struct simple_gating_cond_t : gating_cond_t
{
    simple_gating_cond_t(llvm::Value const *);

    llvm::Value const * predicate;
};

struct negated_gating_cond_t : gating_cond_t
{
    negated_gating_cond_t(llvm::Value const *);

    llvm::Value const * predicate;
};

struct conjuncted_gating_cond_t : gating_cond_t
{
    conjuncted_gating_cond_t(gating_cond_ptr_t lhs, gating_cond_ptr_t rhs);

    gating_cond_ptr_t lhs;
    gating_cond_ptr_t rhs;
};
