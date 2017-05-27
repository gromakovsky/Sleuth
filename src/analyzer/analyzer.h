#pragma once

#include "context.h"
#include "symbolic.h"
#include "gsa/cond.h"

#include <boost/filesystem.hpp>
#include <boost/logic/tribool.hpp>

#include <llvm/IR/Constant.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Support/raw_ostream.h>

// Program point is an alias for instruction because it corresponds to common
// sense, but in fact we use only basic block from this instruction.
using program_point_t = llvm::Instruction const *;

struct analyzer_t
{
    analyzer_t(bool report_indeterminate,
               llvm::raw_ostream & res_out,
               llvm::raw_ostream & warn_out = llvm::errs(),
               llvm::raw_ostream & debug_out = llvm::outs());

    void analyze_file(boost::filesystem::path const &);

    ~analyzer_t();

private:
    void analyze_module(llvm::Module const &);
    void analyze_function(llvm::Function const &);
    void analyze_basic_block(llvm::BasicBlock const &);

    void process_instruction(llvm::Instruction const &);
    void process_getelementptr(llvm::GetElementPtrInst const &);
    void process_load(llvm::LoadInst const &);
    void process_store(llvm::StoreInst const &);
    void process_memory_access(llvm::Instruction const &, llvm::Value const &);
    void process_call(llvm::CallInst const &);

    sym_range compute_def_range(var_id const &);
    sym_range compute_use_range(var_id const &, program_point_t);
    void update_def_range(var_id const &);

    sym_range compute_def_range_const(llvm::Constant const &);
    sym_range compute_def_range_internal(llvm::Value const &);
    sym_range compute_buffer_size_range(llvm::Value const &);
    vulnerability_info_t is_access_vulnerable(llvm::Value const &);
    vulnerability_info_t is_access_vulnerable_gep(llvm::GetElementPtrInst const &);

    /* ------------------------------------------------
     * Refinement
     * ------------------------------------------------
     */

    // Refine define range according to predicates on ways to the given program point
    sym_range refine_def_range(var_id, sym_range, program_point_t);
    // Refine define range according to the gating condition
    sym_range refine_def_range_gating(var_id, sym_range, gating_cond_t const &);

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
    predicates_t collect_gating_predicates(var_id v, gating_cond_t const &);

    // The first argument is a variable for which we want to refine range.
    // The second argument is symbolic range as it's known before call.
    // The third argument is a full representation of a predicate.
    // So refine_def_range_internal(x, y, {PT_EQ, a, b, p}) refines range `y` of `x`
    // taking into account that `a == b` at point `p`.
    sym_range refine_def_range_internal(var_id, sym_range const &, predicate_t const &);

    /* ------------------------------------------------
     * Reporting
     * ------------------------------------------------
     */

    void report_overflow(llvm::Instruction const &, sym_range const & idx_range,
                         sym_range const & size_range, bool sure = true);
    void report_potential_overflow(llvm::Instruction const &, sym_range const & idx_range,
                                   sym_range const & size_range);
private:
    struct impl_t;
    std::unique_ptr<impl_t> pimpl_;
    impl_t & pimpl();
    impl_t const & pimpl() const;
};

