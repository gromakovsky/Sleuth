#include "analyzer.h"

#include <iostream>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/Instructions.h>

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
    auto it = ctx_.def_ranges.find(v);
    if (it != ctx_.def_ranges.end())
        return it->second;

    ctx_.def_ranges.insert({v, sym_range::full});
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
