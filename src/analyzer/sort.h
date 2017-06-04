#pragma once

#include <vector>

#include <llvm/IR/Function.h>

using func_vector = std::vector<llvm::Function const *>;

// Sort functions in topological order.
// Callee goes before caller.
func_vector sort_functions(func_vector const &);
