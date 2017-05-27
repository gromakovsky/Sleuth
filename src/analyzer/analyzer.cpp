#include "analyzer.h"
#include "analyzer/impl.h"

#include <iostream>
#include <functional>

#include <boost/logic/tribool.hpp>

#include <llvm/ADT/StringRef.h>
#include <llvm/Pass.h>
#include <llvm/Analysis/MemoryBuiltins.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>

namespace fs = boost::filesystem;
using llvm::dyn_cast;
using llvm::dyn_cast_or_null;

/* ------------------------------------------------
 * Overflow check
 * ------------------------------------------------
 */

using boost::tribool;

bool is_argument_only(var_id var)
{
    if (!!dynamic_cast<llvm::Argument const *>(var))
    {
        return true;
    }
    else if (auto sext = dynamic_cast<llvm::SExtInst const *>(var))
    {
        return is_argument_only(sext->getOperand(0));
    }
    else if (auto zext = dynamic_cast<llvm::ZExtInst const *>(var))
    {
        return is_argument_only(zext->getOperand(0));
    }

    return false;
}

bool is_argument_only(sym_atomic_ptr const & atom)
{
    auto ptr = atom.get();
    if (dynamic_cast<atomic_const const *>(ptr))
    {
        return true;
    }
    else if (auto var = dynamic_cast<atomic_var const *>(ptr))
    {
        return is_argument_only(var->var());
    }
    else if (auto linear = dynamic_cast<atomic_linear const *>(ptr))
    {
        return is_argument_only(linear->atom());
    }
    else if (auto bin_op = dynamic_cast<atomic_bin_op const *>(ptr))
    {
        return is_argument_only(bin_op->lhs()) && is_argument_only(bin_op->rhs());
    }

    return false;
}

bool is_argument_only(sym_expr const & e)
{
    return is_argument_only(e.to_atom());
}

// Result of overflow check. Can be either verdict or a set of triggers.
struct check_overflow_res_t
{
    tribool verdict;
    std::vector<trigger_t> triggers;
};

// Returns true if there definitely is an overflow, indeterminate if it can't
// determine presense of overflow and false if there is definitely no overflow
check_overflow_res_t check_overflow(sym_range const & size_range, sym_range const & idx_range,
                                    llvm::Instruction const & instr)
{
    if (size_range.hi <= idx_range.hi || idx_range.lo <= sym_expr(scalar_t(-1)))
        return {true, {}};

    std::vector<trigger_t> triggers;
    bool a1 = is_argument_only(size_range.hi);
    bool a2 = is_argument_only(idx_range.hi);
    if (is_argument_only(size_range.hi) && is_argument_only(idx_range.hi))
    {
        triggers.push_back({size_range.hi, idx_range.hi, instr});
    }

    if (is_argument_only(idx_range.lo))
    {
        triggers.push_back(trigger_t(idx_range.lo, sym_expr(scalar_t(-1)), instr));
    }

    if (!triggers.empty())
        return {false, triggers};

    if (sym_expr(scalar_t(0)) <= idx_range.lo
            && idx_range.hi <= size_range.lo - sym_expr(scalar_t(1)))
        return {false, {}};

    return {boost::logic::indeterminate, {}};
}

/* ------------------------------------------------
 * Computing buffer size
 * ------------------------------------------------
 */

// size is number of elements, not bytes
sym_range analyzer_t::compute_buffer_size_range(llvm::Value const & v)
{
//    if (auto llvm_arg = dynamic_cast<llvm::Argument const *>(&v))
//    {
//        argument_t arg = {llvm_arg->getParent(), llvm_arg->getArgNo()};
//        auto iter = pimpl().ctx.arg_size_ranges.find(arg);
//        if (iter != pimpl().ctx.arg_size_ranges.end())
//            return iter->second;
//    }

    llvm::TargetLibraryInfo const & tli = pimpl().ctx.tliwp.getTLI();
    if (auto alloca = dynamic_cast<llvm::AllocaInst const *>(&v))
        return compute_use_range(alloca->getArraySize(), alloca);
    else if (auto call = dynamic_cast<llvm::CallInst const *>(&v))
    {
        if (llvm::isAllocationFn(&v, &tli, true))
        {
            auto res = compute_use_range(call->getArgOperand(0), call);   // TODO: can it be improved?
            pimpl().debug_out << "Allocated " << res << "\n";
            return res;
        }
    }
    else if (auto bitcast = dynamic_cast<llvm::BitCastInst const *>(&v))
    {
        auto src_type = bitcast->getSrcTy();
        auto dst_type = bitcast->getDestTy();
        if (auto src_ptr_type = dyn_cast_or_null<llvm::PointerType>(src_type))
        {
            if (auto dst_ptr_type = dyn_cast_or_null<llvm::PointerType>(dst_type))
            {
                llvm::Type * src_element_type = src_ptr_type->getElementType();
                llvm::Type * dst_element_type = dst_ptr_type->getElementType();
                if (auto src_int_type = dyn_cast_or_null<llvm::IntegerType>(src_element_type))
                {
                    if (auto dst_int_type = dyn_cast_or_null<llvm::IntegerType>(dst_element_type))
                    {
                        if (src_int_type->getBitWidth() == 8)
                        {
                            unsigned dst_width = dst_int_type->getBitWidth();
                            if (dst_width % 8 == 0)
                            {
                                sym_expr k(scalar_t(dst_width / 8));
                                llvm::Value * operand = bitcast->getOperand(0);
                                if (operand)
                                {
                                    return compute_buffer_size_range(*operand) / k;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    else if (auto const_seq = dynamic_cast<llvm::ConstantDataSequential const *>(&v))
    {
        return const_sym_range(const_seq->getNumElements());
    }

    return { sym_expr(scalar_t(1)), sym_expr::top };
}

/* ------------------------------------------------
 * Vulnerability detection
 * ------------------------------------------------
 */

vulnerability_info_t analyzer_t::is_access_vulnerable(llvm::Value const & v,
                                                      llvm::Instruction const & instr)
{
    auto cached = pimpl().ctx.vulnerability_info.find(&v);
    if (cached != pimpl().ctx.vulnerability_info.end())
        return cached->second;

    vulnerability_info_t res = { false, sym_range::empty, sym_range::empty };
    if (auto gep = dynamic_cast<llvm::GetElementPtrInst const *>(&v))
        res = is_access_vulnerable_gep(*gep, instr);

    pimpl().ctx.vulnerability_info.insert({&v, res});
    return res;
}

vulnerability_info_t analyzer_t::is_access_vulnerable_gep(llvm::GetElementPtrInst const & gep,
                                                          llvm::Instruction const & instr)
{
    auto source_type = gep.getSourceElementType();
    if (!source_type)
        pimpl().warn_out << "GEP instruction doesn't have source element type\n";

    pimpl().debug_out << "Processing GEP with source element type " << *source_type << "\n";
    llvm::Value const * pointer_operand = gep.getPointerOperand();
    if (!pointer_operand)
    {
        pimpl().warn_out << "GEP's pointer operand is null\n";
        return { false, sym_range::empty, sym_range::empty };
    }

    sym_range buf_size = compute_buffer_size_range(*pointer_operand);
    pimpl().debug_out << "GEP's pointer operand's buffer size is in range " << buf_size << "\n";

    sym_range idx_range = compute_use_range(*gep.idx_begin(), &gep);
    pimpl().debug_out << "GEP's base index is in range " << idx_range << "\n";

    check_overflow_res_t res = check_overflow(buf_size, idx_range, instr);
    for (trigger_t const & trigger : res.triggers)
    {
        pimpl().ctx.triggers.emplace(gep.getParent()->getParent(), trigger);
    }

    return { res.verdict, idx_range, buf_size };
}

/* ------------------------------------------------
 * Code processing
 * ------------------------------------------------
 */

void analyzer_t::process_instruction(llvm::Instruction const & instr)
{
    if (auto load = dynamic_cast<llvm::LoadInst const *>(&instr))
    {
        process_load(*load);
    }
    else if (auto store = dynamic_cast<llvm::StoreInst const *>(&instr))
    {
        process_store(*store);
    }
    else if (auto call = dynamic_cast<llvm::CallInst const *>(&instr))
    {
        process_call(*call);
    }
}

void analyzer_t::process_load(llvm::LoadInst const & load)
{
    if (auto pointer_operand = load.getPointerOperand())
        return process_memory_access(load, *pointer_operand);
    else
        pimpl().warn_out << "load instruction doesn't have a pointer operand\n";
}

void analyzer_t::process_store(llvm::StoreInst const & store)
{
    if (auto pointer_operand = store.getPointerOperand())
        return process_memory_access(store, *pointer_operand);
    else
        pimpl().warn_out << "store instruction doesn't have a pointer operand\n";
}

// Process instruction 'instr' which accesses memory pointed to by value 'ptr_val'.
void analyzer_t::process_memory_access(llvm::Instruction const & instr, llvm::Value const & ptr_val)
{
    vulnerability_info_t vuln_info = is_access_vulnerable(ptr_val, instr);
    if (vuln_info.decision)
        report_overflow(instr, vuln_info.idx_range, vuln_info.size_range);
    else if (boost::logic::indeterminate(vuln_info.decision))
        report_potential_overflow(instr, vuln_info.idx_range, vuln_info.size_range);
    else
        ++pimpl().total_correct;
}

void analyzer_t::process_call(llvm::CallInst const & call)
{
    llvm::Function const * called = call.getCalledFunction();
    auto triggers = pimpl().ctx.triggers.equal_range(called);
    for (auto it = triggers.first; it != triggers.second; ++it)
    {
        trigger_t const & trigger = it->second;
        pimpl().debug_out << "Processing trigger: "
                          << trigger.lhs
                          << " <= "
                          << trigger.rhs
                          << "\n"
                             ;
        tribool triggered = is_le_arg(trigger.lhs, trigger.rhs, call);
        if (triggered)
        {
            pimpl().debug_out << "TRIGGERED\n";
            report_overflow(*trigger.instr, boost::none, boost::none);
        }
        else if (boost::logic::indeterminate(triggered))
        {
            pimpl().debug_out << "Potentially triggered\n";
            report_potential_overflow(*trigger.instr, boost::none, boost::none);
        }
        else
        {
            pimpl().debug_out << "Didn't trigger\n";
        }
    }
}

tribool analyzer_t::is_le_arg(sym_expr const & e1, sym_expr const & e2, llvm::CallInst const & call)
{
    sym_range range1 = resolve_expr_arg(e1, call);
    sym_range range2 = resolve_expr_arg(e2, call);
    pimpl().debug_out << "Evaluated ranges for trigger: "
                      << range1 << ", "
                      << range2 << "\n"
                         ;

    if (range1.hi <= range2.lo)
        return true;

    // It should be '<', but '<' is not defined for symbolic expressions, so
    // we add 1.
    if (range2.hi <= range1.lo + sym_expr(scalar_t(1)))
        return false;

    return boost::indeterminate;
}

sym_range analyzer_t::resolve_expr_arg(sym_expr const & e, llvm::CallInst const & call)
{
    sym_atomic_ptr atom_shared = e.to_atom();

    std::function<sym_range(sym_atomic const *)> resolve_atom =
            [this, &call, &resolve_atom, &e](sym_atomic const * atom) -> sym_range
    {
        if (auto cnst = dynamic_cast<atomic_const const *>(atom))
        {
            return { sym_expr(cnst->value()), sym_expr(cnst->value()) };
        }
        else if (auto atomic = dynamic_cast<atomic_var const *>(atom))
        {
            var_id var = atomic->var();
            if (auto arg = dynamic_cast<llvm::Argument const *>(var))
            {
                if (arg->getParent() == call.getCalledFunction())
                {
                    unsigned i = arg->getArgNo();
                    var_id arg_operand = call.getArgOperand(i);
                    return compute_use_range(arg_operand, &call);
                }
                else
                {
                    pimpl().warn_out << "Function mismatch in resolve_expr_arg\n";
                }
            }
            else if (auto linear = dynamic_cast<atomic_linear const *>(atom))
            {
                return sym_expr(linear->coeff()) * resolve_atom(linear->atom().get());
            }
        }

        return { e, e };
    };

    return resolve_atom(atom_shared.get());
}

/* ------------------------------------------------
 * Constructor/destructor
 * ------------------------------------------------
 */

analyzer_t::analyzer_t(bool report_indeterminate,
                       llvm::raw_ostream & res_out,
                       llvm::raw_ostream & warn_out,
                       llvm::raw_ostream & debug_out)
    : pimpl_(new impl_t(report_indeterminate, res_out, warn_out, debug_out))
{
}

analyzer_t::~analyzer_t()
{
}
