#pragma once

#include "atomic.h"

#include <boost/optional.hpp>
#include <boost/logic/tribool.hpp>

using boost::logic::tribool;

struct sym_expr
{
    explicit sym_expr(scalar_t);
    explicit sym_expr(sym_atomic_ptr const &);

    sym_expr operator-() const;
    sym_expr & operator+=(sym_expr const &);
    sym_expr & operator-=(sym_expr const &);
    sym_expr & operator*=(sym_expr const &);
    sym_expr & operator/=(sym_expr const &);

    bool operator<=(sym_expr const &) const;
    bool operator==(sym_expr const &) const;
    bool operator!=(sym_expr const &) const;

    static sym_expr top;
    static sym_expr bot;

    bool is_top() const;
    bool is_bot() const;

    void print(llvm::raw_ostream &) const;

    boost::optional<scalar_t> to_scalar() const;

    sym_atomic_ptr to_atom() const;

private:
    sym_atomic_ptr to_atom_no_delta() const;
private:
    // actual value is `coeff_ * atom_ + b` unless it's a special expression
    scalar_t coeff_;
    sym_atomic_ptr atom_;
    scalar_t delta_;

    sym_expr(bool);
    tribool is_special_;  // true for top, false for bot, indeterminate for usual value
};

sym_expr operator+(sym_expr const & a, sym_expr const & b);
sym_expr operator-(sym_expr const & a, sym_expr const & b);
sym_expr operator*(sym_expr const & a, sym_expr const & b);
sym_expr operator/(sym_expr const & a, sym_expr const & b);

sym_expr meet(sym_expr const & a, sym_expr const & b);
sym_expr join(sym_expr const & a, sym_expr const & b);

llvm::raw_ostream & operator<<(llvm::raw_ostream &, sym_expr const &);

sym_expr var_sym_expr(var_id const &);

