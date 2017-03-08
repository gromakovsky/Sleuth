#pragma once

#include <memory>

#include <boost/logic/tribool.hpp>

#include <llvm/ADT/APInt.h>

#include "common.h"

using boost::logic::tribool;
using scalar_t = std::int64_t;

struct sym_atomic
{
    virtual void print(llvm::raw_ostream &) const = 0;
    virtual bool operator==(sym_atomic const &) const = 0;
};

using sym_atomic_ptr = std::shared_ptr<sym_atomic>;

llvm::raw_ostream & operator<<(llvm::raw_ostream &, sym_atomic const &);

struct atomic_const : sym_atomic
{
    atomic_const(scalar_t);

    virtual void print(llvm::raw_ostream &) const override;
    virtual bool operator==(sym_atomic const &) const override;

private:
    scalar_t val_;
};

struct atomic_var : sym_atomic
{
    atomic_var(var_id);

    virtual void print(llvm::raw_ostream &) const override;
    virtual bool operator==(sym_atomic const &) const override;

private:
    var_id var_;
};

struct atomic_linear : sym_atomic
{
    atomic_linear(sym_atomic_ptr const &, scalar_t);

    virtual void print(llvm::raw_ostream &) const override;
    virtual bool operator==(sym_atomic const &) const override;

private:
    sym_atomic_ptr atom_;
    scalar_t coeff_;
};

struct atomic_bin_op : sym_atomic
{
    enum op_t {
        Plus,
        Minus,
        Mult,
        Div,
    };

    atomic_bin_op(sym_atomic_ptr const & lhs, sym_atomic_ptr const & rhs, op_t);

    virtual void print(llvm::raw_ostream &) const override;
    virtual bool operator==(sym_atomic const &) const override;

private:
    sym_atomic_ptr lhs_, rhs_;
    op_t operation_;
};

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

private:
    sym_atomic_ptr to_atom_no_delta() const;
    sym_atomic_ptr to_atom() const;

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

struct sym_range
{
    sym_expr lo;
    sym_expr hi;

    sym_range & operator|=(sym_range const &);  // union
    sym_range & operator&=(sym_range const &);  // intersection

    sym_range & operator+=(sym_range const &);
    sym_range & operator-=(sym_range const &);
    sym_range & operator*=(sym_expr const &);
    sym_range & operator*=(sym_range const &);
    sym_range & operator/=(sym_expr const &);
    sym_range & operator/=(sym_range const &);

    static sym_range full;
};

sym_range operator|(sym_range const & a, sym_range const & b);
sym_range operator&(sym_range const & a, sym_range const & b);

sym_range operator+(sym_range const & a, sym_range const & b);
sym_range operator-(sym_range const & a, sym_range const & b);
sym_range operator*(sym_range const & a, sym_expr const & b);
sym_range operator*(sym_expr const & a, sym_range const & b);
sym_range operator*(sym_range const & a, sym_range const & b);
sym_range operator/(sym_range const & a, sym_expr const & b);
sym_range operator/(sym_range const & a, sym_range const & b);

bool operator==(sym_range const & a, sym_range const & b);
bool operator!=(sym_range const & a, sym_range const & b);

llvm::raw_ostream & operator<<(llvm::raw_ostream &, sym_range const &);

sym_range var_sym_range(var_id const &);
