#include "PPCModule.h"

#include "Logger.h"
#include <algorithm>
#include <assert.h>
#include <cstring>
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
    const GuestAddress virtualAddr = text->getVirtualAddress();
    const GuestAddress virtualSize = text->getVirtualSize();

    const GuestAddress base = mImage->getBaseAddress();
    const GuestAddress textStart = base + virtualAddr;
    const GuestAddress textEnd = textStart + virtualSize;

    const auto* secData = static_cast<const uint8_t*>(mImage->getMemoryData()) + virtualAddr;

    for (auto& [key, func] : funcs_) {
        func.bbs_.clear();

        // check if out if ranges are out of bounds, and if aligned
        if (func.mStart < textStart || func.mEnd > textEnd || func.mStart >= func.mEnd
            || ((func.mStart | func.mEnd) & 3)) {
            assert(false);
            continue;
        }

        std::vector<GuestAddress> boundaries;
        boundaries.push_back(func.mStart);
        boundaries.push_back(func.mEnd);

        for (GuestAddress pc = func.mStart; pc < func.mEnd; pc += 4) {
            const auto offset = pc - textStart;

            uint32_t raw;
            std::memcpy(&raw, secData + offset, sizeof(raw));

            codec::Ins inst{bswap32(raw)};

            const bool directBranch
                = (inst.is_unconditional_branch() || inst.is_conditional_branch()) && !inst.field_lk();

            const bool indirectTerminator
                = inst.is_blr() || (inst.op == PpcOpcode::Bcctr && !inst.field_lk());

            if (directBranch) {
                if (auto dest = inst.branch_dest(pc)) {
                    if (*dest >= func.mStart && *dest < func.mEnd)
                        boundaries.push_back(*dest);
                }

                // Branch ends the current BB.
                if (pc + 4 < func.mEnd)
                    boundaries.push_back(pc + 4);
            } else if (indirectTerminator) {
                if (pc + 4 < func.mEnd)
                    boundaries.push_back(pc + 4);
            }
        }

        std::sort(boundaries.begin(), boundaries.end());
        boundaries.erase(std::unique(boundaries.begin(), boundaries.end()), boundaries.end());

        for (size_t i = 0; i + 1 < boundaries.size(); ++i) {
            if (boundaries[i] == boundaries[i + 1])
                continue;

            func.bbs_.push_back({
                .mStartAddress = boundaries[i],
                .mEndAddress = boundaries[i + 1],
            });
        }
    }
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
        uint32_t data = bswap32(*(uint32_t*)(secDataPtr + (address - start)));

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
            uint32_t data = bswap32(*(uint32_t*)(secDataPtr + (address - start)));
            uint32_t dataAhead = bswap32(*(uint32_t*)(secDataPtr + (address + 4 - start)));
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
