#pragma once

#include <boost/filesystem.hpp>

#include <llvm/IR/Module.h>
#include <llvm/IR/Constant.h>

#include "context.h"
#include "symbolic.h"

struct analyzer_t
{
    void analyze_file(boost::filesystem::path const &);

private:
    void analyze_module(llvm::Module const &);
    void analyze_function(llvm::Function const &);
    void analyze_basic_block(llvm::BasicBlock const &);
    void process_instruction(llvm::Instruction const &);

    sym_range compute_def_range(var_id const &);
    sym_range compute_use_range(var_id const &, void * = nullptr);
    void update_def_range(var_id const &);

    sym_range compute_def_range_const(llvm::Constant const &);
    sym_range compute_def_range_internal(llvm::Value const &);

private:
    context_t ctx_;
};
