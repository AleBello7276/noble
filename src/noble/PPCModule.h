#include <unordered_map>
#include <memory>


#include "Codec/ppc_codec.h"
#include "Loader/ImageLoader.h"
#include <Loader/XEXImage.h>
#include <Loader/PEImage.h>

enum BinaryType : uint8_t {
    BIN_XEX,
    BIN_PE,
    BIN_KERNEL,
    BIN_UNKNOWN
};


const uint32_t MAX_BLOCK_SIZE = 1; // how many instructions a block can contain -1 is unlimited

class PPCModule {
public:
    std::string mPath;
    std::unique_ptr<XLoader::IImage> mImage;
    BinaryType m_type;
    uint32_t mID;
    std::unordered_map<uint32_t, codec::DecodedInst> mInstrMap; // address -> instruction
    
    PPCModule(std::string path, bool useCache = false, bool isKernel = false);
private:
    // Load an internal Image of the XEX or PE file for the emulator to use
    void LoadBinary();

    // Recompile the code into LLVM IR
    void RecompileBinary();

    // Run several simple passes of analysis over the Module code 
    // starting from the entrypoint and spreading using known branches
    // it naturally discovers all instructions in the code without relying on .text section which might be misleading (xboxkrnl)
    void DiscoverInstructions();
};