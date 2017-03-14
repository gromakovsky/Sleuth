#include "analyzer.h"

#include <llvm/ADT/StringRef.h>
#include <llvm/Pass.h>
#include <llvm/Analysis/MemoryBuiltins.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>

/* ------------------------------------------------
 * Control dependencies
 * ------------------------------------------------
 */

sym_range analyzer_t::refine_def_range(var_id v, sym_range const & def_range, program_point_t p)
{
    if (!p)
        return def_range;

    llvm::BasicBlock const * bb = p->getParent();
    if (!bb)
        return def_range;

    llvm::BasicBlock const * pred = bb->getSinglePredecessor();
    if (!pred)
        return def_range;

    llvm::TerminatorInst const * terminator = pred->getTerminator();
    if (auto br = dynamic_cast<llvm::BranchInst const *>(terminator))
    {
        bool is_true_succ = br->getSuccessor(0) == bb;
        if (auto cmp_inst = dynamic_cast<llvm::ICmpInst const *>(br->getCondition()))
        {
            auto refine = [this, v, &def_range, &cmp_inst](bool swap_args, analyzer_t::predicate_type pr_type)
            {
                return refine_def_range_internal(v, def_range, pr_type,
                                                 swap_args ? cmp_inst->getOperand(1) : cmp_inst->getOperand(0),
                                                 swap_args ? cmp_inst->getOperand(0) : cmp_inst->getOperand(1),
                                                 cmp_inst
                                                 );
            };

            switch (cmp_inst->getPredicate())
            {
            case llvm::ICmpInst::ICMP_EQ:
                {
                    auto pr_type = is_true_succ ? PT_EQ : PT_NE;
                    return refine(false, pr_type);
                }
            case llvm::ICmpInst::ICMP_NE:
                {
                    auto pr_type = is_true_succ ? PT_NE : PT_EQ;
                    return refine(false, pr_type);
                }
            case llvm::ICmpInst::ICMP_UGT:
            case llvm::ICmpInst::ICMP_SGT:
                {
                    auto pr_type = is_true_succ ? PT_LT : PT_LE;
                    bool swap_args = is_true_succ;
                    return refine(swap_args, pr_type);
                }
            case llvm::ICmpInst::ICMP_UGE:
            case llvm::ICmpInst::ICMP_SGE:
                {
                    auto pr_type = is_true_succ ? PT_LE : PT_LT;
                    bool swap_args = is_true_succ;
                    return refine(swap_args, pr_type);
                }
            case llvm::ICmpInst::ICMP_ULT:
            case llvm::ICmpInst::ICMP_SLT:
                {
                    auto pr_type = is_true_succ ? PT_LT : PT_LE;
                    bool swap_args = !is_true_succ;
                    return refine(swap_args, pr_type);
                }
            case llvm::ICmpInst::ICMP_ULE:
            case llvm::ICmpInst::ICMP_SLE:
                {
                    auto pr_type = is_true_succ ? PT_LE : PT_LT;
                    bool swap_args = !is_true_succ;
                    return refine(swap_args, pr_type);
                }
            default:
                {
                }
            }
        }
    }

    return def_range;
}

sym_range analyzer_t::refine_def_range_internal(var_id v, sym_range const & def_range,
                                                analyzer_t::predicate_type pt,
                                                var_id a, var_id b, program_point_t point)
{
    var_id op2 = nullptr;
    if (v == a)
        op2 = b;
    else if (v == b)
        op2 = a;

    if (!op2)
        return def_range;

    sym_range op2_range = compute_use_range(op2, point);

    if (pt == PT_NE)
    {
        if (op2_range.lo == op2_range.hi)
        {
            if (auto scalar = op2_range.lo.to_scalar())
            {
                if (auto lo_scalar = def_range.lo.to_scalar())
                {
                    if (*lo_scalar == *scalar)
                    {
                        return {sym_expr(*lo_scalar + 1), def_range.hi};
                    }
                }

                if (auto hi_scalar = def_range.hi.to_scalar())
                {
                    if (*hi_scalar == *scalar)
                    {
                        return {def_range.lo, sym_expr(*hi_scalar - 1)};
                    }
                }
            }
        }

        return def_range;
    }

    sym_range to_intersect = sym_range::full;
    switch (pt)
    {
    case PT_EQ:
    {
        to_intersect = compute_use_range(op2, point);
        break;
    }
    case PT_LT:
    {
        if (v == a)
            to_intersect = {sym_expr::bot, op2_range.hi - sym_expr(scalar_t(1))};
        else
            to_intersect = {op2_range.lo + sym_expr(scalar_t(1)), sym_expr::top};
        break;
    }
    case PT_LE:
    {
        if (v == a)
            to_intersect = {sym_expr::bot, op2_range.hi};
        else
            to_intersect = {op2_range.lo, sym_expr::top};
        break;
    }
    case PT_NE:
    {
        // can't happen
        break;
    }
    }

    llvm::outs() << "control dependency leads to intersection with " << to_intersect << "\n";
    return def_range & to_intersect;
}
