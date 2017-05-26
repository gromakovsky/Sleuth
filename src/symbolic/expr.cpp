#include <llvm/Support/raw_ostream.h>

#include "expr.h"

namespace bl = boost::logic;

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
    // bad case so nothing smart
    if (!bl::indeterminate(rhs.is_special_) || !bl::indeterminate(is_special_))
        return *this;

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

sym_expr & sym_expr::operator/=(sym_expr const & rhs)
{
    // bad case so nothing smart
    if (!bl::indeterminate(rhs.is_special_) || !bl::indeterminate(is_special_))
        return *this;

    // suppose we are computing `(ax + b) * (cy + d)`
    scalar_t a = coeff_;
    scalar_t b = delta_;
    scalar_t c = rhs.coeff_;
    scalar_t d = rhs.delta_;

    if (c == 0)
    {
        coeff_ /= d;  // caller is responsible for `rhs ≠ 0`
        delta_ /= d;
    }
    else
    {
        // value should be `(ax + b) / (cy + d) + 0`
        delta_ = 0;
        coeff_ = 1;
        atom_ = std::make_shared<atomic_bin_op>(to_atom(), rhs.to_atom(), atomic_bin_op::Div);
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
    if (diff.coeff_ == 0 && delta_ <= rhs.delta_)
        return true;

    return false;
}

bool sym_expr::operator==(sym_expr const & rhs) const
{
    if (is_bot()) return rhs.is_bot();
    if (is_top()) return rhs.is_top();

    if (!bl::indeterminate(rhs.is_special_))
        return false;

    if (delta_ != rhs.delta_ || coeff_ != rhs.coeff_)
        return false;
    if (!atom_ && !rhs.atom_)
        return true;
    if (!!atom_ ^ !!rhs.atom_)
        return false;

    return *atom_ == *rhs.atom_;
}

bool sym_expr::operator!=(sym_expr const & rhs) const
{
    return !(*this == rhs);
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
    else {
        if (coeff_)
        {
            if (coeff_ != 1)
                out << coeff_ << " * ";

            out << *atom_;
            if (delta_)
                out << " + ";
        }

        if (delta_ || !coeff_)
            out << delta_;
    }
}

boost::optional<scalar_t> sym_expr::to_scalar() const
{
    if (!coeff_)
        return delta_;

    return boost::none;
}

sym_atomic_ptr sym_expr::to_atom_no_delta() const
{
    return coeff_ == 1 ? atom_ : std::make_shared<atomic_linear>(atom_, coeff_);
}

sym_atomic_ptr sym_expr::to_atom() const
{
    sym_atomic_ptr no_delta_atom = to_atom_no_delta();
    if (delta_ == 0)
        return no_delta_atom;

    auto delta_atom = std::make_shared<atomic_const>(delta_);
    return std::make_shared<atomic_bin_op>(no_delta_atom, delta_atom, atomic_bin_op::Plus);
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

sym_expr operator/(sym_expr const & a, sym_expr const & b)
{
    sym_expr res(a);
    res /= b;
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

