#include "builder.h"

#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>

struct gsa_builder_t::impl_t
{
    std::unordered_map<llvm::PHINode const *, std::vector<gating_cond_ptr_t>> conditions;
    llvm::DominatorTreeWrapperPass dtwp;
    std::unordered_map<llvm::Value const *, std::pair<llvm::Value const *, llvm::ICmpInst::Predicate>> preds;
    std::unordered_set<llvm::BasicBlock const *> visited_blocks;
};

gsa_builder_t::gsa_builder_t()
    : pimpl_(std::make_unique<impl_t>())
{
}

gsa_builder_t::~gsa_builder_t()
{
}

void gsa_builder_t::build(llvm::Module const & module)
{
    for (auto it = module.rbegin(); it != module.rend(); ++it)
        process_function(*it);
}

void gsa_builder_t::process_function(llvm::Function const & func)
{
    pimpl().dtwp.runOnFunction(*const_cast<llvm::Function *>(&func));

    llvm::DominatorTree const & dom_tree = pimpl().dtwp.getDomTree();
    for (auto const & bb : func)
        process_basic_block_recursive(bb, dom_tree);
}

// This function traverses basic blocks from function's CFG in depth-first order.
// Here we compute gating paths and build gating functions.
void gsa_builder_t::process_basic_block_recursive(llvm::BasicBlock const & bb, llvm::DominatorTree const & dt)
{
    if (pimpl().visited_blocks.count(&bb))
        return;

    pimpl().visited_blocks.insert(&bb);
    for (auto const & instr : bb)
    {
        if (auto phi = dynamic_cast<llvm::PHINode const *>(&instr))
        {
            unsigned n = phi->getNumIncomingValues();
            for (size_t i = 0; i != n; ++i)
            {
                llvm::Value const * inc_val = phi->getIncomingValue(i);
                auto iter = pimpl().conditions.find(phi);
                if (iter != pimpl().conditions.end())
                {
                    if (iter->second.size() <= i)
                        continue;

                    gating_cond_ptr_t old_cond = iter->second[i];
                    gating_cond_ptr_t to_conjunct = construct_gating_cond(inc_val, false);
                    gating_cond_ptr_t new_cond = std::make_shared<conjuncted_gating_cond_t>(old_cond, to_conjunct);
                    pimpl().conditions[phi][i] = std::move(new_cond);
//                    pimpl().conditions.erase(iter);
//                    auto to_insert = std::make_pair(phi, );
//                    pimpl().conditions.insert(std::move(to_insert));
                }
                else if (pimpl().conditions[phi].size())
                {
                    pimpl().conditions[phi][i] = construct_gating_cond(inc_val, true);
                }
            }

            if (n == 2 && pimpl().conditions[phi].size())
            {
                gating_cond_ptr_t lhs = construct_gating_cond(phi->getIncomingValue(0), false);
                gating_cond_ptr_t rhs = construct_gating_cond(phi->getIncomingValue(1), false);
                gating_cond_ptr_t conj = std::make_shared<conjuncted_gating_cond_t>(lhs, rhs);
                pimpl().conditions[phi].push_back(conj);
                pimpl().conditions[phi].push_back(rhs);
            }
        }
        if (auto cmp_inst = dynamic_cast<llvm::ICmpInst const *>(&instr))
        {
            auto op0 = cmp_inst->getOperand(0);
            auto op1 = cmp_inst->getOperand(1);
            switch (cmp_inst->getPredicate())
            {
            case llvm::ICmpInst::ICMP_EQ:
                {
                    pimpl().preds[op0] = std::make_pair(op1, llvm::ICmpInst::ICMP_NE);
                    pimpl().preds[op1] = std::make_pair(op0, llvm::ICmpInst::ICMP_NE);
                    break;
                }
            case llvm::ICmpInst::ICMP_NE:
                {
                    pimpl().preds[op0] = std::make_pair(op1, llvm::ICmpInst::ICMP_EQ);
                    pimpl().preds[op1] = std::make_pair(op0, llvm::ICmpInst::ICMP_EQ);
                    break;
                }
            case llvm::ICmpInst::ICMP_UGT:
            case llvm::ICmpInst::ICMP_SGT:
                {
                    pimpl().preds[op0] = std::make_pair(op1, llvm::ICmpInst::ICMP_UGT);
                    pimpl().preds[op1] = std::make_pair(op0, llvm::ICmpInst::ICMP_ULE);
                    break;
                }
            case llvm::ICmpInst::ICMP_UGE:
            case llvm::ICmpInst::ICMP_SGE:
                {
                    pimpl().preds[op0] = std::make_pair(op1, llvm::ICmpInst::ICMP_UGE);
                    pimpl().preds[op1] = std::make_pair(op0, llvm::ICmpInst::ICMP_ULT);
                    break;
                }
            case llvm::ICmpInst::ICMP_ULT:
            case llvm::ICmpInst::ICMP_SLT:
                {
                    pimpl().preds[op0] = std::make_pair(op1, llvm::ICmpInst::ICMP_ULT);
                    pimpl().preds[op1] = std::make_pair(op0, llvm::ICmpInst::ICMP_UGE);
                    break;
                }
            case llvm::ICmpInst::ICMP_ULE:
            case llvm::ICmpInst::ICMP_SLE:
                {
                    pimpl().preds[op0] = std::make_pair(op1, llvm::ICmpInst::ICMP_ULE);
                    pimpl().preds[op1] = std::make_pair(op0, llvm::ICmpInst::ICMP_UGT);
                    break;
                }
            default:
                {
                }
            }
        }
        if (auto terminator = dynamic_cast<llvm::TerminatorInst const *>(&instr))
        {
            for (unsigned i = 0; i != terminator->getNumSuccessors(); ++i)
            {
                llvm::BasicBlock const * succ = terminator->getSuccessor(i);
                if (succ)
                {
                    process_basic_block_recursive(*succ, dt);
                }
            }
        }
    }
}

llvm::Instruction const * gsa_builder_t::subroot(llvm::Instruction const * instr)
{
    llvm::BasicBlock const * parent = instr->getParent();
    llvm::TerminatorInst const * par_term = parent->getTerminator();
    if (dynamic_cast<llvm::ReturnInst const *>(par_term))
        return nullptr;
    else if (auto br_instr = dynamic_cast<llvm::BranchInst const *>(par_term))
    {
        unsigned n = br_instr->getNumSuccessors();
        for (unsigned i = 0; i != n; ++i)
        {
            llvm::BasicBlock const * succ = br_instr->getSuccessor(i);
            if (succ)
            {
                return &(succ->front());
            }
        }
    }

    return par_term;
}

gating_cond_ptr_t gsa_builder_t::construct_gating_cond(llvm::Value const * val, bool negate) const
{
    if (negate)
        return std::make_shared<simple_gating_cond_t>(val);
    else
        return std::make_shared<negated_gating_cond_t>(val);
}

gating_cond_t const * gsa_builder_t::get_gating_condition(llvm::PHINode const & phi, unsigned i) const
{
    auto const & conditions = pimpl().conditions;
    auto it = conditions.find(&phi);
    if (it == conditions.end())
        return nullptr;
    else if (it->second.size() > i)
        return it->second[i].get();
    else
        return nullptr;
}

gsa_builder_t::impl_t & gsa_builder_t::pimpl()
{
    return *pimpl_;
}

gsa_builder_t::impl_t const & gsa_builder_t::pimpl() const
{
    return *pimpl_;
}
