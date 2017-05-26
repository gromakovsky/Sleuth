#include "impl.h"

analyzer_t::impl_t & analyzer_t::pimpl()
{
    return *pimpl_;
}

analyzer_t::impl_t const & analyzer_t::pimpl() const
{
    return *pimpl_;
}

analyzer_t::impl_t::impl_t(bool report_indeterminate,
                           llvm::raw_ostream & res_out,
                           llvm::raw_ostream & warn_out,
                           llvm::raw_ostream & debug_out)
    : report_indeterminate(report_indeterminate)
    , res_out(res_out)
    , warn_out(warn_out)
    , debug_out(debug_out)
    , total_overflows(0)
    , total_indeterminate(0)
    , total_correct(0)
{

}
