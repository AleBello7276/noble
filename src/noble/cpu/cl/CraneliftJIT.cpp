#include "CraneliftJIT.h"

#include "core/HostClock.h"
#include "debugger/Debugger.h"
#include "diagnostics/Performance.h"
#include "powerpc-rs.h"
#include <algorithm>
#include <assert.h>
#include <cstring>
#include <format>
#include <string>

#include "clDispatchTable.h"
#include "kernel/hle/Shims.h"

// TODO: move this
static void host_yield() {
    LOG_FATAL("Unhandled yield.");
    throw std::runtime_error("Unhandled yield.");
    assert(false);
}

static uint64_t host_load_clock() {
    return HostClock::GetInstance().GetGuestTickCount();
}

CraneliftJIT::CraneliftJIT(Memory& memory, bool debugging, const config::JITConfig& settings)
    : memory_(memory), config_(settings), debugging_(debugging),
      dispatchBlocks_(settings.compilation == config::Compilation::Blocks), jit_module_(nullptr) {
    if (!config_.branchBudget || config_.branchBudget > INT32_MAX)
        throw std::invalid_argument("jit branch budget must be between 1 and 2147483647");

    const std::string optimization = config::Name(config_.optimization);
    std::vector<const char*> names{"opt_level", "enable_verifier"};
    std::vector<const char*> values{optimization.c_str(), config_.verifyPasses ? "true" : "false"};

    for (const auto& [name, value] : config_.flags) {
        if (name == "opt_level" || name == "enable_verifier")
            throw std::invalid_argument("use the typed jit setting for " + name);
        names.push_back(name.c_str());
        values.push_back(value.c_str());
    }

    jit_builder_ = cranelift::JITBuilder::with_flags(names.data(), values.data(), names.size());
    if (!jit_builder_)
        throw std::runtime_error(cranelift::last_error());

    jit_builder_.symbol("host_yield", host_yield);
    jit_builder_.symbol("host_load_clock", host_load_clock);
    if (debugging_)
        jit_builder_.symbol("host_debug_instruction", &debugger::ExecutionSession::Instruction);

    jit_module_ = cranelift::JITModule(std::move(jit_builder_));

    auto yield_sig = jit_module_.make_signature();
    host_yield_id
        = jit_module_.declare_function("host_yield", cranelift::Linkage::CL_LINKAGE_IMPORT, yield_sig);

    auto clock_sig = jit_module_.make_signature();
    clock_sig.push_return(cranelift::types::I64());
    host_load_clock_id
        = jit_module_.declare_function("host_load_clock", cranelift::Linkage::CL_LINKAGE_IMPORT, clock_sig);
    if (debugging_) {
        auto signature = jit_module_.make_signature();
        signature.push_param(cranelift::types::Pointer(jit_module_));
        signature.push_param(cranelift::types::I32());
        signature.push_return(cranelift::types::I32());
        host_debug_instruction_id
            = jit_module_.declare_function("host_debug_instruction", cranelift::CL_LINKAGE_IMPORT, signature);
    }
}

void CraneliftJIT::RegisterPPCModule(const PPCModule& module) {
    std::lock_guard lock(mutex_);
    RegisterModule(module);
}

void CraneliftJIT::RegisterModule(const PPCModule& module) {
    if (!module.mImage)
        throw std::invalid_argument("module not an image");

    // retain metadata copies
    for (const auto& section : module.mImage->getSections()) {
        if (!section->isExecutable() || !section->getVirtualSize())
            continue;

        const uint64_t start = uint64_t(module.mImage->getBaseAddress()) + section->getVirtualAddress();
        const uint64_t end = start + section->getVirtualSize();

        if (start > UINT32_MAX || end > uint64_t(UINT32_MAX) + 1)
            throw std::invalid_argument("executable section exceeds guest address space");

        const auto duplicate
            = std::find_if(codeRegions_.begin(), codeRegions_.end(), [&](const CodeRegion& region) {
                  return region.start == start && region.end == end;
              });

        if (duplicate == codeRegions_.end())
            codeRegions_.push_back({static_cast<GuestAddress>(start), end});
    }

    for (const auto& [address, bounds] : module.funcs_) {
        if (address != bounds.mStart || bounds.mStart >= bounds.mEnd || ((bounds.mStart | bounds.mEnd) & 3))
            throw std::invalid_argument("invalid registered function bounds");

        functionBounds_.insert_or_assign(address, bounds);
    }
    for (const auto& import : module.mImage->getImports()) {
        if (import->type == ImportType::Variable) {
            if (!imports_)
                throw std::logic_error("attach an hle registry before binding variable imports");
            imports_->BindVariableImport(*import);
            continue;
        }

        if (import->type != ImportType::Function || !import->funcImportAddr)
            continue;

        const auto [it, inserted] = importsByAddress_.try_emplace(import->funcImportAddr, *import);
        if (!inserted && (it->second.library != import->library || it->second.ordinal != import->ordinal))
            throw std::runtime_error("conflicting imports at the same guest address");

        if (import->tableAddr) {
            auto* slot = memory_.Translate(import->tableAddr, sizeof(uint32_t));
            if (!slot)
                throw std::runtime_error("unmapped import address table slot");

            const uint32_t target = byte_swap(import->funcImportAddr);
            std::memcpy(slot, &target, sizeof(target));
        }
    }
}

void CraneliftJIT::CompilePPCModule(PPCModule& module) {
    std::lock_guard lock(mutex_);
    RegisterModule(module);

    for (const auto& import : module.mImage->getImports())
        if (import->type == ImportType::Function && import->funcImportAddr)
            CompileImport(*import);

    for (const auto& [address, bounds] : module.funcs_)
        if (!importsByAddress_.contains(address) && !CompileFunction(bounds))
            return;
}

bool CraneliftJIT::CompileFunction(const PPCFuncMap& bounds) {
    using namespace cranelift;
    const GuestAddress funcStart = bounds.mStart;
    const GuestAddress funcEnd = bounds.mEnd;

    if (compiledBlocks_.contains(funcStart))
        return true;
    diagnostics::CompileTimer compileProfile(funcStart, funcEnd - funcStart);

    if (funcStart >= funcEnd || ((funcStart | funcEnd) & 3))
        throw std::invalid_argument("invalid jit function bounds");

    const size_t size = uint64_t(funcEnd) - funcStart;
    const auto* pointer = static_cast<const uint8_t*>(memory_.Translate(funcStart, size));

    if (!pointer)
        throw std::invalid_argument("unmapped jit instruction range");

    const std::span<const uint8_t> bytes(pointer, size);
    PPCFuncMap analyzed = bounds;
    {
        diagnostics::PhaseTimer profile(diagnostics::Phase::FunctionCFG);
        PPCModule::BuildFunctionCFG(analyzed, bytes);
    }
    diagnostics::PhaseTimer emissionProfile(diagnostics::Phase::EmitIR);

    cranelift::Context funcContext = jit_module_.make_context();

    const FuncId funcID = DeclareGuestFunction(funcStart).m_id;
    funcContext.signature().push_param(types::Pointer(jit_module_));
    funcContext.signature().push_param(types::Pointer(jit_module_));
    FunctionBuilderContext _builderContext;

    FunctionBuilder _builder{funcContext, _builderContext};

    // make entry block
    Block entry = _builder.create_block();
    _builder.append_block_params_for_function_params(entry);
    _builder.switch_to_block(entry);

    Value state = _builder.block_param(entry, 0);
    Value base = _builder.block_param(entry, 1);

    // per function / compilation jit block context
    EmitterContext emitter(analyzed, state, base, jit_module_, _builder, &memory_, this);
    if (!debugging_) {
        emitter.branchBudget = _builder.declare_var(types::I32());
        _builder.def_var(emitter.branchBudget, emitter.i32(config_.branchBudget));
    }

    // keep guest branches out of the native entry block containing abi parameters
    const Block guestEntry = _builder.create_block();
    emitter.Jump(guestEntry);
    emitter.SwitchToBlock(guestEntry);
    emitter.clBlockMap.emplace(funcStart, guestEntry);

    // add all block edges labels
    for (auto block : analyzed.bbs_) {
        emitter.BlockLookup(block.mStartAddress);
        continue;
    }

    GuestAddress address = funcStart;
    for (; address < funcEnd; address += 4) {
        uint32_t word;
        std::memcpy(&word, bytes.data() + (address - funcStart), sizeof(word));
        const codec::Ins inst(byte_swap(word));
        const codec::Opcode op = inst.op;

        InstructionInfo info{.mInst = inst, .mAddress = address};
#if NOBLE_CRANELIFT_DEBUG
        auto str = std::format("{:08X} : {}", info.mAddress, info.mInst.simplified().to_string());
        emitter.comment(str);
#endif

        // if the current address is start of a block, switch to it
        if (emitter.clBlockMap.contains(address)) {
            if (address != funcStart)
                emitter.SwitchToBlock(emitter.BlockLookup(address));
        }

        if (emitter.terminated)
            continue;

        if (op == PpcOpcode::Illegal) [[unlikely]] {
            LOG_ERROR("Unknown instruction tried to dispatch. Data: {:08X} Address: {:08X}\n", inst.code,
                      address);
            cl_illegal_handler(emitter, info);
            return false;
        }

        const auto i = static_cast<std::size_t>(op);

        assert(i < emitter_dispatch_table.size());

        if (i >= emitter_dispatch_table.size() || !emitter_dispatch_table[i]) {
            cl_illegal_handler(emitter, info);
            LOG_FATAL("Instruction NYI at {:08X}: {:08X} {} (entry {:08X})\n", address, inst.code,
                      inst.basic().to_string(), funcStart);
            return false;
        }

        if (debugging_) {
            const auto hook = _builder.declare_func_in_func(jit_module_, host_debug_instruction_id);
            const auto call = emitter.Call(hook, std::array{state, emitter.i32(address)});
            const auto proceed = _builder.inst_result(call, 0);
            const auto execute = _builder.create_block();
            const auto stopped = _builder.create_block();
            emitter.Branch(proceed, execute, {}, stopped, {});
            emitter.SwitchToBlock(stopped);
            emitter.Return();
            emitter.SwitchToBlock(execute);
        }

        emitter_dispatch_table[i](emitter, info);  // dispatch

        // if next address a start of a new block, add a fall through jump to it
        if (!emitter.terminated && emitter.clBlockMap.contains(address + 4)) {
            emitter.Jump(emitter.BlockLookup(address + 4));
        }

        continue;
    }
    // close the final block if its instruction did not already terminate it
    if (!emitter.terminated) {
        emitter.ins().store(_builder.memflags_new(), emitter.i32(funcEnd), state, offsetof(PPCContext, NIA));
        emitter.Return();
    }
    _builder.seal_all_blocks();
    _builder.finish(jit_module_);

    if (config_.dumpIR || NOBLE_CRANELIFT_DEBUG)
        LOG_INFO("{}", funcContext.ir());

    emissionProfile.Stop();
    PublishFunction(funcID, funcContext, funcStart, funcEnd);
    return true;
}

void CraneliftJIT::SetHLERegistry(hle::Registry* registry) {
    std::lock_guard lock(mutex_);

    if (!compiledBlocks_.empty() && imports_ != registry)
        throw std::logic_error("cannot replace the hle registry while compiled code refers to it");

    imports_ = registry;
}

void CraneliftJIT::CompileImport(const XLoader::Import& import) {
    using namespace cranelift;

    if (compiledBlocks_.contains(import.funcImportAddr))
        return;

    if (!imports_)
        throw std::logic_error("attach an hle registry before compiling imports");

    const auto hostEntry = imports_->Resolve(import.library, static_cast<uint16_t>(import.ordinal));

    Context context = jit_module_.make_context();
    context.signature().push_param(types::Pointer(jit_module_));
    context.signature().push_param(types::Pointer(jit_module_));
    const FuncId id = DeclareGuestFunction(import.funcImportAddr).m_id;

    // host sig
    Signature hostSignature = jit_module_.make_signature();
    hostSignature.push_param(types::Pointer(jit_module_));
    hostSignature.push_param(types::Pointer(jit_module_));
    hostSignature.push_param(types::I32());
    hostSignature.push_param(types::I32());
    hostSignature.push_param(types::I32());

    FunctionBuilderContext builderContext;
    FunctionBuilder builder(context, builderContext);

    // entry block
    const Block entry = builder.create_block();
    builder.append_block_params_for_function_params(entry);
    builder.switch_to_block(entry);

    const Value state = builder.block_param(entry, 0);
    const Value base = builder.block_param(entry, 1);

    EmitterContext emitter({}, state, base, jit_module_, builder);

    const SigRef hostSignatureRef = builder.import_signature(hostSignature);
    const Value target = builder.ins().iconst(types::Pointer(jit_module_),
                                              static_cast<int64_t>(reinterpret_cast<uintptr_t>(hostEntry)));
    const std::array args{
        builder.ins().iconst(types::Pointer(jit_module_),
                             static_cast<int64_t>(reinterpret_cast<uintptr_t>(imports_))),
        state,
        builder.ins().iconst(types::I32(), static_cast<uint32_t>(import.library)),
        builder.ins().iconst(types::I32(), import.ordinal),
        builder.ins().iconst(types::I32(), import.funcImportAddr),
    };

    emitter.CallIndirect(hostSignatureRef, target, args);
    const Value returnAddress
        = builder.ins().band(builder.ins().ireduce(types::I32(), emitter.load_spr(eSPR::LR)),
                             builder.ins().iconst(types::I32(), -4));
    builder.ins().store(builder.memflags_new(), returnAddress, state, offsetof(PPCContext, NIA));
    emitter.Return();

    builder.seal_all_blocks();
    builder.finish(jit_module_);

    PublishFunction(id, context, import.funcImportAddr, import.funcImportAddr + 16);
}

void CraneliftJIT::CompileJITBlock(GuestAddress address) {
    diagnostics::PhaseTimer waitProfile(diagnostics::Phase::CompileWait);
    std::lock_guard lock(mutex_);
    waitProfile.Stop();
    if (address & 3)
        throw std::invalid_argument("unaligned entry address");

    if (compiledBlocks_.contains(address))
        return;

    if (const auto it = importsByAddress_.find(address); it != importsByAddress_.end()) {
        CompileImport(it->second);
        return;
    }

    if (debugging_ || dispatchBlocks_) {
        uint64_t limit = uint64_t(UINT32_MAX & ~3u);
        for (const auto& region : codeRegions_)
            if (address >= region.start && address < region.end)
                limit = (std::min)(limit, region.end);
        auto bounds = PPCModule::AnalyseJITBlock(memory_, address, limit);
        // execute the supported prefix before reporting an unsupported instruction
        {
            for (GuestAddress pc = address; pc < bounds.mEnd; pc += 4) {
                uint32_t word;
                std::memcpy(&word, memory_.Translate(pc, 4), 4);
                const codec::Ins inst(byte_swap(word));
                const size_t opcode = size_t(inst.op);
                if (pc > address
                    && (opcode >= emitter_dispatch_table.size() || !emitter_dispatch_table[opcode])) {
                    bounds.mEnd = pc;
                    break;
                }
            }
        }
        CompileFunction(bounds);
        return;
    }

    // an entry inside a known function receives its own native entry at that exact address
    for (auto it = functionBounds_.upper_bound(address); it != functionBounds_.begin();) {
        --it;

        if (address < it->second.mEnd) {
            auto bounds = it->second;
            bounds.mStart = address;
            CompileFunction(bounds);
            return;
        }
    }

    uint64_t limit = uint64_t(UINT32_MAX & ~3u);
    for (const auto& region : codeRegions_)
        if (address >= region.start && address < region.end)
            limit = (std::min)(limit, region.end);

    if (const auto next = functionBounds_.upper_bound(address); next != functionBounds_.end())
        limit = (std::min)(limit, uint64_t(next->first));

    CompileFunction(PPCModule::AnalyseJITBlock(memory_, address, limit));
}

void CraneliftJIT::InvalidateBlock(GuestAddress address) {}

bool CraneliftJIT::IsImport(GuestAddress address) const {
    std::lock_guard lock(mutex_);
    return importsByAddress_.contains(address);
}

void CraneliftJIT::InvalidateRegion(GuestAddress from, GuestAddress to) {}

JITBlock CraneliftJIT::FindBlock(GuestAddress address) const {
    std::lock_guard lock(compiledMutex_);

    const auto it = compiledBlocks_.find(address);
    return it != compiledBlocks_.end() ? *it->second : nullptr;
}

cranelift::Signature CraneliftJIT::GuestSignature() {
    auto signature = jit_module_.make_signature();
    signature.push_param(cranelift::types::Pointer(jit_module_));
    signature.push_param(cranelift::types::Pointer(jit_module_));

    return signature;
}

JITFunction CraneliftJIT::LookupFunction(GuestAddress address) {
    std::lock_guard lock(mutex_);
    return DeclareGuestFunction(address);
}

JITFunction CraneliftJIT::DeclareGuestFunction(GuestAddress address) {
    std::lock_guard lock(funcMutex_);
    if (const auto it = functions_.find(address); it != functions_.end())
        return it->second;

    const auto signature = GuestSignature();
    const std::string name = std::format("{:08X}", address);
    const auto id = jit_module_.declare_function(name.c_str(), CL_LINKAGE_EXPORT, signature);

    if (id == cranelift::INVALID_ID)
        throw std::runtime_error(cranelift::last_error());

    const JITFunction function{.mStartAddress = address, .mEndAddress = 0, .m_id = id};
    functions_.emplace(address, function);

    return function;
}

void CraneliftJIT::PublishFunction(cranelift::FuncId id, cranelift::Context& context, GuestAddress start,
                                   GuestAddress end) {
    {
        diagnostics::PhaseTimer profile(diagnostics::Phase::Verify);
        if (!context.verify(jit_module_))
            throw std::runtime_error(cranelift::last_error());
    }
    {
        diagnostics::PhaseTimer profile(diagnostics::Phase::Codegen);
        if (!jit_module_.define_function(id, context))
            throw std::runtime_error(cranelift::last_error());
    }
    {
        diagnostics::PhaseTimer profile(diagnostics::Phase::Finalize);
        if (!jit_module_.finalize_definitions())
            throw std::runtime_error(cranelift::last_error());
    }

    const JITBlock compiled = jit_module_.get_finalized_function_as<JITBlock>(id);
    if (!compiled)
        throw std::runtime_error("compiled function has no native entry");

    {
        std::lock_guard lock(compiledMutex_);
        compiledBlocks_.emplace(start, std::make_shared<const JITBlock>(compiled));
    }
    std::lock_guard lock(funcMutex_);

    functions_.insert_or_assign(
        start, JITFunction{.mStartAddress = start, .mEndAddress = end, .m_id = id, .mCallable = true});
}

std::optional<JITFunction> CraneliftJIT::FindFunction(GuestAddress address) const {
    std::lock_guard lock(funcMutex_);
    const auto it = functions_.find(address);

    if (it != functions_.end() && it->second.mCallable)
        return it->second;

    return std::nullopt;
}

std::vector<JITFunction> CraneliftJIT::CallableFunctions() const {
    std::lock_guard lock(funcMutex_);
    std::vector<JITFunction> result;

    for (const auto& [address, function] : functions_)
        if (function.mCallable)
            result.push_back(function);

    return result;
}
