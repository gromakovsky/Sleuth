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
#include <llvm/Support/raw_ostream.h>

namespace fs = boost::filesystem;

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

    auto it = ctx_.val_ranges.find(v);
    if (it != ctx_.val_ranges.end())
        return it->second;

    ctx_.new_val_set.insert(v);
    ctx_.val_ranges.insert({v, sym_range::full});

    ctx_.val_ranges.insert({v, compute_def_range_internal(*v)});

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
        llvm::errs() << "Constant " << c.getName() << " doesn't have a type";
        return var_sym_range(&c);
    }

    if (auto i = dynamic_cast<llvm::ConstantInt const *>(&c))
    {
        llvm::APInt v = i->getValue();
        scalar_t scalar = v.getLimitedValue();  // TODO: not the best solution obviously
        sym_expr e(scalar);
        return {e, e};
    }

    llvm::outs() << "Can't compute def range of constant named \""
                 << c.getName()
                 << "\" with type \""
                 << *t << "\"";

    return var_sym_range(&c);
}

sym_range analyzer_t::compute_def_range_internal(llvm::Value const & v)
{
    return var_sym_range(&v);
}

// size is number of elements, not bytes
sym_range analyzer_t::compute_buffer_size_range(llvm::Value const & v)
{
    llvm::TargetLibraryInfoWrapperPass tliwp;
    llvm::TargetLibraryInfo const & tli = tliwp.getTLI();
    if (auto alloca = dynamic_cast<llvm::AllocaInst const *>(&v))
        return compute_use_range(alloca->getArraySize());
    else if (auto call = dynamic_cast<llvm::CallInst const *>(&v))
    {
        if (llvm::isAllocationFn(&v, &tli, true))
        {
            llvm::outs() << "Instruction is an allocation function call\n";
            auto res = compute_use_range(call->getArgOperand(0));
            llvm::outs() << "Allocated " << res << "\n";
            return res;
        }
    }

    return sym_range::full;
}

bool analyzer_t::can_access_ptr(llvm::Value const & v)
{
    auto cached = ctx_.vulnerability_info.find(&v);
    if (cached != ctx_.vulnerability_info.end())
        return cached->second;

    bool res = true;
    if (auto gep = dynamic_cast<llvm::GetElementPtrInst const *>(&v))
        res = can_access_ptr_gep(*gep);

    ctx_.vulnerability_info.insert({&v, res});
    return res;
}

bool analyzer_t::can_access_ptr_gep(llvm::GetElementPtrInst const & gep)
{
    auto source_type = gep.getSourceElementType();
    if (!source_type)
        llvm::errs() << "GEP instruction doesn't have source element type\n";

    llvm::outs() << "Processing GEP with source element type " << *source_type << "\n";
    llvm::Value const * pointer_operand = gep.getPointerOperand();
    if (!pointer_operand)
    {
        llvm::errs() << "GEP's pointer operand is null\n";
        return false;
    }

    sym_range buf_size = compute_buffer_size_range(*pointer_operand);
    llvm::outs() << "GEP's pointer operand's buffer size is in range " << buf_size << "\n";

    sym_range idx_range = compute_use_range(*gep.idx_begin());
    llvm::outs() << "GEP's base index in in range " << idx_range << "\n";

    if (check_overflow(buf_size, idx_range))
        return false;

    return true;
}

/* ------------------------------------------------
 * Reporting
 * ------------------------------------------------
 */

void analyzer_t::report_overflow(llvm::Instruction const & instr)
{
    instr.getDebugLoc().print(llvm::outs());
    llvm::Function const * f = instr.getFunction();
    auto func_name = f ? f->getName() : "<unknown>";
    llvm::outs() << " | overflow is possible in function "
                 << func_name
                 << ", instruction "
                 << instr.getOpcodeName()
                 << "\n";
}

/* ------------------------------------------------
 * Code processing
 * ------------------------------------------------
 */

void analyzer_t::analyze_function(llvm::Function const & f)
{
    llvm::outs() << "Analyzing function " << f.getName() << "\n";

    for (auto const & bb : f)
        analyze_basic_block(bb);
}

void analyzer_t::analyze_basic_block(llvm::BasicBlock const & bb)
{
    llvm::outs() << "Analyzing basic block " << bb.getName() << "\n";

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
        llvm::errs() << "load instruction doesn't have a pointer operand\n";
}

void analyzer_t::process_store(llvm::StoreInst const & store)
{
    if (auto pointer_operand = store.getPointerOperand())
        return process_memory_access(store, *pointer_operand);
    else
        llvm::errs() << "store instruction doesn't have a pointer operand\n";
}

// Process instruction 'instr' which accesses memory pointed to by value 'ptr_val'.
void analyzer_t::process_memory_access(llvm::Instruction const & instr, llvm::Value const & ptr_val)
{
    if (!can_access_ptr(ptr_val))
        report_overflow(instr);
}

/* ------------------------------------------------
 * LLVM wrappers
 * ------------------------------------------------
 */

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
    std::cout << "Analyzing module "
              << module.getModuleIdentifier()
              << " corresponding to "
              << module.getSourceFileName()
              << std::endl;

    for (auto const & f : module)
        analyze_function(f);
}

