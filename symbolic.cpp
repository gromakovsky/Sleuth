#include "symbolic.h"

#include <llvm/Support/raw_ostream.h>

namespace bl = boost::logic;

llvm::raw_ostream & operator<<(llvm::raw_ostream & out, sym_atomic const & a)
{
    a.print(out);
    return out;
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
    }
}

bool atomic_bin_op::operator==(sym_atomic const & rhs) const
{
    if (auto bin_op = dynamic_cast<atomic_bin_op const *>(&rhs))
        return operation_ == bin_op->operation_
                && *lhs_ == *bin_op->lhs_
                && *rhs_ == *bin_op->rhs_;

    return false;

}

/* ------------------------------------------------
 * Symbolic expression
 * ------------------------------------------------
 */

sym_expr::sym_expr(scalar_t scalar)
    : coeff_(0)
    , atom_(nullptr)
    , delta_(scalar)
    , is_special_(boost::logic::indeterminate)
{
}

sym_expr::sym_expr(sym_atomic_ptr const & atom)
    : coeff_(1)
    , atom_(atom)
    , delta_(0)
    , is_special_(boost::logic::indeterminate)
{
}

sym_expr sym_expr::operator-() const
{
    if (is_top())
        return sym_expr::bot;
    else if (is_bot())
        return sym_expr::top;

    sym_expr res(-delta_);
    res.atom_ = atom_;
    res.coeff_ = -coeff_;
    return res;
}

sym_expr & sym_expr::operator+=(sym_expr const & rhs)
{
    if (!bl::indeterminate(is_special_))
        return *this;

    if (!bl::indeterminate(rhs.is_special_))
    {
        is_special_ = rhs.is_special_;
        atom_.reset();
        return *this;
    }

    delta_ += rhs.delta_;
    if (rhs.coeff_ == 0)
        return *this;

    if (coeff_ == 0)
    {
        coeff_ = rhs.coeff_;
        atom_ = rhs.atom_;
    }
    else if (coeff_ == -rhs.coeff_)
    {
        coeff_ = 0;
        atom_.reset();
    }
    else if (*atom_ == *rhs.atom_)
    {
        coeff_ += rhs.coeff_;
    }
    else
    {
        sym_atomic_ptr plus_lhs = to_atom_no_delta();
        sym_atomic_ptr plus_rhs = rhs.to_atom_no_delta();
        coeff_ = 1;
        atom_ = std::make_shared<atomic_bin_op>(plus_lhs, plus_rhs, atomic_bin_op::Plus);
    }

    return *this;
}

sym_expr & sym_expr::operator-=(sym_expr const & rhs)
{
    return *this += -rhs;
}

sym_expr & sym_expr::operator*=(sym_expr const & rhs)
{
    if (!bl::indeterminate(rhs.is_special_))
        return *this;

    // strange case actually
    if (!bl::indeterminate(rhs.is_special_))
    {
        is_special_ = rhs.is_special_;
        atom_.reset();
        return *this;
    }

    // suppose we are computing `(ax + b) * (cy + d)`
    scalar_t a = coeff_;
    scalar_t b = delta_;
    scalar_t c = rhs.coeff_;
    scalar_t d = rhs.delta_;

    delta_ = b * d;
    // now value is `ax + bd`

    if (a == 0 && c == 0)
    {
        // in this case `ax + bd = bd = (ax + b) * (cy + d)`
    }
    else if (a == 0)
    {
        // in this case result should be `bcy + bd`
        coeff_ = b * c;
        if (coeff_)
            atom_ = rhs.atom_;
        else
            atom_.reset();;
    }
    else if (c == 0)
    {
        // in this case result should be `adx + bd`
        coeff_ *= d;
        if (!coeff_)
            atom_.reset();
    }
    else
    {
        sym_atomic_ptr x(std::move(atom_));
        atom_ = std::make_shared<atomic_bin_op>(x, rhs.atom_,  atomic_bin_op::Mult);
        coeff_ *= c;
        // at this point value is `acxy + bd`
        // we also know that `a ≠ 0` and `c ≠ 0`

        if (b != 0)
        {
            sym_expr e(rhs.atom_);
            e.coeff_ = b * c;
            *this += e;
        }

        if (d != 0)
        {
            sym_expr e(x);
            e.coeff_ = a * d;
            *this += e;
        }
    }

    return *this;
}

bool sym_expr::operator<=(sym_expr const & rhs) const
{
    if (is_bot() || rhs.is_top())
        return true;

    if (is_top() || rhs.is_bot())
        return false;

    sym_expr diff = *this - rhs;
    if (diff.coeff_ == 0 && diff.delta_ <= 0)
        return true;

    return false;
}

bool sym_expr::is_top() const
{
    return is_special_ == true;
}

bool sym_expr::is_bot() const
{
    return is_special_ == false;
}

void sym_expr::print(llvm::raw_ostream & out) const
{
    if (is_bot())
        out << "bot";
    else if (is_top())
        out << "top";
    else if (!coeff_)
        out << delta_;
    else if (!delta_)
        out << coeff_ << " * " << *atom_;
    else
        out << coeff_ << " * " << *atom_ << " + " << delta_;
}

sym_atomic_ptr sym_expr::to_atom_no_delta() const
{
    return coeff_ == 1 ? atom_ : std::make_shared<atomic_linear>(atom_, coeff_);
}

sym_expr::sym_expr(bool is_special)
    : coeff_(0)
    , delta_(0)
    , is_special_(is_special)
{
}

sym_expr sym_expr::top = sym_expr(true);
sym_expr sym_expr::bot = sym_expr(false);

sym_expr operator+(sym_expr const & a, sym_expr const & b)
{
    sym_expr res(a);
    res += b;
    return res;
}

sym_expr operator-(sym_expr const & a, sym_expr const & b)
{
    sym_expr res(a);
    res -= b;
    return res;
}

sym_expr operator*(sym_expr const & a, sym_expr const & b)
{
    sym_expr res(a);
    res *= b;
    return res;
}

sym_expr meet(sym_expr const & a, sym_expr const & b)
{
    if (a <= b)
        return a;
    else if (b <= a)
        return b;
    else
        return sym_expr::bot;
}

sym_expr join(sym_expr const & a, sym_expr const & b)
{
    if (a <= b)
        return b;
    else if (b <= a)
        return a;
    else
        return sym_expr::top;
}

llvm::raw_ostream & operator<<(llvm::raw_ostream & out, sym_expr const & e)
{
    e.print(out);
    return out;
}

sym_expr var_sym_expr(var_id const & v)
{
    return sym_expr(std::make_shared<atomic_var>(v));
}

/* ------------------------------------------------
 * Symbolic range
 * ------------------------------------------------
 */

sym_range & sym_range::operator|=(sym_range const & rhs)
{
    lo = meet(lo, rhs.lo);
    hi = join(hi, rhs.hi);
    return *this;
}

sym_range & sym_range::operator&=(sym_range const & rhs)
{
    lo = join(lo, rhs.lo);
    hi = meet(hi, rhs.hi);
    return *this;
}

sym_range & sym_range::operator+=(sym_range const & rhs)
{
    lo += rhs.lo;
    hi += rhs.hi;
    return *this;
}

sym_range & sym_range::operator-=(sym_range const & rhs)
{
    lo -= rhs.hi;
    hi -= rhs.lo;
    return *this;
}

sym_range & sym_range::operator*=(sym_expr const & e)
{
    sym_range tmp = { hi * e, lo * e };
    lo *= e;
    hi *= e;
    *this |= tmp;
    return *this;
}

sym_range & sym_range::operator*=(sym_range const & rhs)
{
    sym_range tmp = *this * rhs.hi;
    *this *= rhs.lo;
    *this |= tmp;
    return *this;
}

sym_range operator|(sym_range const & a, sym_range const & b)
{
    sym_range res = a;
    res |= b;
    return res;
}

sym_range operator&(sym_range const & a, sym_range const & b)
{
    sym_range res = a;
    res &= b;
    return res;
}

sym_range operator+(sym_range const & a, sym_range const & b)
{
    sym_range res = a;
    res += b;
    return res;
}

sym_range operator-(sym_range const & a, sym_range const & b)
{
    sym_range res = a;
    res -= b;
    return res;
}

sym_range operator*(sym_range const & a, sym_expr const & b)
{
    sym_range res = a;
    res *= b;
    return res;
}

sym_range operator*(sym_range const & a, sym_range const & b)
{
    sym_range res = a;
    res *= b;
    return res;
}

sym_range sym_range::full = { sym_expr::bot, sym_expr::top };

llvm::raw_ostream & operator<<(llvm::raw_ostream & out, sym_range const & r)
{
    return out << "[" << r.lo << ", " << r.hi << "]";
}

sym_range var_sym_range(var_id const & v)
{
    sym_expr e = var_sym_expr(v);
    return {e, e};
}
