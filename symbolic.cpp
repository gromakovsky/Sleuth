#include "symbolic.h"

namespace bl = boost::logic;

sym_expr::sym_expr(std::int64_t val)
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

    return val_ <= rhs.val_;
}

bool sym_expr::is_top() const
{
    return is_special_ == true;
}

bool sym_expr::is_bot() const
{
    return is_special_ == false;
}

sym_expr::sym_expr(bool is_special)
    : val_(0)
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
