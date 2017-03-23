#include "analyzer.h"

#include <iterator>

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

analyzer_t::predicates_t analyzer_t::collect_predicates(llvm::BasicBlock const * bb)
{
    if (!bb)
        return {};

    llvm::BasicBlock const * predecessor = bb->getSinglePredecessor();
    if (!predecessor)
        return {};

    predicates_t predicates;
    llvm::TerminatorInst const * terminator = predecessor->getTerminator();
    if (auto br = dynamic_cast<llvm::BranchInst const *>(terminator))
    {
        bool is_true_succ = br->getSuccessor(0) == bb;
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

    predicates_t res = collect_predicates(predecessor);
    std::copy(predicates.begin(), predicates.end(), std::back_inserter(res));

    return res;
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

    llvm::outs() << "control dependency leads to intersection with " << to_intersect << "\n";
    return def_range & to_intersect;
}
