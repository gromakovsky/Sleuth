#include "common.h"

#include <llvm/IR/InstrTypes.h>

scalar_t extract_const(llvm::ConstantInt const & i)
{
    // TODO: not the best solution obviously
    llvm::APInt v = i.getValue();
    bool is_neg = v.isNegative();
    if (is_neg)
        v.flipAllBits();

    scalar_t res = v.getLimitedValue();
    if (is_neg)
        res = -res - 1;

    return res;
}

boost::optional<scalar_t> extract_const_maybe(llvm::Value const * v)
{
    if (auto i = dynamic_cast<llvm::ConstantInt const *>(v))
        return extract_const(*i);

    return boost::none;
}

monotony_t does_monotonically_depend(var_id dependent, var_id x)
{
    // currently we only consuder binary operators
    if (auto bin_op = dynamic_cast<llvm::BinaryOperator const *>(dependent))
    {
        var_id op0 = bin_op->getOperand(0), op1 = bin_op->getOperand(1);
        if (op0 != x && op1 != x)
            return MONOTONY_NO;

        bool x_is0 = op0 == x;
        if (auto scalar = extract_const_maybe(x_is0 ? op1 : op0))
        {
            // actually we consider only addition and subtraction
            switch (bin_op->getOpcode())
            {
            case llvm::BinaryOperator::Add:
                return *scalar > 0 ? MONOTONY_INC : MONOTONY_DEC;
            case llvm::BinaryOperator::Sub:
                if (x_is0)
                    return *scalar > 0 ? MONOTONY_DEC : MONOTONY_INC;
                else
                    break;
            default: break;
            }
        }

    }

    return MONOTONY_NO;
}
