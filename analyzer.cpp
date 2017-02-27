#include "analyzer.h"

#include <iostream>

#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/raw_ostream.h>

namespace fs = boost::filesystem;

/* ------------------------------------------------
 * Core logic
 * ------------------------------------------------
 */

sym_range analyzer_t::compute_use_range(var_id const & v, void *)
{
    return compute_def_range(v);
}

sym_range analyzer_t::compute_def_range(var_id const & v)
{
    if (auto const_v = dynamic_cast<llvm::Constant*>(v))
        return compute_def_range_const(*const_v);

    auto it = ctx_.def_ranges.find(v);
    if (it != ctx_.def_ranges.end())
        return it->second;

    ctx_.new_val_set.insert(v);
    ctx_.def_ranges.insert({v, sym_range::full});

    ctx_.def_ranges.insert({v, compute_def_range_internal(*v)});

    update_def_range(v);
    ctx_.new_val_set.erase(v);

    auto res_it = ctx_.def_ranges.find(v);
    return res_it == ctx_.def_ranges.end() ? sym_range::full : res_it->second;
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
        return sym_range::full;
    }

    if (auto i = dynamic_cast<llvm::ConstantInt const *>(&c))
    {
        llvm::APInt v = i->getValue();
        sym_expr e(v);
        return {e, e};
    }

    llvm::errs() << "Can't compute def range of constant named \""
                 << c.getName()
                 << "\" with type \""
                 << *t << "\"";

    return sym_range::full;
}

sym_range analyzer_t::compute_def_range_internal(llvm::Value const &)
{
    return sym_range::full;
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
        std::cerr << "Failed to parse module" << std::endl;
        error.print(p.string().c_str(), llvm::errs());
    }

    context_t analyzer_ctx;
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

void analyzer_t::analyze_function(llvm::Function const & f)
{
    std::cout << "Analyzing function " << f.getName().str() << std::endl;

    for (auto const & bb : f)
        analyze_basic_block(bb);
}

void analyzer_t::analyze_basic_block(llvm::BasicBlock const & bb)
{
    std::cout << "Analyzing basic block " << bb.getName().str() << std::endl;

    for (auto const & i : bb)
        process_instruction(i);
}

void analyzer_t::process_instruction(llvm::Instruction const & instr)
{
}
