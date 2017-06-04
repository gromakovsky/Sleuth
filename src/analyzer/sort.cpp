#include "sort.h"

#include <functional>
#include <unordered_set>

#include <llvm/IR/Instructions.h>

using f_ptr = llvm::Function const *;

namespace {

std::unordered_set<f_ptr> get_called_functions(f_ptr func)
{
    std::unordered_set<f_ptr> res;
    for (auto const & bb : *func)
    {
        for (auto const & instr : bb)
        {
            if (auto call = dynamic_cast<llvm::CallInst const *>(&instr))
            {
                res.insert(call->getCalledFunction());
            }
        }
    }

    return res;
}

}

std::vector<f_ptr> sort_functions(std::vector<f_ptr> const & functions)
{
    std::vector<f_ptr> res;
    std::unordered_set<f_ptr> visited;
    std::function<void(f_ptr)> dfs = [&dfs, &visited, &res](f_ptr f)
    {
        if (visited.count(f))
            return;

        visited.insert(f);
        for (f_ptr called : get_called_functions(f))
        {
            dfs(called);
        }

        res.push_back(f);
    };

    for (f_ptr f : functions)
    {
        dfs(f);
    }

    return res;
}
