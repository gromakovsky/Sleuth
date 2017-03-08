#include "analyzer.h"

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
 * Computing symbolic ranges
 * ------------------------------------------------
 */

sym_range analyzer_t::compute_use_range(var_id const & v, void *)
{
    return compute_def_range(v);
}

sym_range analyzer_t::compute_def_range(var_id const & v)
{
    if (auto const_v = dynamic_cast<llvm::Constant const *>(v))
        return compute_def_range_const(*const_v);

    if (!v)
        return var_sym_range(v);

    auto it = ctx_.val_ranges.find(v);
    if (it != ctx_.val_ranges.end())
        return it->second;

    ctx_.new_val_set.insert(v);
    ctx_.val_ranges.emplace(v, sym_range::full);

    auto range = compute_def_range_internal(*v);
    ctx_.val_ranges.erase(v);
    ctx_.val_ranges.emplace(v, std::move(range));

    update_def_range(v);
    ctx_.new_val_set.erase(v);

    auto res_it = ctx_.val_ranges.find(v);
    return res_it == ctx_.val_ranges.end() ? sym_range::full : res_it->second;
}

void analyzer_t::update_def_range(var_id const & v)
{

}

sym_range analyzer_t::compute_def_range_const(llvm::Constant const & c)
{
    llvm::Type * t = c.getType();
    if (!t)
    {
        warn_out_ << "Constant " << c.getName() << " doesn't have a type";
        return var_sym_range(&c);
    }

    if (auto i = dynamic_cast<llvm::ConstantInt const *>(&c))
    {
        llvm::APInt v = i->getValue();
        scalar_t scalar = v.getLimitedValue();  // TODO: not the best solution obviously
        sym_expr e(scalar);
        return {e, e};
    }

    debug_out_ << "Can't compute def range of constant named \""
               << c.getName()
               << "\" with type \""
               << *t << "\"";

    return var_sym_range(&c);
}

sym_range analyzer_t::compute_def_range_internal(llvm::Value const & v)
{
    if (auto bin_op = dynamic_cast<llvm::BinaryOperator const *>(&v))
    {
        if (bin_op->getOpcode() == llvm::BinaryOperator::Add)
            return compute_use_range(bin_op->getOperand(0)) + compute_use_range(bin_op->getOperand(1));
        else if (bin_op->getOpcode() == llvm::BinaryOperator::Sub)
            return compute_use_range(bin_op->getOperand(0)) - compute_use_range(bin_op->getOperand(1));
        else if (bin_op->getOpcode() == llvm::BinaryOperator::Mul)
            return compute_use_range(bin_op->getOperand(0)) * compute_use_range(bin_op->getOperand(1));
        else if (bin_op->getOpcode() == llvm::BinaryOperator::SDiv)
            return compute_use_range(bin_op->getOperand(0)) / compute_use_range(bin_op->getOperand(1));
    }

    return var_sym_range(&v);
}

// size is number of elements, not bytes
sym_range analyzer_t::compute_buffer_size_range(llvm::Value const & v)
{
    llvm::TargetLibraryInfo const & tli = ctx_.tliwp.getTLI();
    if (auto alloca = dynamic_cast<llvm::AllocaInst const *>(&v))
        return compute_use_range(alloca->getArraySize());
    else if (auto call = dynamic_cast<llvm::CallInst const *>(&v))
    {
        if (llvm::isAllocationFn(&v, &tli, true))
        {
            auto res = compute_use_range(call->getArgOperand(0));   // TODO: can it be improved?
            debug_out_ << "Allocated " << res << "\n";
            return res;
        }
    }
    else if (llvm::BitCastInst const * bitcast = dyn_cast<llvm::BitCastInst const>(&v))
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

    return sym_range::full;
}

tribool analyzer_t::is_access_vulnerable(llvm::Value const & v)
{
    auto cached = ctx_.vulnerability_info.find(&v);
    if (cached != ctx_.vulnerability_info.end())
        return cached->second;

    tribool res = true;
    if (auto gep = dynamic_cast<llvm::GetElementPtrInst const *>(&v))
        res = is_access_vulnerable_gep(*gep);

    ctx_.vulnerability_info.insert({&v, res});
    return res;
}

tribool analyzer_t::is_access_vulnerable_gep(llvm::GetElementPtrInst const & gep)
{
    auto source_type = gep.getSourceElementType();
    if (!source_type)
        warn_out_ << "GEP instruction doesn't have source element type\n";

    debug_out_ << "Processing GEP with source element type " << *source_type << "\n";
    llvm::Value const * pointer_operand = gep.getPointerOperand();
    if (!pointer_operand)
    {
        warn_out_ << "GEP's pointer operand is null\n";
        return false;
    }

    sym_range buf_size = compute_buffer_size_range(*pointer_operand);
    debug_out_ << "GEP's pointer operand's buffer size is in range " << buf_size << "\n";

    sym_range idx_range = compute_use_range(*gep.idx_begin());
    debug_out_ << "GEP's base index is in range " << idx_range << "\n";

    return check_overflow(buf_size, idx_range);
}

/* ------------------------------------------------
 * Reporting
 * ------------------------------------------------
 */

void analyzer_t::report_overflow(llvm::Instruction const & instr, bool sure)
{
    instr.getDebugLoc().print(res_out_);
    llvm::Function const * f = instr.getFunction();
    auto func_name = f ? f->getName() : "<unknown>";
    res_out_ << " | overflow "
             << (sure ? "is possible" : "may be possible (but not surely)")
             << " in function "
             << func_name
             << ", instruction "
             << instr.getOpcodeName()
             << "\n";
}

void analyzer_t::report_potential_overflow(llvm::Instruction const & instr)
{
    if (!report_indeterminate_)
        return;

    report_overflow(instr, false);
}

/* ------------------------------------------------
 * Code processing
 * ------------------------------------------------
 */

void analyzer_t::analyze_function(llvm::Function const & f)
{
    debug_out_ << "Analyzing function " << f.getName() << "\n";

    for (auto const & bb : f)
        analyze_basic_block(bb);
}

void analyzer_t::analyze_basic_block(llvm::BasicBlock const & bb)
{
//    debug_out_ << "Analyzing basic block " << bb.getName() << "\n";

    for (auto const & i : bb)
        process_instruction(i);
}

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
}

void analyzer_t::process_load(llvm::LoadInst const & load)
{
    if (auto pointer_operand = load.getPointerOperand())
        return process_memory_access(load, *pointer_operand);
    else
        warn_out_ << "load instruction doesn't have a pointer operand\n";
}

void analyzer_t::process_store(llvm::StoreInst const & store)
{
    if (auto pointer_operand = store.getPointerOperand())
        return process_memory_access(store, *pointer_operand);
    else
        warn_out_ << "store instruction doesn't have a pointer operand\n";
}

// Process instruction 'instr' which accesses memory pointed to by value 'ptr_val'.
void analyzer_t::process_memory_access(llvm::Instruction const & instr, llvm::Value const & ptr_val)
{
    tribool overflow = is_access_vulnerable(ptr_val);
    if (overflow)
        report_overflow(instr);
    else if (boost::logic::indeterminate(overflow))
        report_potential_overflow(instr);
}

/* ------------------------------------------------
 * LLVM wrappers
 * ------------------------------------------------
 */

analyzer_t::analyzer_t(bool report_indeterminate,
                       llvm::raw_ostream & res_out,
                       llvm::raw_ostream & warn_out,
                       llvm::raw_ostream & debug_out)
    : report_indeterminate_(report_indeterminate)
    , res_out_(res_out)
    , warn_out_(warn_out)
    , debug_out_(debug_out)
{
}

void analyzer_t::analyze_file(fs::path const & p)
{
    llvm::LLVMContext context;
    llvm::SMDiagnostic error;
    auto m = llvm::parseIRFile(p.string(), error, context);
    if (!m)
    {
        std::cerr << "Failed to parse module " << p.string() << std::endl;
        error.print(p.string().c_str(), llvm::errs());
        return;
    }

    analyze_module(*m);
}

void analyzer_t::analyze_module(llvm::Module const & module)
{
    debug_out_ << "Analyzing module "
               << module.getModuleIdentifier()
               << " corresponding to "
               << module.getSourceFileName()
               << "\n";

    for (auto const & f : module)
        analyze_function(f);
}

