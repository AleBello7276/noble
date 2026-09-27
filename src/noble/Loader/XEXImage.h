#pragma once
#include "ImageLoader.h"
#include <cstdint>
#include <vector>



namespace XLoader
{
    // XEX2 specific enums and structures
    enum class XEXCompressionType : uint16_t {
        None = 0,
        Basic = 1,
        Normal = 2,
        Delta = 3
    };

    enum class XEXEncryptionType : uint16_t {
        None = 0,
        Normal = 1
    };

    enum XEXHeaderKey : uint32_t {
        ResourceInfo = 0x000002FF,
        FileFormatInfo = 0x000003FF,
        BaseAddress = 0x00010201,
        EntryPoint = 0x00010100,
        ImportLibraries = 0x000103FF,
        ExecutionInfo = 0x00040006,
        TLSInfo = 0x00020104,
        DefaultStackSize = 0x00020200,
        DefaultHeapSize = 0x00020401,
        SystemFlags = 0x00030000
    };

#pragma pack(push, 1)
    struct XEXHeader {
        uint32_t magic;           // 'XEX2' = 0x58455832
        uint32_t moduleFlags;
        uint32_t exeOffset;
        uint32_t reserved;
        uint32_t certificateOffset;
        uint32_t headerCount;

        bool isValid() const {
            return magic == 0x58455832;
        }
    };

    struct XEXOptionalHeaderEntry {
        uint32_t key;
        uint32_t offset;
    };

    struct XEXLoaderInfo {
        uint32_t headerSize;
        uint32_t imageSize;
        uint8_t rsaSignature[256];
        uint32_t unkLength;
        uint32_t imageFlags;
        uint32_t loadAddress;
        uint8_t sectionDigest[20];
        uint32_t importTableCount;
        uint8_t importTableDigest[20];
        uint8_t mediaId[16];
        uint8_t fileKey[16];
        uint32_t exportTable;
        uint8_t headerDigest[20];
        uint32_t gameRegions;
        uint32_t mediaFlags;
    };

    struct XEXSection {
        uint32_t info;
        uint8_t digest[20];

        uint32_t getType() const { return info & 0xF; }
        uint32_t getPageCount() const { return info >> 4; }
    };

    struct XEXExecutionInfo {
        uint32_t mediaId;
        uint32_t version;
        uint32_t baseVersion;
        uint32_t titleId;
        uint8_t platform;
        uint8_t executableTable;
        uint8_t discNumber;
        uint8_t discCount;
        uint32_t savegameId;
    };

    struct XEXFileCompressionInfo {
        uint16_t compressionType;
        uint16_t encryptionType;
    };

    struct XEXBasicCompressionBlock {
        uint32_t dataSize;
        uint32_t zeroSize;
    };

    struct XEXImportLibraryHeader {
        uint32_t size;
        uint8_t digest[20];
        uint32_t importId;
        uint32_t version;
        uint32_t minVersion;
        uint16_t nameIndex;
        uint16_t recordCount;
    };
#pragma pack(pop)

    class XEXImage : public IImage {
    public:
        XEXImage() : m_baseAddress(0), m_entryPoint(0), m_memoryData(nullptr), m_memorySize(0) {}
        ~XEXImage() override;

        bool load(const uint8_t* data, size_t size) override;
        uint32_t getBaseAddress() const override { return m_baseAddress; }
        uint32_t getEntryPoint() const override { return m_entryPoint; }
        const uint8_t* getMemoryData() const override { return m_memoryData; }
        size_t getMemorySize() const override { return m_memorySize; }
        const std::vector<std::unique_ptr<Section>>& getSections() const override { return m_sections; }
        const std::vector<std::unique_ptr<Import>>& getImports() const override { return m_imports; }

    private:
        // header loading
        bool loadHeaders(const uint8_t* data, size_t size);
        bool parseOptionalHeaders(const uint8_t* data, size_t size);
        bool loadLoaderInfo(const uint8_t* data, size_t size);

        // decompress
        bool decompressImage(const uint8_t* data, size_t size);
        bool decompressBasic(const uint8_t* data, size_t size);
        bool decompressNormal(const uint8_t* data, size_t size);

        // decrypt
        void decryptSessionKey();
        bool decryptData(uint8_t* dest, const uint8_t* src, size_t size);

        bool extractPEImage();
        bool processImports();

        // util
        static void swap16(uint16_t* val);
        static void swap32(uint32_t* val);

    private:
        uint32_t m_baseAddress;
        uint32_t m_entryPoint;
        uint8_t* m_memoryData;
        size_t m_memorySize;

        XEXHeader m_header;
        XEXLoaderInfo m_loaderInfo;
        XEXExecutionInfo m_executionInfo;

        uint8_t m_sessionKey[16];
        XEXCompressionType m_compressionType;
        XEXEncryptionType m_encryptionType;

        std::vector<XEXOptionalHeaderEntry> m_optionalHeaders;
        std::vector<XEXBasicCompressionBlock> m_compressionBlocks;
        std::vector<XEXSection> m_xexSections;

        std::vector<std::string> m_libraryNames;
        std::vector<uint32_t> m_importRecords;

        std::vector<std::unique_ptr<Section>> m_sections;
        std::vector<std::unique_ptr<Import>> m_imports;
    };
}