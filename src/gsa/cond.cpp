#include "cond.h"

gating_cond_t::~gating_cond_t()
{
}

simple_gating_cond_t::simple_gating_cond_t(llvm::Value const * val)
    : predicate(val)
{
}

negated_gating_cond_t::negated_gating_cond_t(llvm::Value const * val)
    : predicate(val)
{
}

conjuncted_gating_cond_t::conjuncted_gating_cond_t(gating_cond_ptr_t lhs,
                                                   gating_cond_ptr_t rhs)
    : lhs(lhs)
    , rhs(rhs)
{
}
