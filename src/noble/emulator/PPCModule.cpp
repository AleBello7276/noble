#include "PPCModule.h"

#include "Logger.h"
#include "Memory.h"
#include "diagnostics/Performance.h"
#include <algorithm>
#include <assert.h>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_set>

PPCModule::PPCModule() : m_type(BinaryType::BIN_UNKNOWN), mID(UINT32_MAX) {}

PPCModule::PPCModule(std::string path, bool useCache, bool isKernel) {
    mPath = path;
    m_type = isKernel ? BinaryType::BIN_KERNEL : BinaryType::BIN_UNKNOWN;
    mID = UINT32_MAX;

    if (useCache == false) {
        // load and decode instructions
        LoadBinary();
    }
}

void PPCModule::LoadBinary() {
    mImage = XLoader::ImageLoader::load(mPath);
    if (mImage == nullptr) {
        LOG_ERROR("PBinaryHandle::LoadBinary -> Failed to load binary image");
        return;
    }
    if (m_type == BinaryType::BIN_UNKNOWN) {
        if (dynamic_cast<XLoader::XEXImage*>(mImage.get()) != nullptr) {
            m_type = BinaryType::BIN_XEX;
        } else if (dynamic_cast<XLoader::PEImage*>(mImage.get()) != nullptr) {
            m_type = BinaryType::BIN_PE;
        } else {
            LOG_ERROR("PBinaryHandle::LoadBinary -> Unknown binary type");
            mImage.reset();
            return;
        }
    }

    AnalyseFunctions();
}

void PPCModule::AnalyseFunctions() {
    diagnostics::PhaseTimer profile(diagnostics::Phase::ModuleAnalysis);
    const uint32_t entryPoint = mImage->getEntryPoint();
    const uint32_t imageBase = mImage->getBaseAddress();
    const uint8_t* mData = mImage->getMemoryData();

    XLoader::Section* pdata = nullptr;
    XLoader::Section* text = nullptr;

    for (const auto& sec : mImage->getSections()) {
        if (sec->getName() == ".text") {
            text = sec.get();
            continue;
        }

        if (sec->getName() == ".pdata") {
            pdata = sec.get();
            continue;
        }
    }

    // map *general* function bounds
    AnalysePDATAFuncs(pdata);
    AnalyseTEXT(text);

    // map Basic blocks in functions
    BuildFunctionCFG(text);

    return;
}

void PPCModule::BuildFunctionCFG(XLoader::Section* text) {
    if (!text)
        return;

    const uint64_t offset = text->getVirtualAddress();
    const uint64_t size = text->getVirtualSize();
    const uint64_t textStart = uint64_t(mImage->getBaseAddress()) + offset;
    const uint64_t textEnd = textStart + size;

    if (offset + size > mImage->getMemorySize())
        throw std::runtime_error("executable section exceeds image data");

    const std::span<const uint8_t> bytes(mImage->getMemoryData() + offset, size);
    for (auto& [key, function] : funcs_) {
        if (function.mStart < textStart || function.mEnd > textEnd)
            throw std::runtime_error("function bounds exceed executable section");

        BuildFunctionCFG(function,
                         bytes.subspan(function.mStart - textStart, function.mEnd - function.mStart));
    }
}

void PPCModule::BuildFunctionCFG(PPCFuncMap& function, std::span<const uint8_t> code) {
    if (function.mStart >= function.mEnd || ((function.mStart | function.mEnd) & 3)
        || code.size() != uint64_t(function.mEnd) - function.mStart)
        throw std::invalid_argument("invalid function");

    std::vector<GuestAddress> boundaries{function.mStart, function.mEnd};
    for (GuestAddress pc = function.mStart; pc < function.mEnd; pc += 4) {
        uint32_t word;
        std::memcpy(&word, code.data() + (pc - function.mStart), sizeof(word));

        const codec::Ins inst(byte_swap(word));

        if (!inst.is_branch())
            continue;

        // every branch has a separate continuation even if its target is resolved at runtime
        if (pc + 4 < function.mEnd)
            boundaries.push_back(pc + 4);

        if (!inst.field_lk()) {
            if (const auto target = inst.branch_dest(pc);
                target && *target >= function.mStart && *target < function.mEnd)
                boundaries.push_back(*target);
        }
    }
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());
    function.bbs_.clear();

    for (size_t i = 0; i + 1 < boundaries.size(); ++i)
        function.bbs_.push_back({boundaries[i], boundaries[i + 1]});
}

PPCFuncMap PPCModule::AnalyseJITBlock(Memory& memory, GuestAddress address, uint64_t limit) {
    constexpr size_t maxInstructions = 256;

    if (address & 3)
        throw std::invalid_argument("unaligned jit entry address");

    limit = (std::min)(limit, uint64_t(UINT32_MAX & ~3u));
    std::vector<uint8_t> code;

    code.reserve(maxInstructions * 4);
    bool tailCall = false;

    for (uint64_t pc = address; code.size() < maxInstructions * 4 && pc + 4 <= limit; pc += 4) {
        const auto* pointer = static_cast<const uint8_t*>(memory.Translate(static_cast<GuestAddress>(pc), 4));
        if (!pointer)
            break;

        uint32_t word;
        std::memcpy(&word, pointer, sizeof(word));
        code.insert(code.end(), pointer, pointer + 4);

        const codec::Ins inst(byte_swap(word));

        if (inst.is_branch() || inst.op == PpcOpcode::Illegal) {
            tailCall = inst.is_unconditional_branch() && !inst.field_lk();
            break;
        }
    }

    if (code.empty())
        throw std::invalid_argument("jit entry has no mapped executable instructions");

    PPCFuncMap function{.mStart = address,
                        .mEnd = static_cast<GuestAddress>(address + code.size()),
                        .mTailCallProlog = tailCall,
                        .mInPdata = false};

    BuildFunctionCFG(function, code);
    return function;
}

void PPCModule::AnalysePDATAFuncs(XLoader::Section* pdata) {
    assert(pdata);  // for debug

    if (!pdata)
        return;

    LOG_DEBUG("PBinaryHandle::LoadBinary Found pdata section: {}", pdata->getName().c_str());

    uint32_t virtualAddr = pdata->getVirtualAddress();
    uint32_t virtualSize = pdata->getVirtualSize();

    const auto base = mImage->getBaseAddress();
    const auto start = base + virtualAddr;
    const auto end = base + virtualAddr + virtualSize;

    const uint8_t* secDataPtr = (const uint8_t*)mImage->getMemoryData() + (virtualAddr);
    uint32_t address = start;

    while (address < end) {
        PDATAFunc pdataEntry;
        pdataEntry.read(*(PDATAFunc*)(secDataPtr + (address - start)));

        GuestAddress endAddr = ((pdataEntry.FunctionLength * 4) + pdataEntry.StartAddress);
        PPCFuncMap func = {
            .mStart = pdataEntry.StartAddress, .mEnd = endAddr, .mTailCallProlog = false, .mInPdata = true};

        funcs_.try_emplace(pdataEntry.StartAddress, func);

        address += sizeof(PDATAFunc);
    }
}

void PPCModule::AnalyseTEXT(XLoader::Section* text) {
    diagnostics::PhaseTimer profile(diagnostics::Phase::FunctionScan);
    assert(text);  // for debug

    if (!text)
        return;

    uint32_t secVirtBase = 0;
    uint32_t secVirtSize = 0;

    LOG_DEBUG("PBinaryHandle::LoadBinary Found executable section: {}", text->getName().c_str());

    uint32_t virtualAddr = text->getVirtualAddress();
    uint32_t virtualSize = text->getVirtualSize();

    const auto base = mImage->getBaseAddress();
    const auto start = base + virtualAddr;
    const auto end = base + virtualAddr + virtualSize;

    mCodeStart_ = start;
    mCodeEnd_ = end;

    const uint8_t* secDataPtr = (const uint8_t*)mImage->getMemoryData() + (virtualAddr);
    uint32_t address = start;

    // find all BL instructions branch targets
    // while checking if not already in map (because pdata may already have it)
    std::unordered_set<GuestAddress> entries;

    for (GuestAddress address = start; address < end; address += 4) {
        uint32_t data = byte_swap(*(uint32_t*)(secDataPtr + (address - start)));

        codec::Ins inst(data);

        // is not a BL instruction
        if (!inst.is_unconditional_branch() || !inst.field_lk())
            continue;

        auto dest = inst.branch_dest(address);
        if (!dest)
            continue;

        const GuestAddress target = *dest;

        if (target & 3)  // check if function target is aligned
            continue;

        // already in pdata
        if (funcs_.contains(target))
            continue;

        // valid
        entries.insert(target);
    }

    // at this point *entries* set is filled with unique entry points
    // now it needs to find a general end of the function, it wont be perfect but good enough

    address = start;

    // uint32_t funcCount = 0;

    for (GuestAddress funcStart : entries) {
        address = funcStart;
        // funcCount++;

        for (;;) {
            uint32_t data = byte_swap(*(uint32_t*)(secDataPtr + (address - start)));
            uint32_t dataAhead = byte_swap(*(uint32_t*)(secDataPtr + (address + 4 - start)));
            codec::Ins inst = codec::Ins(data);
            codec::Ins instAhead = codec::Ins(dataAhead);

            // check for blr or cctr
            if (inst.is_blr() || inst.op == PpcOpcode::Bcctr) {
                // blr + Illegal (0 padding)
                if (instAhead.op == PpcOpcode::Illegal) {
                    // LOG_DEBUG("func {} -> blr or bctr + Illegal (0 padding)", funcCount);

                    PPCFuncMap func = {.mStart = funcStart, .mEnd = address + 4, .mTailCallProlog = false};
                    funcs_.try_emplace(func.mStart, func);
                    break;  // next entry
                }

                // blr + next addr is a function entry
                GuestAddress nextAddr = address + 4;
                if (entries.contains(nextAddr) || funcs_.contains(nextAddr)) {
                    // LOG_DEBUG("func {} -> blr or bctr + next addr is a function entry", funcCount);

                    PPCFuncMap func = {.mStart = funcStart, .mEnd = address + 4, .mTailCallProlog = false};
                    funcs_.try_emplace(func.mStart, func);
                    break;  // next entry
                }
            }

            // check tail call
            if ((inst.is_unconditional_branch() && !inst.field_lk())) {
                //  tail call + Illegal (0 padding)
                if (instAhead.op == PpcOpcode::Illegal) {
                    // LOG_DEBUG("func {} -> tail call + Illegal (0 padding)", funcCount);

                    PPCFuncMap func = {.mStart = funcStart, .mEnd = address + 4, .mTailCallProlog = true};
                    funcs_.try_emplace(func.mStart, func);
                    break;  // next entry
                }

                //  tail call + next addr is a function entry
                GuestAddress nextAddr = address + 4;
                if (entries.contains(nextAddr) || funcs_.contains(nextAddr)) {
                    // LOG_DEBUG("func {} -> tail call + Illegal (0 padding)", funcCount);

                    PPCFuncMap func = {.mStart = funcStart, .mEnd = address + 4, .mTailCallProlog = true};
                    funcs_.try_emplace(func.mStart, func);
                    break;  // next entry
                }
            }

            address += 4;

            if (address >= end) {
                LOG_FATAL("Something went wrong during analysis\n");
                assert(false);
            }
        }
    }
}
