#include "CraneliftJIT.h"

#include "powerpc-rs.h"
#include <assert.h>
#include <format>
#include <string>

#include "clDispatchTable.h"

CraneliftJIT::CraneliftJIT(Memory& memory) : memory_(memory), jit_module_(nullptr) {
    jit_builder_ = cranelift::JITBuilder();

    assert(jit_builder_);
    jit_module_ = cranelift::JITModule(std::move(jit_builder_));
}

void CraneliftJIT::CompilePPCModule(PPCModule& module) {
    using namespace cranelift;

    FunctionBuilderContext _builderContext;  // reusable builder context
    for (auto& [key, bounds] : module.funcs_) {
        const GuestAddress funcStart = bounds.mStart;
        const GuestAddress funcEnd = bounds.mEnd;

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

        GuestAddress address = funcStart;

        // per function / compilation jit block context
        EmitterContext emitter(bounds, state, base, jit_module_, _builder);

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
            emitter_dispatch_table[i](emitter, info);  // dispatch
            // emitter.FlushState();
            // LOG_INFO("{}", funcContext.ir());
        }

        // flush cached register values
        emitter.FlushState();
    }

    return;

    cranelift::Context context = jit_module_.make_context();

    cranelift::Signature signature = context.signature();
    signature.push_param(cranelift::types::I64());

    cranelift::FuncId id = jit_module_.declare_function("add_two", CL_LINKAGE_EXPORT, signature);

    assert(id != INVALID_ID);

    FunctionBuilderContext builder_context;
    {
        FunctionBuilder builder{context, builder_context};

        Block entry = builder.create_block();
        builder.append_block_params_for_function_params(entry);
        builder.switch_to_block(entry);

        Value input = builder.block_param(entry, 0);
        Value two = builder.ins().iconst(types::I64(), 2);
        Value sum = builder.ins().iadd(input, two);

        std::array<Value, 1> returns{sum};
        builder.ins().return_(returns);

        builder.seal_all_blocks();
        builder.finish(jit_module_);
    }

    LOG_INFO("{}", context.ir());

    if (!context.verify(jit_module_))
        throw std::runtime_error(last_error());

    if (!jit_module_.define_function(id, context))
        throw std::runtime_error(last_error());

    if (!jit_module_.finalize_definitions())
        throw std::runtime_error(last_error());

    using Function = std::int64_t (*)(std::int64_t);
    const Function function = jit_module_.get_finalized_function_as<Function>(id);

    int a = function(40);

    void();
}

void CraneliftJIT::CompileJITBlock(GuestAddress address) {}

void CraneliftJIT::InvalidateBlock(GuestAddress address) {}

void CraneliftJIT::InvalidateRegion(GuestAddress from, GuestAddress to) {}

JITBlock CraneliftJIT::FindBlock(GuestAddress address) const {
    return NULL;
}
