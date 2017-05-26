#pragma once

#include "cond.h"

#include <memory>

#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

struct gsa_builder_t
{
    gsa_builder_t();
    ~gsa_builder_t();

    // build GSA form
    void build(llvm::Module const &);

    // returns gating condition for the i-th argument of give phi node if it is known
    gating_cond_t const * get_gating_condition(llvm::PHINode const &, unsigned index) const;

private:
    struct impl_t;
    std::unique_ptr<impl_t> pimpl_;
    impl_t & pimpl();
    impl_t const & pimpl() const;
};
