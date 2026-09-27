#include "AES/AES.h"
#include "ImageLoader.h"

#include "Logger.h"
#include "PEImage.h"
#include "XEXImage.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>

namespace XLoader {

class BinaryReader {
public:
    BinaryReader(const uint8_t* data, size_t size) : m_data(data), m_size(size), m_offset(0) {}

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
        LOG_FATAL(": \"{}\" no such file or directory\n", path);
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
        uint32_t xex2Magic = ((magic & 0xFF000000) >> 24) | ((magic & 0x00FF0000) >> 8)
                             | ((magic & 0x0000FF00) << 8) | ((magic & 0x000000FF) << 24);

        if (xex2Magic == 0x58455832) {  // 'XEX2'
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

    if (peSignature != 0x00004550) {  // "PE\0\0"
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

        if (m_optHeader.magic != 0x10B) {  // PE32
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

    size_t sectionOffset
        = m_dosHeader.newHeaderOffset + 4 + sizeof(COFFHeader) + m_coffHeader.optionalHeaderSize;
    if (!reader.seek(sectionOffset)) {
        return false;
    }

    for (int i = 0; i < m_coffHeader.numberOfSections; i++) {
        PESectionHeader sectionHeader;
        if (!reader.read(&sectionHeader, sizeof(PESectionHeader))) {
            return false;
        }

        char name[9] = {0};
        std::memcpy(name, sectionHeader.name, 8);

        bool readable = (sectionHeader.characteristics & IMAGE_SCN_MEM_READ) != 0;
        bool writable = (sectionHeader.characteristics & IMAGE_SCN_MEM_WRITE) != 0;
        bool executable = (sectionHeader.characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;

        // THIS WONT BE PERMANENT, it's just so i can get things going without wasting time with this
        // TODO: remove this, i can just load the file directly in memory without parsing sections
        // if(strcmp(name,".text") == 0)
        //{
        //    printf("PEImage::loadSections %s", "WARNING-- USING HARDCODED OFFSET AND SIZE");
        //    sectionHeader.pointerToRawData = sectionHeader.virtualAddress;
        //    sectionHeader.sizeOfRawData = 0xFDE4c;
        //    sectionHeader.virtualSize = 0xFDE4c;
        //}
        auto section = std::make_unique<Section>(name, sectionHeader.virtualAddress,
                                                 sectionHeader.virtualSize, sectionHeader.pointerToRawData,
                                                 sectionHeader.sizeOfRawData, readable, writable, executable);

        printf("  Section '%s': VA=0x%08X, Size=0x%08X\n", name, sectionHeader.virtualAddress,
               sectionHeader.virtualSize);

        m_sections.push_back(std::move(section));
    }

    return true;
}

void PEImage::buildMemoryImage(const uint8_t* data, size_t size) {
    // Allocate memory for the image
    m_memorySize = m_optHeader.sizeOfImage;
    m_memoryData = new uint8_t[m_memorySize];
    std::memset(m_memoryData, 0, m_memorySize);

    // The file can be larger than the virtual image.
    std::memcpy(m_memoryData, data, (std::min)(size, m_memorySize));
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

void swap16(uint16_t* val) {
    *val = ((*val & 0xFF00) >> 8) | ((*val & 0x00FF) << 8);
}

void swap32(uint32_t* val) {
    *val = ((*val & 0xFF000000) >> 24) | ((*val & 0x00FF0000) >> 8) | ((*val & 0x0000FF00) << 8)
           | ((*val & 0x000000FF) << 24);
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
    static const uint8_t retailKey[16]
        = {0x20, 0xB1, 0x85, 0xA5, 0x9D, 0x28, 0xFD, 0xC3, 0x40, 0x58, 0x3F, 0xBB, 0x08, 0x96, 0xBF, 0x91};

    // XEX2 devkit key (all zeros)
    static const uint8_t devkitKey[16] = {0};

    // Determine which key to use based on execution info
    const uint8_t* keyToUse = devkitKey;
    if (m_executionInfo.titleId != 0) {
        keyToUse = retailKey;
        printf("  Using retail key for decryption\n");
    } else {
        printf("  Using devkit key for decryption\n");
    }

    uint32_t roundKeys[4 * (MAXNR + 1)];
    int rounds = rijndaelKeySetupDec(roundKeys, keyToUse, 128);
    rijndaelDecrypt(roundKeys, rounds, m_loaderInfo.fileKey, m_sessionKey);

    printf("  Session key decrypted\n");
}

bool XEXImage::decompressBasic(const uint8_t* data, size_t size) {
    // Calculate uncompressed size
    size_t uncompressedSize = 0;

    for (const auto& block : m_compressionBlocks) {
        if (block.dataSize > 128 * 1024 * 1024 - uncompressedSize
            || block.zeroSize > 128 * 1024 * 1024 - uncompressedSize - block.dataSize)
            return false;

        uncompressedSize += block.dataSize + block.zeroSize;
    }

    // Source data starts at exe offset
    if (m_header.exeOffset > size)
        return false;

    size_t sourceBytes = 0;
    for (const auto& block : m_compressionBlocks) {
        if (block.dataSize > size - m_header.exeOffset - sourceBytes)
            return false;

        sourceBytes += block.dataSize;
    }

    if (m_encryptionType == XEXEncryptionType::Normal && (sourceBytes % 16) != 0)
        return false;

    if (m_encryptionType != XEXEncryptionType::None && m_encryptionType != XEXEncryptionType::Normal)
        return false;

    m_memoryData = (uint8_t*)malloc(uncompressedSize);

    if (!m_memoryData)
        return false;

    m_memorySize = uncompressedSize;
    memset(m_memoryData, 0, m_memorySize);

    const uint8_t* src = data + m_header.exeOffset;
    uint8_t* dst = m_memoryData;
    uint32_t roundKeys[4 * (MAXNR + 1)];

    const int rounds = rijndaelKeySetupDec(roundKeys, m_sessionKey, 128);
    uint8_t ivec[16] = {};

    // Process each compression block
    for (const auto& block : m_compressionBlocks) {
        // Copy data portion
        if (block.dataSize > 0) {
            if (m_encryptionType == XEXEncryptionType::None) {
                // No encryption, direct copy
                memcpy(dst, src, block.dataSize);

            } else {
                if (block.dataSize % 16)
                    return false;

                for (size_t n = 0; n < block.dataSize; n += 16) {
                    rijndaelDecrypt(roundKeys, rounds, src + n, dst + n);

                    for (size_t j = 0; j < 16; ++j) {
                        dst[n + j] ^= ivec[j];
                        ivec[j] = src[n + j];
                    }
                }
            }
            src += block.dataSize;
            dst += block.dataSize;
        }

        // Skip zero portion (already zeroed)
        dst += block.zeroSize;
    }

    printf("  Decompressed %zu bytes from basic compression\n", uncompressedSize);
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
        if (m_header.exeOffset > size)
            return false;

        m_memorySize = size - m_header.exeOffset;
        m_memoryData = (uint8_t*)malloc(m_memorySize);
        if (!m_memoryData) {
            return false;
        }
        if (!decryptData(m_memoryData, data + m_header.exeOffset, m_memorySize))
            return false;

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
    if (m_encryptionType == XEXEncryptionType::None) {
        memcpy(dest, src, size);
        return true;
    }

    if (m_encryptionType != XEXEncryptionType::Normal || size % 16)
        return false;

    uint32_t roundKeys[4 * (MAXNR + 1)];
    const int rounds = rijndaelKeySetupDec(roundKeys, m_sessionKey, 128);

    uint8_t ivec[16] = {};
    for (size_t n = 0; n < size; n += 16) {
        rijndaelDecrypt(roundKeys, rounds, src + n, dest + n);

        for (size_t j = 0; j < 16; ++j) {
            dest[n + j] ^= ivec[j];
            ivec[j] = src[n + j];
        }
    }
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
    PESectionHeader* sectionHeaders
        = (PESectionHeader*)((uint8_t*)optHeader + coffHeader->optionalHeaderSize);

    for (int i = 0; i < coffHeader->numberOfSections; i++) {
        PESectionHeader* section = &sectionHeaders[i];

        char name[9] = {0};
        memcpy(name, section->name, 8);

        bool readable = (section->characteristics & IMAGE_SCN_MEM_READ) != 0;
        bool writable = (section->characteristics & IMAGE_SCN_MEM_WRITE) != 0;
        bool executable = (section->characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;

        auto sec = std::make_unique<Section>(name, section->virtualAddress, section->virtualSize,
                                             section->pointerToRawData, section->sizeOfRawData, readable,
                                             writable, executable);

        m_sections.push_back(std::move(sec));
        printf("  PE Section '%s': VA=0x%08X, Size=0x%08X\n", name, section->virtualAddress,
               section->virtualSize);
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
        if (recordAddr < m_baseAddress || offset > m_memorySize || m_memorySize - offset < 4) {
            printf("Import record address out of bounds: 0x%08X\n", recordAddr);
            continue;
        }

        // Read the import value
        uint32_t value;
        memcpy(&value, m_memoryData + offset, sizeof(value));
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
        } else if (libName.find("xbdm") != std::string::npos) {
            lib = XboxLibrary::Xbdm;
        } else if (libName.find("xapi") != std::string::npos) {
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

        printf("  Import: %s (type=%s, lib=%s, ordinal=%u)\n", importName,
               (impType == ImportType::Function) ? "func" : "var", libName.c_str(), ordinal);
    }

    printf("  Processed %zu imports\n", m_imports.size());
    return true;
}

bool XEXImage::parseOptionalHeaders(const uint8_t* data, size_t size) {
    size_t offset = sizeof(XEXHeader);

    for (uint32_t i = 0; i < m_header.headerCount; i++) {
        if (offset > size || size - offset < 8) {
            printf("Optional header %u exceeds file bounds\n", i);
            return false;
        }

        // extract optional header
        XEXOptionalHeaderEntry entry(data, offset);
        offset += 8;

        // get lenght
        switch (entry.mKey & 0xFF) {
        case 0x00:
        case 0x01:
            entry.mValue = entry.mOffset;
            entry.mOffset = 0;
            break;

        case 0xFF: {
            if (entry.mOffset > size || size - entry.mOffset < 4)
                return false;
            entry.mlength = *((uint32_t*)((size_t)data + entry.mOffset));
            entry.mOffset += 4;
            swap32(&entry.mlength);

            if (entry.mOffset > size || entry.mlength > size - entry.mOffset) {
                printf("Optional header %u exceeds file bounds\n", i);
                return false;
            }

            break;
        }
        default:
            entry.mlength = (entry.mKey & 0xFF) * 4;
            if (entry.mOffset > size || entry.mlength > size - entry.mOffset)
                return false;
            break;
        }

        m_optionalHeaders.push_back(entry);

        // Process specific headers
        switch (entry.mKey) {
        case XEXHeaderKey::BaseAddress:
            m_baseAddress = entry.mValue;
            printf("  Base address: 0x%08X\n", m_baseAddress);
            break;

        case XEXHeaderKey::EntryPoint:
            m_entryPoint = entry.mValue;
            printf("  Entry point: 0x%08X\n", m_entryPoint);
            break;

        case XEXHeaderKey::ExecutionInfo:
            if (entry.mOffset <= size && sizeof(XEXExecutionInfo) <= size - entry.mOffset) {
                memcpy(&m_executionInfo, data + entry.mOffset, sizeof(XEXExecutionInfo));
                swap32(&m_executionInfo.mediaId);
                swap32(&m_executionInfo.version);
                swap32(&m_executionInfo.baseVersion);
                swap32(&m_executionInfo.titleId);
                swap32(&m_executionInfo.savegameId);
                printf("  Title ID: 0x%08X\n", m_executionInfo.titleId);
            }
            break;

        case XEXHeaderKey::FileFormatInfo:
            if (entry.mOffset <= size && sizeof(XEXFileCompressionInfo) <= size - entry.mOffset) {
                XEXFileCompressionInfo compInfo(data, entry.mOffset);

                m_compressionType = (XEXCompressionType)compInfo.compressionType;
                m_encryptionType = (XEXEncryptionType)compInfo.encryptionType;

                printf("  Compression: %d, Encryption: %d\n", compInfo.compressionType,
                       compInfo.encryptionType);

                // Load compression blocks if basic compression
                if (m_compressionType == XEXCompressionType::Basic) {
                    size_t blockOffset = entry.mOffset + sizeof(XEXFileCompressionInfo);
                    uint32_t blockCount = entry.mlength >= 8 ? (entry.mlength - 8) / 8 : 0;

                    for (uint32_t i = 0; i < blockCount; ++i) {
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
            break;

        case XEXHeaderKey::ImportLibraries: {
            if (entry.mlength < 8 || entry.mOffset > size || entry.mlength > size - entry.mOffset)
                return false;

            BinaryReader imports(data, size);
            imports.seek(entry.mOffset);
            uint32_t stringTableSize, libraryCount;

            if (!imports.read(&stringTableSize, 4) || !imports.read(&libraryCount, 4))
                return false;

            swap32(&stringTableSize);
            swap32(&libraryCount);
            const size_t end = entry.mOffset + entry.mlength;

            if (stringTableSize > end - imports.tell())
                return false;

            const char* stringTable = reinterpret_cast<const char*>(imports.current());
            imports.seek(imports.tell() + stringTableSize);

            for (uint32_t j = 0; j < libraryCount; ++j) {
                XEXImportLibraryHeader libHeader;
                if (imports.tell() > end || sizeof(libHeader) > end - imports.tell()
                    || !imports.read(&libHeader, sizeof(libHeader)))
                    return false;

                swap32(&libHeader.size);
                swap16(&libHeader.nameIndex);
                swap16(&libHeader.recordCount);

                size_t nameOffset = 0;
                uint16_t nameIndex = libHeader.nameIndex & 0xFF;
                for (uint16_t k = 0; k < nameIndex; ++k) {
                    if (nameOffset >= stringTableSize)
                        return false;

                    const void* terminator
                        = memchr(stringTable + nameOffset, 0, stringTableSize - nameOffset);

                    if (!terminator)
                        return false;

                    nameOffset = static_cast<const char*>(terminator) - stringTable + 1;
                    nameOffset = (nameOffset + 3) & ~size_t(3);
                }

                if (nameOffset >= stringTableSize)
                    return false;

                const void* terminator = memchr(stringTable + nameOffset, 0, stringTableSize - nameOffset);

                if (!terminator)
                    return false;

                m_libraryNames.emplace_back(stringTable + nameOffset);
                for (uint16_t k = 0; k < libHeader.recordCount; ++k) {
                    uint32_t record;

                    if (imports.tell() > end || end - imports.tell() < 4 || !imports.read(&record, 4))
                        return false;

                    swap32(&record);
                    m_importRecords.push_back(record);
                }
            }
        } break;

        case XEXHeaderKey::DefaultStackSize:
            printf("  Stack size: 0x%08X\n", entry.mValue);
            break;

        case XEXHeaderKey::DefaultHeapSize:
            printf("  Heap size: 0x%08X\n", entry.mValue);
            break;
        }
    }

    return true;
}
}  // namespace XLoader
