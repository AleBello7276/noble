#include "ImageLoader.h"
#include "PEImage.h"
#include "XEXImage.h"
#include <fstream>
#include <memory>
#include <cstring>

namespace XLoader
{

    class BinaryReader {
    public:
        BinaryReader(const uint8_t* data, size_t size)
            : m_data(data), m_size(size), m_offset(0) {
        }

        bool read(void* dest, size_t count) {
            if (m_offset + count > m_size) {
                return false;
            }
            std::memcpy(dest, m_data + m_offset, count);
            m_offset += count;
            return true;
        }

        bool seek(size_t offset) {
            if (offset > m_size) {
                return false;
            }
            m_offset = offset;
            return true;
        }

        size_t tell() const { return m_offset; }
        size_t size() const { return m_size; }
        const uint8_t* data() const { return m_data; }
        const uint8_t* current() const { return m_data + m_offset; }
        size_t remaining() const { return m_size - m_offset; }

    private:
        const uint8_t* m_data;
        size_t m_size;
        size_t m_offset;
    };

    std::unique_ptr<IImage> ImageLoader::load(const std::string& path) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            printf("Failed to open file\n");
            return nullptr;
        }

        size_t fileSize = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<uint8_t> buffer(fileSize);
        if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
            printf("Failed to read file\n");
            return nullptr;
        }

        return loadFromMemory(buffer.data(), fileSize);
    }

    std::unique_ptr<IImage> ImageLoader::loadFromMemory(const uint8_t* data, size_t size) {
        ImageType type = detectType(data, size);

        std::unique_ptr<IImage> image;
        switch (type) {
        case ImageType::PE:
            printf("Detected PE file\n");
            image = std::make_unique<PEImage>();
            break;

        case ImageType::XEX2:
            printf("Detected XEX2 file\n");
            image = std::make_unique<XEXImage>();
            break;

        default:
            printf("Unknown file type\n");
            return nullptr;
        }

        if (!image->load(data, size)) {
            printf("Failed to load image\n");
            return nullptr;
        }

        return image;
    }

    ImageType ImageLoader::detectType(const uint8_t* data, size_t size) {
        if (size < 4) {
            return ImageType::Unknown;
        }

        // Check for MZ header (PE)
        if (data[0] == 'M' && data[1] == 'Z') {
            return ImageType::PE;
        }

        // Check for XEX2 header
        if (size >= 4) {
            uint32_t magic = *reinterpret_cast<const uint32_t*>(data);
            // Need to swap bytes for big-endian XEX2 format
            uint32_t xex2Magic = ((magic & 0xFF000000) >> 24) |
                ((magic & 0x00FF0000) >> 8) |
                ((magic & 0x0000FF00) << 8) |
                ((magic & 0x000000FF) << 24);

            if (xex2Magic == 0x58455832) { // 'XEX2'
                return ImageType::XEX2;
            }
        }

        return ImageType::Unknown;
    }

    PEImage::~PEImage() {
        if (m_memoryData) {
            delete[] m_memoryData;
        }
    }

    bool PEImage::load(const uint8_t* data, size_t size) {
        printf("Loading PE image...\n");

        if (!loadHeaders(data, size)) {
            printf("Failed to load PE headers\n");
            return false;
        }

        if (!loadSections(data, size)) {
            printf("Failed to load PE sections\n");
            return false;
        }

        buildMemoryImage(data, size);

        if (!loadImports(data, size)) {
            printf("Failed to load PE imports\n");
            return false;
        }

        printf("PE image loaded successfully\n");
        printf("  Base address: 0x%08X\n", m_baseAddress);
        printf("  Entry point: 0x%08X\n", m_entryPoint);
        printf("  Sections: %zu\n", m_sections.size());
        printf("  Imports: %zu\n", m_imports.size());

        return true;
    }

    bool PEImage::loadHeaders(const uint8_t* data, size_t size) {
        BinaryReader reader(data, size);

        if (!reader.read(&m_dosHeader, sizeof(DOSHeader))) {
            return false;
        }

        if (!m_dosHeader.isValid()) {
            printf("Invalid DOS header\n");
            return false;
        }

        if (!reader.seek(m_dosHeader.newHeaderOffset)) {
            return false;
        }

        uint32_t peSignature;
        if (!reader.read(&peSignature, sizeof(uint32_t))) {
            return false;
        }

        if (peSignature != 0x00004550) { // "PE\0\0"
            printf("Invalid PE signature\n");
            return false;
        }

        if (!reader.read(&m_coffHeader, sizeof(COFFHeader))) {
            return false;
        }

        if (m_coffHeader.optionalHeaderSize >= sizeof(PEOptionalHeader32)) {
            if (!reader.read(&m_optHeader, sizeof(PEOptionalHeader32))) {
                return false;
            }

            if (m_optHeader.magic != 0x10B) { // PE32
                printf("Not a PE32 file\n");
                return false;
            }

            m_baseAddress = m_optHeader.imageBase;
            m_entryPoint = m_baseAddress + m_optHeader.addressOfEntryPoint;
        }

        return true;
    }

    bool PEImage::loadSections(const uint8_t* data, size_t size) {
        BinaryReader reader(data, size);

        size_t sectionOffset = m_dosHeader.newHeaderOffset + 4 + sizeof(COFFHeader) + m_coffHeader.optionalHeaderSize;
        if (!reader.seek(sectionOffset)) {
            return false;
        }

        for (int i = 0; i < m_coffHeader.numberOfSections; i++) {
            PESectionHeader sectionHeader;
            if (!reader.read(&sectionHeader, sizeof(PESectionHeader))) {
                return false;
            }

            char name[9] = { 0 };
            std::memcpy(name, sectionHeader.name, 8);

            bool readable = (sectionHeader.characteristics & IMAGE_SCN_MEM_READ) != 0;
            bool writable = (sectionHeader.characteristics & IMAGE_SCN_MEM_WRITE) != 0;
            bool executable = (sectionHeader.characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;


            // THIS WONT BE PERMANENT, it's just so i can get things going without wasting time with this
            // TODO: remove this, i can just load the file directly in memory without parsing sections
            //if(strcmp(name,".text") == 0)
            //{
            //    printf("PEImage::loadSections %s", "WARNING-- USING HARDCODED OFFSET AND SIZE");
            //    sectionHeader.pointerToRawData = sectionHeader.virtualAddress;
            //    sectionHeader.sizeOfRawData = 0xFDE4c;
            //    sectionHeader.virtualSize = 0xFDE4c;
            //}
            auto section = std::make_unique<Section>(
                name,
                sectionHeader.virtualAddress,
                sectionHeader.virtualSize,
                sectionHeader.pointerToRawData,
                sectionHeader.sizeOfRawData,
                readable, writable, executable
            );
            
            printf("  Section '%s': VA=0x%08X, Size=0x%08X\n",
                name, sectionHeader.virtualAddress, sectionHeader.virtualSize);

            m_sections.push_back(std::move(section));
        }

        return true;
    }

    void PEImage::buildMemoryImage(const uint8_t* data, size_t size) {
        // Allocate memory for the image
        m_memorySize = m_optHeader.sizeOfImage;
        m_memoryData = new uint8_t[m_memorySize];
        std::memset(m_memoryData, 0, m_memorySize);



        // copy everything
        std::memcpy(m_memoryData, data, size);


        // Copy headers
        //size_t headerSize = m_optHeader.sizeOfHeaders;
        //if (headerSize > size) headerSize = size;
        //std::memcpy(m_memoryData, data, headerSize);

        // Copy sections
        //for (const auto& section : m_sections) {
        //    if (section->getPhysicalSize() > 0) {
        //        size_t copySize = section->getPhysicalSize();
        //        if (section->getVirtualSize() < copySize) {
        //            copySize = section->getVirtualSize();
        //        }
        //
        //        if (section->getPhysicalAddress() + copySize <= size) {
        //            std::memcpy(
        //                m_memoryData + section->getVirtualAddress(),
        //                data + section->getPhysicalAddress(),
        //                copySize
        //            );
        //        }
        //    }
        //}
    }

    bool PEImage::loadImports(const uint8_t* data, size_t size) {
        // Import loading would require parsing the import directory
        // This is a simplified version - full implementation would parse IMAGE_IMPORT_DESCRIPTOR

        if (m_optHeader.numberOfRvaAndSizes > 1) {
            auto& importDir = m_optHeader.dataDirectories[1];
            if (importDir.VirtualAddress != 0 && importDir.Size != 0) {
                // Would parse import descriptors here
                printf("Import directory found at RVA 0x%08X\n", importDir.VirtualAddress);
            }
        }

        return true;
    }

    // XEX Image implementation
    XEXImage::~XEXImage() {
        if (m_memoryData) {
            free(m_memoryData);
        }
    }

    void XEXImage::swap16(uint16_t* val) {
        *val = ((*val & 0xFF00) >> 8) | ((*val & 0x00FF) << 8);
    }

    void XEXImage::swap32(uint32_t* val) {
        *val = ((*val & 0xFF000000) >> 24) |
            ((*val & 0x00FF0000) >> 8) |
            ((*val & 0x0000FF00) << 8) |
            ((*val & 0x000000FF) << 24);
    }

    bool XEXImage::load(const uint8_t* data, size_t size) {
        printf("Loading XEX2 image...\n");

        if (!loadHeaders(data, size)) {
            printf("Failed to load XEX headers\n");
            return false;
        }

        if (!decompressImage(data, size)) {
            printf("Failed to decompress XEX image\n");
            return false;
        }

        if (!extractPEImage()) {
            printf("Failed to extract PE from XEX\n");
            return false;
        }

        if (!processImports()) {
            printf("Failed to process XEX imports\n");
            return false;
        }

        printf("XEX2 image loaded successfully\n");
        printf("  Base address: 0x%08X\n", m_baseAddress);
        printf("  Entry point: 0x%08X\n", m_entryPoint);
        printf("  Sections: %zu\n", m_sections.size());
        printf("  Imports: %zu\n", m_imports.size());

        return true;
    }

    bool XEXImage::loadHeaders(const uint8_t* data, size_t size) {
        BinaryReader reader(data, size);

        // Read XEX header
        if (!reader.read(&m_header, sizeof(XEXHeader))) {
            return false;
        }

        // Swap endianness (XEX is big-endian)
        swap32(&m_header.magic);
        swap32(&m_header.moduleFlags);
        swap32(&m_header.exeOffset);
        swap32(&m_header.certificateOffset);
        swap32(&m_header.headerCount);

        if (!m_header.isValid()) {
            printf("Invalid XEX2 header\n");
            return false;
        }

        // Parse optional headers
        if (!parseOptionalHeaders(data, size)) {
            return false;
        }

        // Load loader info
        if (!loadLoaderInfo(data, size)) {
            return false;
        }

        // Decrypt session key
        decryptSessionKey();

        return true;
    }

    bool XEXImage::loadLoaderInfo(const uint8_t* data, size_t size) {
        // Create a reader positioned at the certificate offset
        if (m_header.certificateOffset >= size) {
            printf("Certificate offset exceeds file size\n");
            return false;
        }

        const uint8_t* loaderData = data + m_header.certificateOffset;
        size_t remaining = size - m_header.certificateOffset;

        if (remaining < sizeof(XEXLoaderInfo)) {
            printf("Insufficient data for loader info\n");
            return false;
        }

        // Copy loader info
        memcpy(&m_loaderInfo, loaderData, sizeof(XEXLoaderInfo));

        // Swap endianness for all fields
        swap32(&m_loaderInfo.headerSize);
        swap32(&m_loaderInfo.imageSize);
        swap32(&m_loaderInfo.unkLength);
        swap32(&m_loaderInfo.imageFlags);
        swap32(&m_loaderInfo.loadAddress);
        swap32(&m_loaderInfo.importTableCount);
        swap32(&m_loaderInfo.exportTable);
        swap32(&m_loaderInfo.gameRegions);
        swap32(&m_loaderInfo.mediaFlags);

        printf("  Image size: 0x%08X\n", m_loaderInfo.imageSize);
        printf("  Load address: 0x%08X\n", m_loaderInfo.loadAddress);

        // Load sections if present
        const uint8_t* sectionData = data + m_header.certificateOffset + 0x180;
        if (m_header.certificateOffset + 0x180 + 4 <= size) {
            uint32_t sectionCount = *(uint32_t*)sectionData;
            swap32(&sectionCount);

            sectionData += 4;
            size_t sectionSize = sectionCount * sizeof(XEXSection);

            if (m_header.certificateOffset + 0x180 + 4 + sectionSize <= size) {
                m_xexSections.resize(sectionCount);
                for (uint32_t i = 0; i < sectionCount; i++) {
                    memcpy(&m_xexSections[i], sectionData + i * sizeof(XEXSection), sizeof(XEXSection));
                    swap32(&m_xexSections[i].info);
                }
                printf("  Loaded %u XEX sections\n", sectionCount);
            }
        }

        return true;
    }

    void XEXImage::decryptSessionKey() {
        // XEX2 retail key
        static const uint8_t retailKey[16] = {
            0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28, 0xFD, 0xC3,
            0x40, 0x58, 0x3F, 0xBB, 0x08, 0x96, 0xBF, 0x91
        };

        // XEX2 devkit key (all zeros)
        static const uint8_t devkitKey[16] = { 0 };

        // Determine which key to use based on execution info
        const uint8_t* keyToUse = devkitKey;
        if (m_executionInfo.titleId != 0) {
            keyToUse = retailKey;
            printf("  Using retail key for decryption\n");
        }
        else {
            printf("  Using devkit key for decryption\n");
        }

        // Note: Actual AES decryption would happen here
        // For now, just copy the file key as session key (placeholder)
        memcpy(m_sessionKey, m_loaderInfo.fileKey, 16);

        printf("  Session key decrypted\n");
    }

    bool XEXImage::decompressBasic(const uint8_t* data, size_t size) {
        // Calculate uncompressed size
        uint32_t uncompressedSize = 0;
        for (const auto& block : m_compressionBlocks) {
            uncompressedSize += block.dataSize + block.zeroSize;
        }

        if (uncompressedSize > 128 * 1024 * 1024) { // 128MB sanity check
            printf("Uncompressed size too large: 0x%08X\n", uncompressedSize);
            return false;
        }

        // Allocate memory for uncompressed data
        m_memoryData = (uint8_t*)malloc(uncompressedSize);
        if (!m_memoryData) {
            printf("Failed to allocate %u bytes for decompression\n", uncompressedSize);
            return false;
        }
        m_memorySize = uncompressedSize;
        memset(m_memoryData, 0, m_memorySize);

        // Source data starts at exe offset
        const uint8_t* src = data + m_header.exeOffset;
        uint8_t* dst = m_memoryData;

        // Process each compression block
        for (const auto& block : m_compressionBlocks) {
            // Copy data portion
            if (block.dataSize > 0) {
                if (m_encryptionType == XEXEncryptionType::None) {
                    // No encryption, direct copy
                    memcpy(dst, src, block.dataSize);
                }
                else {
                    // With encryption, would decrypt here
                    // For now, just copy (placeholder)
                    memcpy(dst, src, block.dataSize);
                }
                src += block.dataSize;
                dst += block.dataSize;
            }

            // Skip zero portion (already zeroed)
            dst += block.zeroSize;
        }

        printf("  Decompressed %u bytes from basic compression\n", uncompressedSize);
        return true;
    }

    bool XEXImage::decompressNormal(const uint8_t* data, size_t size) {
        printf("Normal compression not yet implemented\n");
        return false;
    }

    bool XEXImage::decompressImage(const uint8_t* data, size_t size) {
        switch (m_compressionType) {
        case XEXCompressionType::None:
            // No compression - copy directly
            m_memorySize = m_loaderInfo.imageSize;
            m_memoryData = (uint8_t*)malloc(m_memorySize);
            if (!m_memoryData) {
                return false;
            }
            memcpy(m_memoryData, data + m_header.exeOffset, m_memorySize);
            printf("  No compression - copied %zu bytes\n", m_memorySize);
            return true;

        case XEXCompressionType::Basic:
            return decompressBasic(data, size);

        case XEXCompressionType::Normal:
            return decompressNormal(data, size);

        case XEXCompressionType::Delta:
            printf("Delta compression not supported\n");
            return false;

        default:
            printf("Unknown compression type: %d\n", (int)m_compressionType);
            return false;
        }
    }

    bool XEXImage::decryptData(uint8_t* dest, const uint8_t* src, size_t size) {
        // Placeholder for AES decryption
        // In real implementation, would use session key to decrypt
        memcpy(dest, src, size);
        return true;
    }

    bool XEXImage::extractPEImage() {
        // PE header should be at the start of decompressed data
        if (m_memorySize < sizeof(DOSHeader)) {
            printf("Memory too small for DOS header\n");
            return false;
        }

        // Check for MZ signature
        DOSHeader* dosHeader = (DOSHeader*)m_memoryData;
        if (dosHeader->signature[0] != 'M' || dosHeader->signature[1] != 'Z') {
            printf("Invalid DOS header in XEX PE\n");
            return false;
        }

        // Get PE header offset
        if (dosHeader->newHeaderOffset >= m_memorySize) {
            printf("PE header offset exceeds memory size\n");
            return false;
        }

        // Check PE signature
        uint32_t* peSignature = (uint32_t*)(m_memoryData + dosHeader->newHeaderOffset);
        if (*peSignature != 0x00004550) {
            printf("Invalid PE signature in XEX\n");
            return false;
        }

        // Parse COFF header
        COFFHeader* coffHeader = (COFFHeader*)(m_memoryData + dosHeader->newHeaderOffset + 4);

        // Parse optional header
        PEOptionalHeader32* optHeader = (PEOptionalHeader32*)((uint8_t*)coffHeader + sizeof(COFFHeader));

        // Extract sections
        PESectionHeader* sectionHeaders = (PESectionHeader*)((uint8_t*)optHeader + coffHeader->optionalHeaderSize);

        for (int i = 0; i < coffHeader->numberOfSections; i++) {
            PESectionHeader* section = &sectionHeaders[i];

            char name[9] = { 0 };
            memcpy(name, section->name, 8);

            bool readable = (section->characteristics & IMAGE_SCN_MEM_READ) != 0;
            bool writable = (section->characteristics & IMAGE_SCN_MEM_WRITE) != 0;
            bool executable = (section->characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;

            auto sec = std::make_unique<Section>(
                name,
                section->virtualAddress,
                section->virtualSize,
                section->pointerToRawData,
                section->sizeOfRawData,
                readable, writable, executable
            );

            m_sections.push_back(std::move(sec));
            printf("  PE Section '%s': VA=0x%08X, Size=0x%08X\n",
                name, section->virtualAddress, section->virtualSize);
        }

        // Set base address and entry point from XEX headers
        if (m_baseAddress == 0) {
            m_baseAddress = optHeader->imageBase;
        }
        if (m_entryPoint == 0) {
            m_entryPoint = m_baseAddress + optHeader->addressOfEntryPoint;
        }

        printf("  PE extraction complete\n");
        return true;
    }

    bool XEXImage::processImports() {
        // Process import records
        for (size_t i = 0; i < m_importRecords.size(); i++) {
            uint32_t recordAddr = m_importRecords[i];

            // Calculate offset in memory
            uint32_t offset = recordAddr - m_baseAddress;
            if (offset >= m_memorySize) {
                printf("Import record address out of bounds: 0x%08X\n", recordAddr);
                continue;
            }

            // Read the import value
            uint32_t value = *(uint32_t*)(m_memoryData + offset);
            swap32(&value);

            // Extract import information
            uint8_t type = (value >> 24) & 0xFF;
            uint8_t libIndex = (value >> 16) & 0xFF;
            uint16_t ordinal = value & 0xFFFF;

            if (libIndex >= m_libraryNames.size()) {
                printf("Invalid library index: %u\n", libIndex);
                continue;
            }

            // Determine library type
            XboxLibrary lib = XboxLibrary::XboxKrnl;
            const std::string& libName = m_libraryNames[libIndex];
            if (libName.find("xam") != std::string::npos) {
                lib = XboxLibrary::Xam;
            }
            else if (libName.find("xbdm") != std::string::npos) {
                lib = XboxLibrary::Xbdm;
            }
            else if (libName.find("xapi") != std::string::npos) {
                lib = XboxLibrary::Xapi;
            }

            // Create import based on type
            ImportType impType = (type == 0) ? ImportType::Variable : ImportType::Function;

            char importName[256];
            snprintf(importName, sizeof(importName), "%s_%u", libName.c_str(), ordinal);

            auto import = std::make_unique<Import>(lib, impType, importName, ordinal);
import->tableAddr = recordAddr;
import->funcImportAddr = recordAddr;

            m_imports.push_back(std::move(import));

            printf("  Import: %s (type=%s, lib=%s, ordinal=%u)\n",
                importName,
                (impType == ImportType::Function) ? "func" : "var",
                libName.c_str(),
                ordinal);
        }

        printf("  Processed %zu imports\n", m_imports.size());
        return true;
    }

    bool XEXImage::parseOptionalHeaders(const uint8_t* data, size_t size) {
        size_t offset = sizeof(XEXHeader);

        for (uint32_t i = 0; i < m_header.headerCount; i++) {
            if (offset + sizeof(XEXOptionalHeaderEntry) > size) {
                printf("Optional header %u exceeds file bounds\n", i);
                return false;
            }

            XEXOptionalHeaderEntry entry;
            memcpy(&entry, data + offset, sizeof(entry));
            swap32(&entry.key);
            swap32(&entry.offset);

            offset += sizeof(XEXOptionalHeaderEntry);
            m_optionalHeaders.push_back(entry);

            // Process specific headers
            switch (entry.key) {
            case XEXHeaderKey::BaseAddress:
                m_baseAddress = entry.offset;
                printf("  Base address: 0x%08X\n", m_baseAddress);
                break;

            case XEXHeaderKey::EntryPoint:
                m_entryPoint = entry.offset;
                printf("  Entry point: 0x%08X\n", m_entryPoint);
                break;

            case XEXHeaderKey::ExecutionInfo:
                if (entry.offset + sizeof(XEXExecutionInfo) <= size) {
                    memcpy(&m_executionInfo, data + entry.offset, sizeof(XEXExecutionInfo));
                    swap32(&m_executionInfo.mediaId);
                    swap32(&m_executionInfo.version);
                    swap32(&m_executionInfo.baseVersion);
                    swap32(&m_executionInfo.titleId);
                    swap32(&m_executionInfo.savegameId);
                    printf("  Title ID: 0x%08X\n", m_executionInfo.titleId);
                }
                break;

            case XEXHeaderKey::FileFormatInfo:
                if (entry.offset + sizeof(XEXFileCompressionInfo) <= size) {
                    XEXFileCompressionInfo compInfo;
                    memcpy(&compInfo, data + entry.offset, sizeof(compInfo));
                    swap16(&compInfo.compressionType);
                    swap16(&compInfo.encryptionType);

                    m_compressionType = (XEXCompressionType)compInfo.compressionType;
                    m_encryptionType = (XEXEncryptionType)compInfo.encryptionType;

                    printf("  Compression: %d, Encryption: %d\n",
                        compInfo.compressionType, compInfo.encryptionType);

                    // Load compression blocks if basic compression
                    if (m_compressionType == XEXCompressionType::Basic) {
                        size_t blockOffset = entry.offset + sizeof(XEXFileCompressionInfo);
                        uint32_t blockCount = (entry.key & 0xFF) * 4;
                        if (blockCount > sizeof(XEXFileCompressionInfo)) {
                            blockCount = (blockCount - sizeof(XEXFileCompressionInfo)) / sizeof(XEXBasicCompressionBlock);

                            for (uint32_t j = 0; j < blockCount; j++) {
                                if (blockOffset + sizeof(XEXBasicCompressionBlock) > size) break;

                                XEXBasicCompressionBlock block;
                                memcpy(&block, data + blockOffset, sizeof(block));
                                swap32(&block.dataSize);
                                swap32(&block.zeroSize);

                                m_compressionBlocks.push_back(block);
                                blockOffset += sizeof(XEXBasicCompressionBlock);
                            }
                            printf("  Loaded %u compression blocks\n", blockCount);
                        }
                    }
                }
                break;

            case XEXHeaderKey::ImportLibraries:
            {
                // Read import library header
                XEXImportLibraryHeader libHeader;
                if (entry.offset + sizeof(libHeader) > size) break;

                memcpy(&libHeader, data + entry.offset, sizeof(libHeader));
                swap32(&libHeader.size);
                swap32(&libHeader.importId);
                swap32(&libHeader.version);
                swap32(&libHeader.minVersion);
                swap16(&libHeader.nameIndex);
                swap16(&libHeader.recordCount);

                // Read string table
                size_t stringTableOffset = entry.offset + sizeof(libHeader);
                size_t stringTableSize = libHeader.size - sizeof(libHeader) - (libHeader.recordCount * 4);

                if (stringTableOffset + stringTableSize > size) break;

                const char* stringTable = (const char*)(data + stringTableOffset);

                // Extract library names
                size_t strOffset = 0;
                while (strOffset < stringTableSize) {
                    const char* libName = stringTable + strOffset;
                    if (*libName) {
                        m_libraryNames.push_back(libName);
                        printf("  Import library: %s\n", libName);
                    }
                    strOffset += strlen(libName) + 1;
                    // Align to 4 bytes
                    if (strOffset % 4) {
                        strOffset += 4 - (strOffset % 4);
                    }
                }

                // Read import records
                size_t recordOffset = stringTableOffset + stringTableSize;
                for (uint16_t j = 0; j < libHeader.recordCount; j++) {
                    if (recordOffset + 4 > size) break;

                    uint32_t record;
                    memcpy(&record, data + recordOffset, 4);
                    swap32(&record);
                    m_importRecords.push_back(record);
                    recordOffset += 4;
                }

                printf("  Loaded %u import records\n", libHeader.recordCount);
            }
            break;

            case XEXHeaderKey::DefaultStackSize:
                printf("  Stack size: 0x%08X\n", entry.offset);
                break;

            case XEXHeaderKey::DefaultHeapSize:
                printf("  Heap size: 0x%08X\n", entry.offset);
                break;
            }
        }

        return true;
    }
}