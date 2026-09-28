#include <memory>
#include <unordered_map>

#include "Loader/ImageLoader.h"
#include "Loader/PEImage.h"
#include "Loader/XEXImage.h"
#include "powerpc-rs.h"

using GuestAddress = uint32_t;

enum class BinaryType : uint8_t { BIN_XEX, BIN_PE, BIN_KERNEL, BIN_UNKNOWN };

class PPCModule {
public:
    std::string mPath;
    std::unique_ptr<XLoader::IImage> mImage;
    BinaryType m_type;
    uint32_t mID;
    std::unordered_map<GuestAddress, codec::PpcIns> mInstrMap;  // address -> instruction

    PPCModule();
    PPCModule(std::string path, bool useCache = false, bool isKernel = false);

private:
    // Load an internal Image of the XEX or PE file for the emulator to use
    void LoadBinary();

    void DecodeInstructions();
};
