#include "trigger.h"

trigger_t::trigger_t(sym_expr lhs, sym_expr rhs, llvm::Instruction const & instr)
    : lhs(lhs)
    , rhs(rhs)
    , instr(&instr)
{
}
