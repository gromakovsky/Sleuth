#include "analyzer.h"
#include "analyzer/impl.h"

#include <string>

#include <llvm/ADT/StringRef.h>
#include <llvm/Pass.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Module.h>

void analyzer_t::report_overflow(llvm::Instruction const & instr,
                                 boost::optional<sym_range const &> idx_range,
                                 boost::optional<sym_range const &> size_range,
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
    auto print_maybe_range = [this](boost::optional<sym_range const &> range)
    {
        if (range)
            pimpl().res_out << *range;
        else
            pimpl().res_out << "<unknown>";
    };

    pimpl().res_out << " | overflow "
             << (sure ? "is possible" : "may be possible (but not surely)")
             << " in function "
             << func_name
             << ", instruction { "
             << instr
             << " }, index range: "
             ;
    print_maybe_range(idx_range);
    pimpl().res_out << ", size range: ";
    print_maybe_range(size_range);
    pimpl().res_out << "\n";
}

void analyzer_t::report_potential_overflow(llvm::Instruction const & instr,
                                           boost::optional<sym_range const &> idx_range,
                                           boost::optional<sym_range const &> size_range)
{
    report_overflow(instr, idx_range, size_range, false);
}
