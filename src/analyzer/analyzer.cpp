#include "analyzer.h"

#include <iostream>

#include <boost/logic/tribool.hpp>

#include <llvm/ADT/StringRef.h>
#include <llvm/Pass.h>
#include <llvm/Analysis/MemoryBuiltins.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/SourceMgr.h>

namespace fs = boost::filesystem;
using llvm::dyn_cast;
using llvm::dyn_cast_or_null;

/* ------------------------------------------------
 * Overflow check
 * ------------------------------------------------
 */

using boost::tribool;

// Returns true if there definitely is an overflow, indeterminate if it can't
// determine presense of overflow and false if there is definitely no overflow
tribool check_overflow(sym_range const & size_range, sym_range const & idx_range)
{
    if (size_range.hi <= idx_range.hi || idx_range.lo <= sym_expr(scalar_t(-1)))
        return true;

    if (sym_expr(scalar_t(0)) <= idx_range.lo
            && idx_range.hi <= size_range.lo - sym_expr(scalar_t(1)))
        return false;

    return boost::logic::indeterminate;
}

/* ------------------------------------------------
 * Computing symbolic ranges
 * ------------------------------------------------
 */

sym_range analyzer_t::compute_use_range(var_id const & v, program_point_t p)
{
    sym_range r = compute_def_range(v);
    return refine_def_range(v, std::move(r), p);
}

sym_range analyzer_t::compute_def_range(var_id const & v)
{
    if (!v)
        return var_sym_range(v);

    auto it = ctx_.def_ranges.find(v);
    if (it != ctx_.def_ranges.end())
        return it->second;

    if (auto llvm_arg = dynamic_cast<llvm::Argument const *>(v))
    {
        argument_t arg = {llvm_arg->getParent(), llvm_arg->getArgNo()};
        auto iter = ctx_.arg_ranges.find(arg);
        if (iter != ctx_.arg_ranges.end())
        {
            auto res = iter->second;
            ctx_.def_ranges.emplace(v, res);
            return res;
        }
    }

    if (auto const_v = dynamic_cast<llvm::Constant const *>(v))
    {
        auto res = compute_def_range_const(*const_v);
        ctx_.def_ranges.emplace(v, res);
        return res;
    }

    ctx_.new_val_set.insert(v);
    ctx_.def_ranges.emplace(v, sym_range::full);

    auto range = compute_def_range_internal(*v);
    ctx_.def_ranges.erase(v);
    ctx_.def_ranges.emplace(v, std::move(range));

    update_def_range(v);
    ctx_.new_val_set.erase(v);

    auto res_it = ctx_.def_ranges.find(v);
    return res_it == ctx_.def_ranges.end() ? sym_range::full : res_it->second;
}

void analyzer_t::update_def_range(var_id const & v)
{
    if (!v)
        return;

    for (var_id w : v->users())
    {
        if (!ctx_.new_val_set.count(w))
            continue;

        sym_range w_def_range = compute_def_range_internal(*w);
        auto cached_iter = ctx_.def_ranges.find(w);
        sym_range cached = cached_iter == ctx_.def_ranges.end() ? sym_range::full : cached_iter->second;
        w_def_range &= cached;
        if (w_def_range != cached)
        {
            ctx_.def_ranges.erase(w);
            ctx_.def_ranges.emplace(w, w_def_range);
            update_def_range(w);
        }
    }
}

sym_range analyzer_t::compute_def_range_const(llvm::Constant const & c)
{
    llvm::Type * t = c.getType();
    if (!t)
    {
        warn_out_ << "Constant " << c.getName() << " doesn't have a type";
        return var_sym_range(&c);
    }

    if (auto scalar = extract_const_maybe(&c))
    {
        sym_expr e(*scalar);
        return {e, e};
    }

    debug_out_ << "Can't compute def range of constant named \""
               << c.getName()
               << "\" with type \""
               << *t << "\"";

    return var_sym_range(&c);
}

sym_range analyzer_t::compute_def_range_internal(llvm::Value const & v)
{
    if (auto bin_op = dynamic_cast<llvm::BinaryOperator const *>(&v))
    {
        var_id op0 = bin_op->getOperand(0), op1 = bin_op->getOperand(1);
        sym_range op0_range = compute_use_range(op0, bin_op),
                  op1_range = compute_use_range(op1, bin_op);
        if (bin_op->getOpcode() == llvm::BinaryOperator::Add)
            return op0_range + op1_range;
        else if (bin_op->getOpcode() == llvm::BinaryOperator::Sub)
            return op0_range - op1_range;
        else if (bin_op->getOpcode() == llvm::BinaryOperator::Mul)
            return op0_range * op1_range;
        else if (bin_op->getOpcode() == llvm::BinaryOperator::SDiv)
            return op0_range / op1_range;
    }
    else if (auto phi = dynamic_cast<llvm::PHINode const *>(&v))
    {
        sym_range r(sym_range::empty);
        for (var_id inc_v : phi->incoming_values())
            r |= compute_use_range(inc_v, phi);

        if (phi->getNumIncomingValues() == 2)
        {
            var_id inc_v0 = phi->getIncomingValue(0);
            var_id inc_v1 = phi->getIncomingValue(1);

            auto test_value = [&r, phi, this](var_id dependent, var_id another)
            {
                switch (does_monotonically_depend(dependent, phi))
                {
                case MONOTONY_INC:
                {
                    predicate_t predicate = {PT_LE, another, phi, phi};
                    r = refine_def_range_internal(phi, r, predicate);
                    break;
                }
                case MONOTONY_DEC:
                {
                    predicate_t predicate = {PT_LE, phi, another, phi};
                    r = refine_def_range_internal(phi, r, predicate);
                    break;
                }
                default:
                    break;
                }
            };

            test_value(inc_v0, inc_v1);
            test_value(inc_v1, inc_v0);
        }

        return r;
    }
    else if (auto load = dynamic_cast<llvm::LoadInst const *>(&v))
    {
        if (auto gep = dynamic_cast<llvm::GetElementPtrInst const *>(load->getPointerOperand()))
        {
            if (gep->getNumIndices() == 2)  // TODO: check that 0-th index is 0
            {
                llvm::ConstantDataSequential const * const_seq =
                        dynamic_cast<llvm::ConstantDataSequential const *>(gep->getPointerOperand());
                if (!const_seq)
                {
                    if (auto gv = dynamic_cast<llvm::GlobalVariable const *>(gep->getPointerOperand()))
                    {
                        if (gv->isConstant())
                            const_seq = dynamic_cast<llvm::ConstantDataSequential const *>(gv->getInitializer());
                    }
                }
                if (const_seq)
                {
                    auto begin = gep->idx_begin();
                    begin++;
                    sym_range idx_range = compute_use_range(*begin, gep);
                    if (auto scalar_r = to_scalar_range(idx_range))
                    {
                        if (scalar_r->second < 0 || scalar_r->first >= const_seq->getNumElements())
                        {
                            load->getDebugLoc().print(res_out_);
                            res_out_ << "vulnerable access of constant aggregate\n";
                        }
                        else
                        {
                            sym_range res = sym_range::empty;
                            for (unsigned i = scalar_r->first; i <= scalar_r->second; ++i)
                            {
                                scalar_t n(const_seq->getElementAsInteger(i));
                                sym_expr e(n);
                                res.lo = meet(res.lo, e);
                                res.hi = join(res.hi, e);
                            }
                            return res;
                        }
                    }
                }
            }
        }
    }
    else if (auto sext = dynamic_cast<llvm::SExtInst const *>(&v))
    {
        return compute_use_range(sext->getOperand(0), sext);
    }
    else if (auto sext = dynamic_cast<llvm::ZExtInst const *>(&v))
    {
        // TODO: not the best solution
        return compute_use_range(sext->getOperand(0), sext);
    }
    else if (auto type = v.getType())
    {
        if (type->isIntegerTy())
        {
            llvm::IntegerType * int_type = static_cast<llvm::IntegerType *>(type);
            auto bits = int_type->getBitMask();
            scalar_t max(bits >> 1);
            scalar_t min(-max - 1);
            return {sym_expr(min), sym_expr(max)};
        }
    }

    return var_sym_range(&v);
}

/* ------------------------------------------------
 * Computing buffer size
 * ------------------------------------------------
 */

// size is number of elements, not bytes
sym_range analyzer_t::compute_buffer_size_range(llvm::Value const & v)
{
    llvm::TargetLibraryInfo const & tli = ctx_.tliwp.getTLI();
    if (auto alloca = dynamic_cast<llvm::AllocaInst const *>(&v))
        return compute_use_range(alloca->getArraySize(), alloca);
    else if (auto call = dynamic_cast<llvm::CallInst const *>(&v))
    {
        if (llvm::isAllocationFn(&v, &tli, true))
        {
            auto res = compute_use_range(call->getArgOperand(0), call);   // TODO: can it be improved?
            debug_out_ << "Allocated " << res << "\n";
            return res;
        }
    }
    else if (auto bitcast = dynamic_cast<llvm::BitCastInst const *>(&v))
    {
        auto src_type = bitcast->getSrcTy();
        auto dst_type = bitcast->getDestTy();
        if (auto src_ptr_type = dyn_cast_or_null<llvm::PointerType>(src_type))
        {
            if (auto dst_ptr_type = dyn_cast_or_null<llvm::PointerType>(dst_type))
            {
                llvm::Type * src_element_type = src_ptr_type->getElementType();
                llvm::Type * dst_element_type = dst_ptr_type->getElementType();
                if (auto src_int_type = dyn_cast_or_null<llvm::IntegerType>(src_element_type))
                {
                    if (auto dst_int_type = dyn_cast_or_null<llvm::IntegerType>(dst_element_type))
                    {
                        if (src_int_type->getBitWidth() == 8)
                        {
                            unsigned dst_width = dst_int_type->getBitWidth();
                            if (dst_width % 8 == 0)
                            {
                                sym_expr k(scalar_t(dst_width / 8));
                                llvm::Value * operand = bitcast->getOperand(0);
                                if (operand)
                                {
                                    return compute_buffer_size_range(*operand) / k;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    else if (auto const_seq = dynamic_cast<llvm::ConstantDataSequential const *>(&v))
    {
        return const_sym_range(const_seq->getNumElements());
    }

    return { sym_expr(scalar_t(1)), sym_expr::top };
}

/* ------------------------------------------------
 * Vulnerability detection
 * ------------------------------------------------
 */

vulnerability_info_t analyzer_t::is_access_vulnerable(llvm::Value const & v)
{
    auto cached = ctx_.vulnerability_info.find(&v);
    if (cached != ctx_.vulnerability_info.end())
        return cached->second;

    vulnerability_info_t res = { false, sym_range::empty, sym_range::empty };
    if (auto gep = dynamic_cast<llvm::GetElementPtrInst const *>(&v))
        res = is_access_vulnerable_gep(*gep);

    ctx_.vulnerability_info.insert({&v, res});
    return res;
}

vulnerability_info_t analyzer_t::is_access_vulnerable_gep(llvm::GetElementPtrInst const & gep)
{
    auto source_type = gep.getSourceElementType();
    if (!source_type)
        warn_out_ << "GEP instruction doesn't have source element type\n";

    debug_out_ << "Processing GEP with source element type " << *source_type << "\n";
    llvm::Value const * pointer_operand = gep.getPointerOperand();
    if (!pointer_operand)
    {
        warn_out_ << "GEP's pointer operand is null\n";
        return { false, sym_range::empty, sym_range::empty };
    }

    sym_range buf_size = compute_buffer_size_range(*pointer_operand);
    debug_out_ << "GEP's pointer operand's buffer size is in range " << buf_size << "\n";

    sym_range idx_range = compute_use_range(*gep.idx_begin(), &gep);
    debug_out_ << "GEP's base index is in range " << idx_range << "\n";

    return { check_overflow(buf_size, idx_range), idx_range, buf_size };
}

/* ------------------------------------------------
 * Reporting
 * ------------------------------------------------
 */

void analyzer_t::report_overflow(llvm::Instruction const & instr,
                                 sym_range const & idx_range, sym_range const & size_range,
                                 bool sure)
{
    if (sure)
        ++total_overflows_;
    else
        ++total_indeterminate_;

    if (!report_indeterminate_ && !sure)
        return;

    instr.getDebugLoc().print(res_out_);
    llvm::Function const * f = instr.getFunction();
    auto func_name = f ? f->getName() : "<unknown>";
    res_out_ << " | overflow "
             << (sure ? "is possible" : "may be possible (but not surely)")
             << " in function "
             << func_name
             << ", instruction { "
             << instr
             << " }, index range: "
             << idx_range
             << ", size range: "
             << size_range
             << "\n";
}

void analyzer_t::report_potential_overflow(llvm::Instruction const & instr,
                                           sym_range const & idx_range,
                                           sym_range const & size_range)
{
    report_overflow(instr, idx_range, size_range, false);
}

/* ------------------------------------------------
 * Code processing
 * ------------------------------------------------
 */

void analyzer_t::analyze_function(llvm::Function const & f)
{
    debug_out_ << "Analyzing function " << f.getName() << "\n";

    for (auto const & bb : f)
        analyze_basic_block(bb);
}

void analyzer_t::analyze_basic_block(llvm::BasicBlock const & bb)
{
    for (auto const & i : bb)
        process_instruction(i);
}

void analyzer_t::process_instruction(llvm::Instruction const & instr)
{
    if (auto load = dynamic_cast<llvm::LoadInst const *>(&instr))
    {
        process_load(*load);
    }
    else if (auto store = dynamic_cast<llvm::StoreInst const *>(&instr))
    {
        process_store(*store);
    }
    else if (auto call = dynamic_cast<llvm::CallInst const *>(&instr))
    {
        process_call(*call);
    }
}

void analyzer_t::process_load(llvm::LoadInst const & load)
{
    if (auto pointer_operand = load.getPointerOperand())
        return process_memory_access(load, *pointer_operand);
    else
        warn_out_ << "load instruction doesn't have a pointer operand\n";
}

void analyzer_t::process_store(llvm::StoreInst const & store)
{
    if (auto pointer_operand = store.getPointerOperand())
        return process_memory_access(store, *pointer_operand);
    else
        warn_out_ << "store instruction doesn't have a pointer operand\n";
}

// Process instruction 'instr' which accesses memory pointed to by value 'ptr_val'.
void analyzer_t::process_memory_access(llvm::Instruction const & instr, llvm::Value const & ptr_val)
{
    vulnerability_info_t vuln_info = is_access_vulnerable(ptr_val);
    if (vuln_info.decision)
        report_overflow(instr, vuln_info.idx_range, vuln_info.size_range);
    else if (boost::logic::indeterminate(vuln_info.decision))
        report_potential_overflow(instr, vuln_info.idx_range, vuln_info.size_range);
    else
        ++total_correct_;
}

void analyzer_t::process_call(llvm::CallInst const & call)
{
    llvm::Function const * called = call.getCalledFunction();
    for (size_t i = 0; i != call.getNumArgOperands(); ++i)
    {
        argument_t arg = {called, i};
        sym_range range = compute_use_range(call.getArgOperand(i), &call);
        auto iter = ctx_.arg_ranges.find(arg);
        if (iter != ctx_.arg_ranges.end())
        {
            range |= iter->second;
            ctx_.arg_ranges.erase(arg);
        }

        ctx_.arg_ranges.insert({arg, range});
    }
}

/* ------------------------------------------------
 * LLVM wrappers
 * ------------------------------------------------
 */

analyzer_t::analyzer_t(bool report_indeterminate,
                       llvm::raw_ostream & res_out,
                       llvm::raw_ostream & warn_out,
                       llvm::raw_ostream & debug_out)
    : report_indeterminate_(report_indeterminate)
    , res_out_(res_out)
    , warn_out_(warn_out)
    , debug_out_(debug_out)
    , total_overflows_(0)
    , total_indeterminate_(0)
    , total_correct_(0)
{
}

void analyzer_t::analyze_file(fs::path const & p)
{
    llvm::LLVMContext context;
    llvm::SMDiagnostic error;
    auto m = llvm::parseIRFile(p.string(), error, context);
    if (!m)
    {
        std::cerr << "Failed to parse module " << p.string() << std::endl;
        error.print(p.string().c_str(), llvm::errs());
        return;
    }

    analyze_module(*m);
    res_out_ << "Total number of possible overflows: " << total_overflows_
             << ", total number of indeterminate cases: " << total_indeterminate_
             << ", total number of correct memory usages: " << total_correct_
             << "\n";
}

void analyzer_t::analyze_module(llvm::Module const & module)
{
    debug_out_ << "Analyzing module "
               << module.getModuleIdentifier()
               << " corresponding to "
               << module.getSourceFileName()
               << "\n";

    for (auto it = module.rbegin(); it != module.rend(); ++it)
        analyze_function(*it);
}
