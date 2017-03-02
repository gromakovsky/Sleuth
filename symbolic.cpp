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

bool sym_expr::ule(sym_expr const & rhs) const
{
    if (is_bot() || rhs.is_top())
        return true;

    if (is_top() || rhs.is_bot())
        return false;

    unsigned w = std::max(val_.getBitWidth(), rhs.val().getBitWidth());
    if (val_.getBitWidth() < w)
    {
        auto new_val = val_.zext(w);
        return new_val.ule(rhs.val());
    }
    else if (rhs.val().getBitWidth() < w)
    {
        auto new_val = rhs.val().zext(w);
        return val_.ule(new_val);
    }

    return val_.ule(rhs.val_);
}

bool sym_expr::sle(sym_expr const & rhs) const
{
    if (is_bot() || rhs.is_top())
        return true;

    if (is_top() || rhs.is_bot())
        return false;

    unsigned w = std::max(val_.getBitWidth(), rhs.val().getBitWidth());
    if (val_.getBitWidth() < w)
    {
        auto new_val = val_.sext(w);
        return new_val.ule(rhs.val());
    }
    else if (rhs.val().getBitWidth() < w)
    {
        auto new_val = rhs.val().sext(w);
        return val_.ule(new_val);
    }
    return val_.sle(rhs.val_);
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

sym_expr meet(sym_expr const & a, sym_expr const & b, bool signed_)
{
    auto le = signed_
            ? [](sym_expr const & s1, sym_expr const & s2) { return s1.sle(s2); }
            : [](sym_expr const & s1, sym_expr const & s2) { return s1.ule(s2); };

    if (le(a, b))
        return a;
    else if (le(b, a))
        return b;
    else
        return sym_expr::bot;
}

sym_expr join(sym_expr const & a, sym_expr const & b, bool signed_)
{
    auto le = signed_
            ? [](sym_expr const & s1, sym_expr const & s2) { return s1.sle(s2); }
            : [](sym_expr const & s1, sym_expr const & s2) { return s1.ule(s2); };

    if (le(a, b))
        return b;
    else if (le(b, a))
        return a;
    else
        return sym_expr::top;
}

sym_range &sym_range::operator|=(sym_range const & rhs)
{
    // TODO: what about sign?
    lo = meet(lo, rhs.lo, true);
    hi = join(hi, rhs.hi, true);
    return *this;
}

sym_range &sym_range::operator&=(sym_range const & rhs)
{
    // TODO: what about sign?
    lo = join(lo, rhs.lo, true);
    hi = meet(hi, rhs.hi, true);
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
