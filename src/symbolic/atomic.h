#pragma once

#include <memory>

#include <llvm/ADT/APInt.h>

#include "common.h"

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

    scalar_t value() const;

private:
    scalar_t val_;
};

struct atomic_var : sym_atomic
{
    atomic_var(var_id);

    virtual void print(llvm::raw_ostream &) const override;
    virtual bool operator==(sym_atomic const &) const override;

    var_id var() const;

private:
    var_id var_;
};

struct atomic_linear : sym_atomic
{
    atomic_linear(sym_atomic_ptr const &, scalar_t);

    virtual void print(llvm::raw_ostream &) const override;
    virtual bool operator==(sym_atomic const &) const override;

    sym_atomic_ptr const & atom() const;
    scalar_t coeff() const;

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

    sym_atomic_ptr const & lhs() const;
    sym_atomic_ptr const & rhs() const;

private:
    sym_atomic_ptr lhs_, rhs_;
    op_t operation_;
};

