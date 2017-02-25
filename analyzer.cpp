#include "analyzer.h"

#include <iostream>

#include <llvm/IR/LLVMContext.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/IR/Instructions.h>

#include "context.h"
#include "symbolic.h"

namespace fs = boost::filesystem;

sym_range compute_def_range(var_id const & v, context_t & ctx)
{
    auto it = ctx.def_ranges.find(v);
    if (it != ctx.def_ranges.end())
        return it->second;

    ctx.def_ranges.insert({v, sym_range::full});
    return sym_range::full;
}

void process_alloca(llvm::AllocaInst const & alloca, context_t & ctx)
{
    std::cout << "ы" << std::endl;
}

void process_instruction(llvm::Instruction const & instr, context_t & ctx)
{
    if (auto alloca = dynamic_cast<llvm::AllocaInst const *>(&instr))
        process_alloca(*alloca, ctx);
}

void analyze_basic_block(llvm::BasicBlock const & bb, context_t & ctx)
{
    std::cout << "Analyzing basic block " << bb.getName().str() << std::endl;

    for (auto const & i : bb)
        process_instruction(i, ctx);
}

void analyze_function(llvm::Function const & f, context_t & ctx)
{
    std::cout << "Analyzing function " << f.getName().str() << std::endl;

    for (auto const & bb : f)
        analyze_basic_block(bb, ctx);
}

void analyze_module(llvm::Module const & module, context_t & ctx)
{
    std::cout << "Analyzing module "
              << module.getModuleIdentifier()
              << " corresponding to "
              << module.getSourceFileName()
              << std::endl;

    for (auto const & f : module)
        analyze_function(f, ctx);
}

void analyze_file(fs::path const & p)
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
    analyze_module(*m, analyzer_ctx);
}
