#include "analyzer.h"
#include "gsa/builder.h"

struct analyzer_t::impl_t
{
    context_t ctx;
    bool report_indeterminate;
    llvm::raw_ostream & res_out;
    llvm::raw_ostream & warn_out;
    llvm::raw_ostream & debug_out;
    unsigned total_overflows;
    unsigned total_indeterminate;
    unsigned total_correct;
    gsa_builder_t gsa_builder;

    impl_t(bool report_indeterminate,
           llvm::raw_ostream & res_out,
           llvm::raw_ostream & warn_out = llvm::errs(),
           llvm::raw_ostream & debug_out = llvm::outs());

    ~impl_t();
};
