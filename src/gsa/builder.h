#pragma once

#include "cond.h"

#include <memory>

#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>

struct gsa_builder_t
{
    gsa_builder_t();

    ~gsa_builder_t();

    // build GSA form
    void build(llvm::Module const &);

    // all functions are processed separately
    void process_function(llvm::Function const &);

    // here we process basic blocks in depth first order
    void process_basic_block_recursive(llvm::BasicBlock const &, llvm::DominatorTree const &);

    // compute subroot (according to the paper)
    llvm::Instruction const * subroot(llvm::Instruction const *);

    // compute gating condition
    gating_cond_ptr_t construct_gating_cond(llvm::Value const *, bool negate) const;

    // returns gating condition for the i-th argument of give phi node if it is known
    gating_cond_t const * get_gating_condition(llvm::PHINode const &, unsigned index) const;

private:
    struct impl_t;
    std::unique_ptr<impl_t> pimpl_;
    impl_t & pimpl();
    impl_t const & pimpl() const;
};
