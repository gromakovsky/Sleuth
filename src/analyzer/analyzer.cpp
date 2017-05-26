#include "analyzer.h"
#include "analyzer/impl.h"

#include <iostream>

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

// Returns true if there definitely is an overflow, indeterminate if it can't
// determine presense of overflow and false if there is definitely no overflow
tribool check_overflow(sym_range const & size_range, sym_range const & idx_range)
{
    if (size_range.hi <= idx_range.hi || idx_range.lo <= sym_expr(scalar_t(-1)))
        return true;

    if (sym_expr(scalar_t(0)) <= idx_range.lo
            && idx_range.hi <= size_range.lo - sym_expr(scalar_t(1)))
        return false;

    return boost::logic::indeterminate;
}

/* ------------------------------------------------
 * Computing buffer size
 * ------------------------------------------------
 */

// size is number of elements, not bytes
sym_range analyzer_t::compute_buffer_size_range(llvm::Value const & v)
{
    if (auto llvm_arg = dynamic_cast<llvm::Argument const *>(&v))
    {
        argument_t arg = {llvm_arg->getParent(), llvm_arg->getArgNo()};
        auto iter = pimpl().ctx.arg_size_ranges.find(arg);
        if (iter != pimpl().ctx.arg_size_ranges.end())
            return iter->second;
    }

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

vulnerability_info_t analyzer_t::is_access_vulnerable(llvm::Value const & v)
{
    auto cached = pimpl().ctx.vulnerability_info.find(&v);
    if (cached != pimpl().ctx.vulnerability_info.end())
        return cached->second;

    vulnerability_info_t res = { false, sym_range::empty, sym_range::empty };
    if (auto gep = dynamic_cast<llvm::GetElementPtrInst const *>(&v))
        res = is_access_vulnerable_gep(*gep);

    pimpl().ctx.vulnerability_info.insert({&v, res});
    return res;
}

vulnerability_info_t analyzer_t::is_access_vulnerable_gep(llvm::GetElementPtrInst const & gep)
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

    return { check_overflow(buf_size, idx_range), idx_range, buf_size };
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
    vulnerability_info_t vuln_info = is_access_vulnerable(ptr_val);
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
    for (size_t i = 0; i != call.getNumArgOperands(); ++i)
    {
        argument_t arg = {called, i};
        // compute value range
        {
            sym_range range = compute_use_range(call.getArgOperand(i), &call);
            auto iter = pimpl().ctx.arg_ranges.find(arg);
            if (iter != pimpl().ctx.arg_ranges.end())
            {
                range |= iter->second;
                pimpl().ctx.arg_ranges.erase(arg);
            }

            pimpl().ctx.arg_ranges.insert({arg, range});
        }
        // same for buffer size range
        {
            sym_range range = compute_buffer_size_range(*call.getArgOperand(i));
            auto iter = pimpl().ctx.arg_size_ranges.find(arg);
            if (iter != pimpl().ctx.arg_size_ranges.end())
            {
                range |= iter->second;
                pimpl().ctx.arg_size_ranges.erase(arg);
            }

            pimpl().ctx.arg_size_ranges.insert({arg, range});
        }
    }
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
