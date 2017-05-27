#include "atomic.h"

#include <llvm/Support/raw_ostream.h>

llvm::raw_ostream & operator<<(llvm::raw_ostream & out, sym_atomic const & a)
{
    a.print(out);
    return out;
}

atomic_const::atomic_const(scalar_t val)
    : val_(val)
{
}

void atomic_const::print(llvm::raw_ostream & out) const
{
    out << val_;
}

bool atomic_const::operator==(sym_atomic const & rhs) const
{
    if (auto c = dynamic_cast<atomic_const const *>(&rhs))
        return c->val_ == val_;

    return false;
}

scalar_t atomic_const::value() const
{
    return val_;
}

atomic_var::atomic_var(var_id var)
    : var_(var)
{
}

void atomic_var::print(llvm::raw_ostream & out) const
{
    if (!var_)
    {
        out << "null";
        return;
    }

    var_->print(out);
}

bool atomic_var::operator==(sym_atomic const & rhs) const
{
    if (auto var = dynamic_cast<atomic_var const *>(&rhs))
        return var->var_ == var_;

    return false;
}

var_id atomic_var::var() const
{
    return var_;
}

atomic_linear::atomic_linear(sym_atomic_ptr const & atom, scalar_t k)
    : atom_(atom)
    , coeff_(k)
{
}

void atomic_linear::print(llvm::raw_ostream & out) const
{
    out << coeff_ << " * " << *atom_;
}

bool atomic_linear::operator==(sym_atomic const & rhs) const
{
    if (auto lin = dynamic_cast<atomic_linear const *>(&rhs))
        return coeff_ == lin->coeff_
                && *atom_ == *lin->atom_;

    return false;
}

sym_atomic_ptr const & atomic_linear::atom() const
{
    return atom_;
}

scalar_t atomic_linear::coeff() const
{
    return coeff_;
}

atomic_bin_op::atomic_bin_op(sym_atomic_ptr const & lhs, sym_atomic_ptr const & rhs,
                             atomic_bin_op::op_t operation)
    : lhs_(lhs)
    , rhs_(rhs)
    , operation_(operation)
{
}

void atomic_bin_op::print(llvm::raw_ostream & out) const
{
    out << *lhs_ << " ";
    switch (operation_)
    {
    case Plus: out << "+"; break;
    case Minus: out << "-"; break;
    case Mult: out << "*"; break;
    case Div: out << "/"; break;
    }
    out << *rhs_;
}

bool atomic_bin_op::operator==(sym_atomic const & rhs) const
{
    if (auto bin_op = dynamic_cast<atomic_bin_op const *>(&rhs))
        return operation_ == bin_op->operation_
                && *lhs_ == *bin_op->lhs_
                && *rhs_ == *bin_op->rhs_;

    return false;

}

sym_atomic_ptr const & atomic_bin_op::lhs() const
{
    return lhs_;
}

sym_atomic_ptr const & atomic_bin_op::rhs() const
{
    return rhs_;
}

