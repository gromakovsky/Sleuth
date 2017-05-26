#include "expr.h"
#include "range.h"

#include <llvm/Support/raw_ostream.h>

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

sym_range & sym_range::operator/=(sym_expr const & e)
{
    sym_range tmp = { hi / e, lo / e };
    lo /= e;
    hi /= e;
    *this |= tmp;
    return *this;
}

sym_range & sym_range::operator/=(sym_range const & rhs)
{
    if (rhs.hi <= sym_expr(scalar_t(-1)) || sym_expr(scalar_t(1)) <= rhs.lo)
    {
        sym_range tmp = *this / rhs.hi;
        *this /= rhs.lo;
        *this |= tmp;
    }
    else
    {
        lo = sym_expr::bot;
        hi = sym_expr::top;
    }

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

sym_range operator*(sym_expr const & a, sym_range const & b)
{
    return b * a;
}

sym_range operator*(sym_range const & a, sym_range const & b)
{
    sym_range res = a;
    res *= b;
    return res;
}

sym_range operator/(sym_range const & a, sym_expr const & b)
{
    sym_range res = a;
    res /= b;
    return res;
}

sym_range operator/(sym_range const & a, sym_range const & b)
{
    sym_range res = a;
    res /= b;
    return res;
}

sym_range sym_range::full = { sym_expr::bot, sym_expr::top };
sym_range sym_range::empty = { sym_expr::top, sym_expr::bot };

bool operator==(sym_range const & a, sym_range const & b)
{
    return a.lo == b.lo && a.hi == b.hi;
}

bool operator!=(sym_range const & a, sym_range const & b)
{
    return !(a == b);
}

llvm::raw_ostream & operator<<(llvm::raw_ostream & out, sym_range const & r)
{
    return out << "[" << r.lo << "; " << r.hi << "]";
}

sym_range const_sym_range(scalar_t v)
{
    sym_expr e(v);
    return {e, e};
}

sym_range var_sym_range(var_id const & v)
{
    sym_expr e = var_sym_expr(v);
    return {e, e};
}

boost::optional<scalar_range> to_scalar_range(sym_range const & r)
{
    auto lo = r.lo.to_scalar();
    auto hi = r.hi.to_scalar();
    if (lo && hi)
        return std::make_pair(*lo, *hi);

    return boost::none;
}
