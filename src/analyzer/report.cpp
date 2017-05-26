#include "analyzer.h"
#include "analyzer/impl.h"

#include <string>

#include <llvm/ADT/StringRef.h>
#include <llvm/Pass.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>

void analyzer_t::report_overflow(llvm::Instruction const & instr,
                                 sym_range const & idx_range, sym_range const & size_range,
                                 bool sure)
{
    if (sure)
        ++pimpl().total_overflows;
    else
        ++pimpl().total_indeterminate;

    if (!pimpl().report_indeterminate && !sure)
        return;

    instr.getDebugLoc().print(pimpl().res_out);
    llvm::Function const * f = instr.getFunction();
    auto func_name = f ? f->getName() : "<unknown>";
    pimpl().res_out << " | overflow "
             << (sure ? "is possible" : "may be possible (but not surely)")
             << " in function "
             << func_name
             << ", instruction { "
             << instr
             << " }, index range: "
             << idx_range
             << ", size range: "
             << size_range
             << "\n";
}

void analyzer_t::report_potential_overflow(llvm::Instruction const & instr,
                                           sym_range const & idx_range,
                                           sym_range const & size_range)
{
    report_overflow(instr, idx_range, size_range, false);
}
