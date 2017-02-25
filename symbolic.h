#pragma once

#include <cstdint>

#include <boost/logic/tribool.hpp>

using boost::logic::tribool;

struct sym_expr
{
    sym_expr(std::int64_t);

    sym_expr & operator+=(sym_expr const &);
    sym_expr & operator-=(sym_expr const &);

    bool operator<=(sym_expr const &) const;

    static sym_expr top;
    static sym_expr bot;

    bool is_top() const;
    bool is_bot() const;

private:
    sym_expr(bool);
    std::int64_t val_;
    tribool is_special_;
};

sym_expr operator+(sym_expr const & a, sym_expr const & b);
sym_expr operator-(sym_expr const & a, sym_expr const & b);

sym_expr meet(sym_expr const & a, sym_expr const & b);
sym_expr join(sym_expr const & a, sym_expr const & b);

struct sym_range
{
    sym_expr lo;
    sym_expr hi;

    sym_range & operator|=(sym_range const &);  // union
    sym_range & operator&=(sym_range const &);  // intersection

    sym_range & operator+=(sym_range const &);
    sym_range & operator-=(sym_range const &);

    static sym_range full;
};

// union
sym_range operator|(sym_range const & a, sym_range const & b);
// intersection
sym_range operator&(sym_range const & a, sym_range const & b);

sym_range operator+(sym_range const & a, sym_range const & b);
sym_range operator-(sym_range const & a, sym_range const & b);
