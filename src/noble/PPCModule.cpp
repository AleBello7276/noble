#include "PPCModule.h"

#include "Logger.h"

PPCModule::PPCModule(std::string path, bool useCache, bool isKernel) {
    mPath = path;
    m_type = isKernel ? BIN_KERNEL : BIN_UNKNOWN;
    mID = -1;


    if(useCache == false) {
        // load and decode instructions
        LoadBinary();

        // recompile into dym lib
        RecompileBinary();
    }

    LOG_ERROR("TranslateBinary {}", "Caching NYI");

    // the cache is very simple in practice, it's just a way to store already recompiled modules, 
    // it doesn't matter where they are located 
    // so all cached binaries will be located in ./cache/<hash>
}

void PPCModule::LoadBinary() {
    mImage = XLoader::ImageLoader::load(mPath);
    if(mImage == nullptr) {
        LOG_ERROR("PBinaryHandle::LoadBinary -> Failed to load binary image");
        return;
    }
    if(m_type == BIN_UNKNOWN) {
        if(dynamic_cast<XLoader::XEXImage*>(mImage.get()) != nullptr) {
            m_type = BIN_XEX;
        }
        else if(dynamic_cast<XLoader::PEImage*>(mImage.get()) != nullptr) {
            m_type = BIN_PE;
        }
        else {
            LOG_ERROR("PBinaryHandle::LoadBinary -> Unknown binary type");
            return;
        }
    }


    DiscoverInstructions();
}

void PPCModule::RecompileBinary() {

}

void PPCModule::DiscoverInstructions() {
    const uint32_t entryPoint = mImage->getEntryPoint();
    const uint32_t imageBase = mImage->getBaseAddress();
    const uint8_t* mData = mImage->getMemoryData();

    uint32_t test = mData[entryPoint - imageBase];
    LOG_INFO("PPCModule::DiscoverInstructions Entry point at 0x{:08X}, first byte: 0x{:02X}", entryPoint, test);

    for(const auto& sec : mImage->getSections()) {

        if(sec->getName() != ".text") {
            continue;
        }

        uint32_t secVirtBase = 0;
        uint32_t secVirtSize = 0;
        
        // relocation for kernel, i hate this.
        if (this->m_type == BIN_KERNEL)
        {
            secVirtBase = 0x80065c00;   // .text real base
            secVirtSize = 0x10A400;     // .text real size
        }


        LOG_DEBUG("PBinaryHandle::LoadBinary Found executable section: {}", sec->getName().c_str());


        uint32_t virtualAddr = sec->getVirtualAddress();
        uint32_t virtualSize = sec->getVirtualSize();
        
        
        const auto base = mImage->getBaseAddress();
        const auto start = base + virtualAddr;
        const auto end = base + virtualAddr + virtualSize;
        
        
        
        const uint8_t* secDataPtr = (const uint8_t*)mImage->getMemoryData() + (virtualAddr);
        uint32_t address = start;

        while (address <= end) {
            // get and byteswap
            uint32_t data = (uint32_t) * (uint32_t*)(secDataPtr + (address - start));
            mInstrMap.try_emplace(address, codec::PPCCodec::decode(data));
             
            address += 4;
        }
    }
}
