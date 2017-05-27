#pragma once

#include "symbolic/expr.h"

#include <llvm/IR/Instruction.h>

// Trigger is a representation of a condition which leads to buffer overflow.
// It also stores an instruction where this overflow may happen.
//
// The condition is that `lhs <= rhs`.
struct trigger_t
{
    sym_expr lhs, rhs;
    llvm::Instruction const * instr;

    trigger_t(sym_expr lhs, sym_expr rhs, llvm::Instruction const & instr);
};
