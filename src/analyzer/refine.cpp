#include "analyzer.h"

#include <iterator>

#include <llvm/ADT/StringRef.h>
#include <llvm/Pass.h>
#include <llvm/Analysis/MemoryBuiltins.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Analysis/CFG.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>

/* ------------------------------------------------
 * Control dependencies
 * ------------------------------------------------
 */

// Custom version of 'llvm::isPotentiallyReachable'.
// It takes three basic blocks: source, destination and checkpoint.
// It works like 'llvm::isPotentiallyReachable' but ignores edges from
// checkpoint to destination.
//
// It's mostly copy-pasted from llvm sources.
bool is_potentially_reachable_custom(llvm::BasicBlock const * source, llvm::BasicBlock const * dest,
                                     llvm::BasicBlock const * checkpoint, llvm::DominatorTree const & DT)
{
    llvm::SmallVector<llvm::BasicBlock const *, 32> worklist;
    worklist.push_back(source);

    // When the stop block is unreachable, it's dominated from everywhere,
    // regardless of whether there's a path between the two blocks.
    if (!DT.isReachableFromEntry(dest))
      return false;

    // Limit the number of blocks we visit. The goal is to avoid run-away compile
    // times on large CFGs without hampering sensible code. Arbitrarily chosen.
    unsigned Limit = 32;
    llvm::SmallPtrSet<llvm::BasicBlock const *, 32> visited;
    do {
      llvm::BasicBlock const * BB = worklist.pop_back_val();
      if (!visited.insert(BB).second)
        continue;
      if (BB == dest)
        return true;
//      if (DT.dominates(BB, dest))
//        return true;

      if (!--Limit) {
        // We haven't been able to prove it one way or the other. Conservatively
        // answer true -- that there is potentially a path.
        return true;
      }

      if (BB != checkpoint)
        worklist.append(llvm::succ_begin(BB), llvm::succ_end(BB));

    } while (!worklist.empty());

    // We have exhausted all possible paths and are certain that 'To' can not be
    // reached from 'From'.
    return false;
}

analyzer_t::predicates_t analyzer_t::collect_predicates(llvm::BasicBlock const * bb)
{
    if (!bb)
        return {};

    llvm::Function const * func = bb->getParent();
    if (!func)
        return {};

    // Currently it works as follows:
    // 1. Iterate through all blocks within function and collect dominators.
    // 2. Iterate through all dominators and examine reachability from successors.
    //
    // Probably it can be optimized.

    ctx_.dtwp.runOnFunction(*const_cast<llvm::Function *>(func));

    llvm::DominatorTree const & dom_tree = ctx_.dtwp.getDomTree();
    std::vector<llvm::BasicBlock const *> dominators;
    for (auto const & another_bb : *func)
    {
        if (dom_tree.properlyDominates(&another_bb, bb))
            dominators.push_back(&another_bb);
    }

    predicates_t predicates;
    for (llvm::BasicBlock const * dominator : dominators)
    {
        llvm::TerminatorInst const * terminator = dominator->getTerminator();
        if (auto br = dynamic_cast<llvm::BranchInst const *>(terminator))
        {
            if (br->isUnconditional())
                continue;

            llvm::BasicBlock const * true_bb = br->getSuccessor(0);
            llvm::BasicBlock const * false_bb = br->getSuccessor(1);
            bool reachable_from_true = is_potentially_reachable_custom(true_bb, bb, dominator, dom_tree);
            bool reachable_from_false = is_potentially_reachable_custom(false_bb, bb, dominator, dom_tree);
//            debug_out_ << "dominator: " << *dominator->getTerminator()
//                       << ", reachable from true: " << reachable_from_true
//                       << ", reachable from false: " << reachable_from_false
//                       << "\n";
            bool is_true_succ;
            if (reachable_from_false && !reachable_from_true)
                is_true_succ = false;
            else if (reachable_from_true && !reachable_from_false)
                is_true_succ = true;
            else
                continue;

            if (auto cmp_inst = dynamic_cast<llvm::ICmpInst const *>(br->getCondition()))
            {
                auto add_pred = [&predicates, cmp_inst](bool swap_args, predicate_type pr_type)
                {
                    predicate_t pred = {pr_type,
                                        swap_args ? cmp_inst->getOperand(1)
                                                  : cmp_inst->getOperand(0),
                                        swap_args ? cmp_inst->getOperand(0)
                                                  : cmp_inst->getOperand(1),
                                        cmp_inst};
                    predicates.push_back(pred);
                };

                switch (cmp_inst->getPredicate())
                {
                case llvm::ICmpInst::ICMP_EQ:
                    {
                        auto pr_type = is_true_succ ? PT_EQ : PT_NE;
                        add_pred(false, pr_type);
                        break;
                    }
                case llvm::ICmpInst::ICMP_NE:
                    {
                        auto pr_type = is_true_succ ? PT_NE : PT_EQ;
                        add_pred(false, pr_type);
                        break;
                    }
                case llvm::ICmpInst::ICMP_UGT:
                case llvm::ICmpInst::ICMP_SGT:
                    {
                        auto pr_type = is_true_succ ? PT_LT : PT_LE;
                        bool swap_args = is_true_succ;
                        add_pred(swap_args, pr_type);
                        break;
                    }
                case llvm::ICmpInst::ICMP_UGE:
                case llvm::ICmpInst::ICMP_SGE:
                    {
                        auto pr_type = is_true_succ ? PT_LE : PT_LT;
                        bool swap_args = is_true_succ;
                        add_pred(swap_args, pr_type);
                        break;
                    }
                case llvm::ICmpInst::ICMP_ULT:
                case llvm::ICmpInst::ICMP_SLT:
                    {
                        auto pr_type = is_true_succ ? PT_LT : PT_LE;
                        bool swap_args = !is_true_succ;
                        add_pred(swap_args, pr_type);
                        break;
                    }
                case llvm::ICmpInst::ICMP_ULE:
                case llvm::ICmpInst::ICMP_SLE:
                    {
                        auto pr_type = is_true_succ ? PT_LE : PT_LT;
                        bool swap_args = !is_true_succ;
                        add_pred(swap_args, pr_type);
                        break;
                    }
                default:
                    {
                    }
                }
            }
        }
    }

    return predicates;
}

sym_range analyzer_t::refine_def_range(var_id v, sym_range def_range, program_point_t p)
{
    if (!p)
        return def_range;

    llvm::BasicBlock const * bb = p->getParent();
    predicates_t predicates = collect_predicates(bb);
    for (predicate_t const & predicate : predicates)
        def_range = refine_def_range_internal(v, def_range, predicate);

    return def_range;
}


struct match_res_t
{
    scalar_t coeff;
    scalar_t delta;
};

// If v = c1 * to_match_with + c2, this function returns match_res_t(c1, c2).
boost::optional<match_res_t> match_var(var_id v, var_id to_match_with)
{
    if (v == to_match_with)
        return match_res_t({1, 0});

    if (auto bin_op = dynamic_cast<llvm::BinaryOperator const *>(to_match_with))
    {
        var_id op0 = bin_op->getOperand(0), op1 = bin_op->getOperand(1);
        if (op0 != v && op1 != v)
            return boost::none;

        bool v_is0 = op0 == v;
        if (auto scalar = extract_const_maybe(v_is0 ? op1 : op0))
        {
            switch (bin_op->getOpcode())
            {
            case llvm::BinaryOperator::Add:
                return match_res_t({1, -*scalar});
            case llvm::BinaryOperator::Sub:
                return v_is0
                        ? match_res_t({1, *scalar})
                        : match_res_t({-1, *scalar});
            case llvm::BinaryOperator::SDiv:
                if (v_is0)
                    return match_res_t({*scalar, 0});
            default: break;
            }
        }
    }

    if (auto bin_op = dynamic_cast<llvm::BinaryOperator const *>(v))
    {
        var_id op0 = bin_op->getOperand(0), op1 = bin_op->getOperand(1);
        if (op0 != to_match_with && op1 != to_match_with)
            return boost::none;

        bool tmw_is0 = op0 == to_match_with;
        if (auto scalar = extract_const_maybe(tmw_is0 ? op1 : op0))
        {
            switch (bin_op->getOpcode())
            {
            case llvm::BinaryOperator::Add:
                return match_res_t({1, *scalar});
            case llvm::BinaryOperator::Sub:
                return tmw_is0
                        ? match_res_t({1, -*scalar})
                        : match_res_t({-1, *scalar}) ;
            case llvm::BinaryOperator::Mul:
                return match_res_t({*scalar, 0});
            default: break;
            }
        }
    }

    return boost::none;
}

sym_range analyzer_t::refine_def_range_internal(var_id v, sym_range const & def_range,
                                                analyzer_t::predicate_t const & pred)
{
    // a = lhs, b = rhs
    var_id op2 = nullptr;
    boost::optional<match_res_t> match_res;
    if ((match_res = match_var(v, pred.lhs)))
        op2 = pred.rhs;
    else if ((match_res = match_var(v, pred.rhs)))
        op2 = pred.lhs;

    if (!op2 || !match_res)
        return def_range;

    sym_range op2_range = compute_use_range(op2, pred.program_point);

    if (pred.type == PT_NE)
    {
        if (op2_range.lo == op2_range.hi)
        {
            if (auto scalar = op2_range.lo.to_scalar())
            {
                scalar_t transformed_scalar = match_res->coeff * *scalar + match_res->delta;
                if (auto lo_scalar = def_range.lo.to_scalar())
                {
                    if (*lo_scalar == transformed_scalar)
                    {
                        return {sym_expr(*lo_scalar + 1), def_range.hi};
                    }
                }

                if (auto hi_scalar = def_range.hi.to_scalar())
                {
                    if (*hi_scalar == transformed_scalar)
                    {
                        return {def_range.lo, sym_expr(*hi_scalar - 1)};
                    }
                }
            }
        }

        return def_range;
    }

    sym_expr coeff_expr(match_res->coeff);
    sym_expr delta_expr(match_res->delta);
    sym_range to_intersect = sym_range::full;
    switch (pred.type)
    {
    case PT_EQ:
    {
        to_intersect = coeff_expr * op2_range + sym_range({delta_expr, delta_expr});
        break;
    }
    case PT_LT:
    {
        if ((op2 == pred.rhs) ^ (match_res->coeff < 0))
            to_intersect = {sym_expr::bot, coeff_expr * op2_range.hi + delta_expr - sym_expr(scalar_t(1))};
        else
            to_intersect = {coeff_expr * op2_range.lo + delta_expr + sym_expr(scalar_t(1)), sym_expr::top};
        break;
    }
    case PT_LE:
    {
        // op2 == b ⇒ v = c1 * a + c2
        // if (op2 == b && match_res->coeff >= 0) then
        //     v ≤ c1 * op2 + c2
        if ((op2 == pred.rhs) ^ (match_res->coeff < 0))
            to_intersect = {sym_expr::bot, coeff_expr * op2_range.hi + delta_expr};
        else
            to_intersect = {coeff_expr * op2_range.lo + delta_expr, sym_expr::top};
        break;
    }
    case PT_NE:
    {
        // can't happen
        break;
    }
    }

    debug_out_ << "control dependency leads to intersection with " << to_intersect << "\n";
    return def_range & to_intersect;
}
