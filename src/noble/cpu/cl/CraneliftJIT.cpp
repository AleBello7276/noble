#include "CraneliftJIT.h"

#include "powerpc-rs.h"
#include <assert.h>
#include <cstring>
#include <format>
#include <string>

#include "clDispatchTable.h"
#include "kernel/hle/Shims.h"

CraneliftJIT::CraneliftJIT(Memory& memory) : memory_(memory), jit_module_(nullptr) {
    jit_builder_ = cranelift::JITBuilder();

    assert(jit_builder_);

    jit_module_ = cranelift::JITModule(std::move(jit_builder_));
}

void CraneliftJIT::CompilePPCModule(PPCModule& module) {
    using namespace cranelift;

    std::lock_guard lock(mutex_);

    if (!module.mImage)
        throw std::invalid_argument("cannot compile a module without an image");

    // compile import entry points before instruction decoding can reach their placeholders
    for (const auto& import : module.mImage->getImports()) {
        // not a function
        if (import->type != ImportType::Function || !import->funcImportAddr)
            continue;

        // add address to map
        const auto [it, inserted] = importsByAddress_.try_emplace(import->funcImportAddr, *import);
        if (!inserted && (it->second.library != import->library || it->second.ordinal != import->ordinal))
            throw std::runtime_error("conflicting imports at the same guest address");

        // compile direct import hle call
        CompileImport(it->second);

        // update table in guest memory
        if (import->tableAddr) {
            auto* slot = memory_.Translate(import->tableAddr, sizeof(uint32_t));

            if (!slot)
                throw std::runtime_error("unmapped import address table slot");

            const uint32_t address = byte_swap(import->funcImportAddr);
            std::memcpy(slot, &address, sizeof(address));
        }
    }

    FunctionBuilderContext _builderContext;  // reusable builder context

    for (auto& [key, bounds] : module.funcs_) {
        const GuestAddress funcStart = bounds.mStart;
        const GuestAddress funcEnd = bounds.mEnd;

        // skip already compiled function
        if (compiledBlocks_.contains(funcStart))
            continue;

        cranelift::Context funcContext = jit_module_.make_context();

        /* jitted guest entry prototpype is void <>(PPCContext* state, void* mem_base) */
        cranelift::Signature funcSig = funcContext.signature();
        funcSig.push_param(cranelift::types::Pointer(jit_module_));
        funcSig.push_param(cranelift::types::Pointer(jit_module_));

        std::string name = std::format("{:08X}", funcStart);  // hex rappresentation
        cranelift::FuncId funcID = jit_module_.declare_function(name.c_str(), CL_LINKAGE_EXPORT, funcSig);

        FunctionBuilder _builder{funcContext, _builderContext};

        // make entry block
        Block entry = _builder.create_block();
        _builder.append_block_params_for_function_params(entry);
        _builder.switch_to_block(entry);

        Value state = _builder.block_param(entry, 0);
        Value base = _builder.block_param(entry, 1);

        // per function / compilation jit block context
        EmitterContext emitter(bounds, state, base, jit_module_, _builder);

        // if this function is an import call into hle
        if (importsByAddress_.contains(funcStart)) {
            const FuncRef target = _builder.declare_func_in_func(jit_module_, functionIds_.at(funcStart));
            emitter.CallGuest(target);
            break;
        }

        GuestAddress address = funcStart;
        for (; address < funcEnd; address += 4) {
            const codec::Ins inst(byte_swap(*memory_.GuestToHostVirtual<uint32_t*>(address)));
            const codec::Opcode op = inst.op;

            InstructionInfo info{.mInst = inst, .mAddress = address};

            if (op == PpcOpcode::Illegal) [[unlikely]] {
                LOG_ERROR("Unknown instruction tried to dispatch. Data: {:08X} Address: {:08X}\n", inst.code,
                          address);
                cl_illegal_handler(emitter, info);
                return;
            }

            const auto i = static_cast<std::size_t>(op);

            assert(i < emitter_dispatch_table.size());

#ifdef NOBLE_CRANELIFT_DEBUG
            if (emitter_dispatch_table[i] == &cl_illegal_handler) {
                emitter_dispatch_table[i](emitter, info);
                LOG_FATAL("Instruction NYI\n");
                return;
            }
#endif

            emitter_dispatch_table[i](emitter, info);  // dispatch
            continue;
        }
        // flush cached register values
        emitter.FlushState();

        // close the function / compiled block
        _builder.ins().return_();
        _builder.seal_all_blocks();
        _builder.finish(jit_module_);

#ifdef NOBLE_CRANELIFT_DEBUG
        LOG_INFO("{}", funcContext.ir());
#endif

        if (!funcContext.verify(jit_module_))
            throw std::runtime_error(last_error());

        if (!jit_module_.define_function(funcID, funcContext))
            throw std::runtime_error(last_error());

        if (!jit_module_.finalize_definitions())
            throw std::runtime_error(last_error());

        // store the function id, may be needed later
        functionIds_.try_emplace(funcStart, funcID);

        // get the function
        const JITBlock compiled = jit_module_.get_finalized_function_as<JITBlock>(funcID);
        compiledBlocks_.try_emplace(funcStart, std::make_shared<const JITBlock>(compiled));
    }
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
    Signature signature = context.signature();
    signature.push_param(types::Pointer(jit_module_));
    signature.push_param(types::Pointer(jit_module_));

    const std::string name = std::format("{:08X}", import.funcImportAddr);

    const FuncId id = jit_module_.declare_function(name.c_str(), CL_LINKAGE_EXPORT, signature);

    if (id == INVALID_ID)
        throw std::runtime_error(last_error());

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
    emitter.Return();

    builder.seal_all_blocks();
    builder.finish(jit_module_);

    if (!context.verify(jit_module_) || !jit_module_.define_function(id, context)
        || !jit_module_.finalize_definitions())
        throw std::runtime_error(last_error());

    functionIds_.emplace(import.funcImportAddr, id);
    const auto function = jit_module_.get_finalized_function_as<JITBlock>(id);

    compiledBlocks_.emplace(import.funcImportAddr, std::make_shared<const JITBlock>(function));
}

void CraneliftJIT::CompileJITBlock(GuestAddress address) {
    std::lock_guard lock(mutex_);

    const auto it = importsByAddress_.find(address);
    if (it != importsByAddress_.end())
        CompileImport(it->second);
}

void CraneliftJIT::InvalidateBlock(GuestAddress address) {}

void CraneliftJIT::InvalidateRegion(GuestAddress from, GuestAddress to) {}

JITBlock CraneliftJIT::FindBlock(GuestAddress address) const {
    std::lock_guard lock(mutex_);

    const auto it = compiledBlocks_.find(address);
    return it != compiledBlocks_.end() ? *it->second : nullptr;
}

cranelift::FuncId CraneliftJIT::FindFunctionId(GuestAddress address) const {
    std::lock_guard lock(mutex_);

    const auto it = functionIds_.find(address);
    return it != functionIds_.end() ? it->second : cranelift::INVALID_ID;
}
