#include "builder.h"

#include <memory>
#include <vector>
#include <unordered_map>

using gating_cond_ptr_t = std::unique_ptr<gating_cond_t>;

struct gsa_builder_t::impl_t
{
    std::unordered_map<llvm::PHINode const *, std::vector<gating_cond_ptr_t>> conditions;
};

gsa_builder_t::gsa_builder_t()
{
}

gsa_builder_t::~gsa_builder_t()
{
}

void gsa_builder_t::build(llvm::Module const &)
{
    // TODO
}

gating_cond_t const * gsa_builder_t::get_gating_condition(llvm::PHINode const & phi, unsigned i) const
{
    auto const & conditions = pimpl().conditions;
    auto it = conditions.find(&phi);
    if (it == conditions.end())
        return nullptr;
    else if (it->second.size() > i)
        return it->second[i].get();
    else
        return nullptr;
}

gsa_builder_t::impl_t & gsa_builder_t::pimpl()
{
    return *pimpl_;
}

gsa_builder_t::impl_t const & gsa_builder_t::pimpl() const
{
    return *pimpl_;
}
