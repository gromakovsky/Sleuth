#include "analyzer/impl.h"

#include <iostream>

#include <boost/logic/tribool.hpp>
#include <boost/filesystem.hpp>

#include <llvm/ADT/StringRef.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>

namespace fs = boost::filesystem;

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
    pimpl().res_out << "Total number of possible overflows: " << pimpl().total_overflows
             << ", total number of indeterminate cases: " << pimpl().total_indeterminate
             << ", total number of correct memory usages: " << pimpl().total_correct
             << "\n";
}

void analyzer_t::analyze_module(llvm::Module const & module)
{
    pimpl().debug_out << "Analyzing module "
               << module.getModuleIdentifier()
               << " corresponding to "
               << module.getSourceFileName()
               << "\n";

    pimpl().gsa_builder.build(module);
    for (auto const & f : module)
        analyze_function(f);
}

void analyzer_t::analyze_function(llvm::Function const & f)
{
    pimpl().debug_out << "Analyzing function " << f.getName() << "\n";

    for (auto const & bb : f)
        analyze_basic_block(bb);
}

void analyzer_t::analyze_basic_block(llvm::BasicBlock const & bb)
{
    for (auto const & i : bb)
        process_instruction(i);
}
