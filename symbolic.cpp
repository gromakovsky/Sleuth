#include "symbolic.h"

#include <llvm/Support/raw_ostream.h>

namespace bl = boost::logic;

sym_expr::sym_expr(llvm::APInt val)
    : val_(val)
    , is_special_(boost::logic::indeterminate)
{
}

sym_expr & sym_expr::operator+=(sym_expr const & rhs)
{
    if (bl::indeterminate(rhs.is_special_) && bl::indeterminate(rhs.is_special_))
        val_ += rhs.val_;

    return *this;
}

sym_expr & sym_expr::operator-=(sym_expr const & rhs)
{
    if (bl::indeterminate(rhs.is_special_) && bl::indeterminate(rhs.is_special_))
        val_ -= rhs.val_;

    return *this;
}

bool sym_expr::operator<=(sym_expr const & rhs) const
{
    if (is_bot() || rhs.is_top())
        return true;

    if (is_top() || rhs.is_bot())
        return false;

    return val_.ule(rhs.val_);
}

bool sym_expr::is_top() const
{
    return is_special_ == true;
}

bool sym_expr::is_bot() const
{
    return is_special_ == false;
}

llvm::APInt const & sym_expr::val() const
{
    return val_;
}

sym_expr::sym_expr(bool is_special)
    : val_(llvm::APInt())
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

sym_range &sym_range::operator|=(sym_range const & rhs)
{
    lo = meet(lo, rhs.lo);
    hi = join(hi, rhs.hi);
    return *this;
}

sym_range &sym_range::operator&=(sym_range const & rhs)
{
    lo = join(lo, rhs.lo);
    hi = meet(hi, rhs.hi);
    return *this;
}

sym_range &sym_range::operator+=(sym_range const & rhs)
{
    lo += rhs.lo;
    hi += rhs.hi;
    return *this;
}

sym_range &sym_range::operator-=(sym_range const & rhs)
{
    lo -= rhs.hi;
    hi -= rhs.lo;
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

sym_range sym_range::full = { sym_expr::bot, sym_expr::top };

llvm::raw_ostream & operator<<(llvm::raw_ostream & out, sym_expr const & e)
{
    if (e.is_bot())
        return out << "bot";
    else if (e.is_top())
        return out << "top";
    else
        return out << e.val();
}

llvm::raw_ostream & operator<<(llvm::raw_ostream & out, sym_range const & r)
{
    return out << "[" << r.lo << ", " << r.hi << "]";
}
