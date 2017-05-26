#include "analyzer.h"
#include "analyzer/impl.h"

#include <boost/logic/tribool.hpp>

#include <llvm/ADT/StringRef.h>
#include <llvm/Pass.h>
#include <llvm/Analysis/MemoryBuiltins.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/IR/Constants.h>
#include <llvm/Support/SourceMgr.h>

using boost::tribool;

sym_range analyzer_t::compute_use_range(var_id const & v, program_point_t p)
{
    sym_range r = compute_def_range(v);
    return refine_def_range(v, std::move(r), p);
}

sym_range analyzer_t::compute_def_range(var_id const & v)
{
    if (!v)
        return var_sym_range(v);

    auto it = pimpl().ctx.def_ranges.find(v);
    if (it != pimpl().ctx.def_ranges.end())
        return it->second;

    if (auto llvm_arg = dynamic_cast<llvm::Argument const *>(v))
    {
        argument_t arg = {llvm_arg->getParent(), llvm_arg->getArgNo()};
        auto iter = pimpl().ctx.arg_ranges.find(arg);
        if (iter != pimpl().ctx.arg_ranges.end())
        {
            auto res = iter->second;
            pimpl().ctx.def_ranges.emplace(v, res);
            return res;
        }
    }

    if (auto const_v = dynamic_cast<llvm::Constant const *>(v))
    {
        auto res = compute_def_range_const(*const_v);
        pimpl().ctx.def_ranges.emplace(v, res);
        return res;
    }

    pimpl().ctx.new_val_set.insert(v);
    pimpl().ctx.def_ranges.emplace(v, sym_range::full);

    auto range = compute_def_range_internal(*v);
    pimpl().ctx.def_ranges.erase(v);
    pimpl().ctx.def_ranges.emplace(v, std::move(range));

    update_def_range(v);
    pimpl().ctx.new_val_set.erase(v);

    auto res_it = pimpl().ctx.def_ranges.find(v);
    return res_it == pimpl().ctx.def_ranges.end() ? sym_range::full : res_it->second;
}

void analyzer_t::update_def_range(var_id const & v)
{
    if (!v)
        return;

    for (var_id w : v->users())
    {
        if (!pimpl().ctx.new_val_set.count(w))
            continue;

        sym_range w_def_range = compute_def_range_internal(*w);
        auto cached_iter = pimpl().ctx.def_ranges.find(w);
        sym_range cached = cached_iter == pimpl().ctx.def_ranges.end() ? sym_range::full : cached_iter->second;
        w_def_range &= cached;
        if (w_def_range != cached)
        {
            pimpl().ctx.def_ranges.erase(w);
            pimpl().ctx.def_ranges.emplace(w, w_def_range);
            update_def_range(w);
        }
    }
}

sym_range analyzer_t::compute_def_range_const(llvm::Constant const & c)
{
    llvm::Type * t = c.getType();
    if (!t)
    {
        pimpl().warn_out << "Constant " << c.getName() << " doesn't have a type";
        return var_sym_range(&c);
    }

    if (auto scalar = extract_const_maybe(&c))
    {
        sym_expr e(*scalar);
        return {e, e};
    }

    pimpl().debug_out << "Can't compute def range of constant named \""
               << c.getName()
               << "\" with type \""
               << *t << "\"";

    return var_sym_range(&c);
}

sym_range analyzer_t::compute_def_range_internal(llvm::Value const & v)
{
    if (auto bin_op = dynamic_cast<llvm::BinaryOperator const *>(&v))
    {
        var_id op0 = bin_op->getOperand(0), op1 = bin_op->getOperand(1);
        sym_range op0_range = compute_use_range(op0, bin_op),
                  op1_range = compute_use_range(op1, bin_op);
        if (bin_op->getOpcode() == llvm::BinaryOperator::Add)
            return op0_range + op1_range;
        else if (bin_op->getOpcode() == llvm::BinaryOperator::Sub)
            return op0_range - op1_range;
        else if (bin_op->getOpcode() == llvm::BinaryOperator::Mul)
            return op0_range * op1_range;
        else if (bin_op->getOpcode() == llvm::BinaryOperator::SDiv)
            return op0_range / op1_range;
    }
    else if (auto phi = dynamic_cast<llvm::PHINode const *>(&v))
    {
        sym_range r(sym_range::empty);
        for (var_id inc_v : phi->incoming_values())
            r |= compute_use_range(inc_v, phi);

        if (phi->getNumIncomingValues() == 2)
        {
            var_id inc_v0 = phi->getIncomingValue(0);
            var_id inc_v1 = phi->getIncomingValue(1);

            auto test_value = [&r, phi, this](var_id dependent, var_id another)
            {
                switch (does_monotonically_depend(dependent, phi))
                {
                case MONOTONY_INC:
                {
                    predicate_t predicate = {PT_LE, another, phi, phi};
                    r = refine_def_range_internal(phi, r, predicate);
                    break;
                }
                case MONOTONY_DEC:
                {
                    predicate_t predicate = {PT_LE, phi, another, phi};
                    r = refine_def_range_internal(phi, r, predicate);
                    break;
                }
                default:
                    break;
                }
            };

            test_value(inc_v0, inc_v1);
            test_value(inc_v1, inc_v0);
        }

        return r;
    }
    else if (auto load = dynamic_cast<llvm::LoadInst const *>(&v))
    {
        if (auto gep = dynamic_cast<llvm::GetElementPtrInst const *>(load->getPointerOperand()))
        {
            if (gep->getNumIndices() == 2)  // TODO: check that 0-th index is 0
            {
                llvm::ConstantDataSequential const * const_seq =
                        dynamic_cast<llvm::ConstantDataSequential const *>(gep->getPointerOperand());
                if (!const_seq)
                {
                    if (auto gv = dynamic_cast<llvm::GlobalVariable const *>(gep->getPointerOperand()))
                    {
                        if (gv->isConstant())
                            const_seq = dynamic_cast<llvm::ConstantDataSequential const *>(gv->getInitializer());
                    }
                }
                if (const_seq)
                {
                    auto begin = gep->idx_begin();
                    begin++;
                    sym_range idx_range = compute_use_range(*begin, gep);
                    if (auto scalar_r = to_scalar_range(idx_range))
                    {
                        if (scalar_r->second < 0 || scalar_r->first >= const_seq->getNumElements())
                        {
                            load->getDebugLoc().print(pimpl().res_out);
                            pimpl().res_out << "vulnerable access of constant aggregate\n";
                        }
                        else
                        {
                            sym_range res = sym_range::empty;
                            for (unsigned i = scalar_r->first; i <= scalar_r->second; ++i)
                            {
                                scalar_t n(const_seq->getElementAsInteger(i));
                                sym_expr e(n);
                                res.lo = meet(res.lo, e);
                                res.hi = join(res.hi, e);
                            }
                            return res;
                        }
                    }
                }
            }
        }
    }
    else if (auto sext = dynamic_cast<llvm::SExtInst const *>(&v))
    {
        return compute_use_range(sext->getOperand(0), sext);
    }
    else if (auto sext = dynamic_cast<llvm::ZExtInst const *>(&v))
    {
        return compute_use_range(sext->getOperand(0), sext);
    }
    else if (auto type = v.getType())
    {
        if (type->isIntegerTy())
        {
            llvm::IntegerType * int_type = static_cast<llvm::IntegerType *>(type);
            auto bits = int_type->getBitMask();
            scalar_t max(bits >> 1);
            scalar_t min(-max - 1);
            return {sym_expr(min), sym_expr(max)};
        }
    }

    return var_sym_range(&v);
}
