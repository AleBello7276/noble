#include <memory>
#include <unordered_map>

#include "Loader/ImageLoader.h"
#include "Loader/PEImage.h"
#include "Loader/XEXImage.h"
#include "core/bswap.h"
#include "powerpc-rs.h"

using GuestAddress = uint32_t;

enum class BinaryType : uint8_t { BIN_XEX, BIN_PE, BIN_KERNEL, BIN_UNKNOWN };

struct PDATAFunc {
    GuestAddress StartAddress;

    union {
        uint32_t data;
        struct {
            uint32_t PrologLength : 8;
            uint32_t FunctionLength : 22;
            uint32_t ThirtyTwoBit : 1;
            uint32_t ExceptionFlag : 1;
        };
    };

    void read(PDATAFunc func) {
        StartAddress = bswap32(func.StartAddress);
        data = bswap32(func.data);
    }
};

struct PPCBasicBlock {
    GuestAddress mStartAddress;
    GuestAddress mEndAddress;
};

struct PPCFuncMap {
    GuestAddress mStart;
    GuestAddress mEnd;
    std::vector<PPCBasicBlock> bbs_;
    bool mTailCallProlog;
    bool mInPdata;
};

class PPCModule {
public:
    std::string mPath;
    std::unique_ptr<XLoader::IImage> mImage;
    BinaryType m_type;
    uint32_t mID;

    /* entry points both from pdata and from simple BL instruction search */
    std::unordered_map<GuestAddress, PPCFuncMap> funcs_;

    PPCModule();
    PPCModule(std::string path, bool useCache = false, bool isKernel = false);

    /* Guest start and end addresses of .text section */
    GuestAddress GetCodeStart() { return mCodeStart_; }
    GuestAddress GetCodeEnd() { return mCodeEnd_; }

private:
    // Load an internal Image of the XEX or PE file for the emulator to use
    void LoadBinary();

    /* simple bound analysis for functions in the current Module */
    void AnalyseFunctions();

    /* extract function bounds from pdata */
    void AnalysePDATAFuncs(XLoader::Section* pdata);

    /* analyse function bounds in text with some heuristics */
    void AnalyseTEXT(XLoader::Section* text);

    /* do some CFG analysis over functions in *funcs_* to map Basic Blocks */
    void BuildFunctionCFG(XLoader::Section* text);

private:
    GuestAddress mCodeStart_;
    GuestAddress mCodeEnd_;
};
