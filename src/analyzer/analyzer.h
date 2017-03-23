#pragma once

#include <boost/filesystem.hpp>
#include <boost/logic/tribool.hpp>

#include <llvm/IR/Constant.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/raw_ostream.h>

#include "context.h"
#include "symbolic.h"

using program_point_t = llvm::Instruction const *;

struct analyzer_t
{
    analyzer_t(bool report_indeterminate,
               llvm::raw_ostream & res_out,
               llvm::raw_ostream & warn_out = llvm::errs(),
               llvm::raw_ostream & debug_out = llvm::outs());

    void analyze_file(boost::filesystem::path const &);

private:
    void analyze_module(llvm::Module const &);
    void analyze_function(llvm::Function const &);
    void analyze_basic_block(llvm::BasicBlock const &);

    void process_instruction(llvm::Instruction const &);
    void process_getelementptr(llvm::GetElementPtrInst const &);
    void process_load(llvm::LoadInst const &);
    void process_store(llvm::StoreInst const &);
    void process_memory_access(llvm::Instruction const &, llvm::Value const &);

    sym_range compute_def_range(var_id const &);
    sym_range compute_use_range(var_id const &, program_point_t);
    void update_def_range(var_id const &);

    sym_range compute_def_range_const(llvm::Constant const &);
    sym_range compute_def_range_internal(llvm::Value const &);
    sym_range compute_buffer_size_range(llvm::Value const &);
    boost::tribool is_access_vulnerable(llvm::Value const &);
    boost::tribool is_access_vulnerable_gep(llvm::GetElementPtrInst const &);

    sym_range refine_def_range(var_id, sym_range, program_point_t);
    enum predicate_type {
        PT_EQ,
        PT_NE,
        PT_LT,
        PT_LE,
    };

    struct predicate_t
    {
        predicate_type type;
        var_id lhs;
        var_id rhs;
        program_point_t program_point;
    };

    using predicates_t = std::vector<analyzer_t::predicate_t>;

    predicates_t collect_predicates(llvm::BasicBlock const *);

    // The first argument is a variable for which we want to refine range.
    // The second argument is symbolic range as it's known before call.
    // The third argument is a full representation of a predicate.
    // So refine_def_range_internal(x, y, {PT_EQ, a, b, p}) refines range `y` of `x`
    // taking into account that `a == b` at point `p`.
    sym_range refine_def_range_internal(var_id, sym_range const &, predicate_t const &);

    void report_overflow(llvm::Instruction const &, bool sure = true);
    void report_potential_overflow(llvm::Instruction const &);

private:
    context_t ctx_;
    bool report_indeterminate_;
    llvm::raw_ostream & res_out_;
    llvm::raw_ostream & warn_out_;
    llvm::raw_ostream & debug_out_;
};

scalar_t extract_const(llvm::ConstantInt const &);

boost::optional<scalar_t> extract_const_maybe(llvm::Value const *);
