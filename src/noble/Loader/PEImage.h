#pragma once
#include "ImageLoader.h"
#include <windows.h>



namespace XLoader
{


    // PE file structures
#pragma pack(push, 1)
    struct DOSHeader {
        char signature[2];  // MZ
        uint16_t lastSize;
        uint16_t nBlocks;
        uint16_t nReloc;
        uint16_t headerSize;
        uint16_t minAlloc;
        uint16_t maxAlloc;
        uint16_t ss;
        uint16_t sp;
        uint16_t checksum;
        uint16_t ip;
        uint16_t cs;
        uint16_t relocPos;
        uint16_t overlay;
        uint16_t reserved1[4];
        uint16_t oemId;
        uint16_t oemInfo;
        uint16_t reserved2[10];
        uint32_t newHeaderOffset;

        bool isValid() const {
            return signature[0] == 'M' && signature[1] == 'Z';
        }
    };

    struct COFFHeader {
        uint16_t machine;
        uint16_t numberOfSections;
        uint32_t timeDateStamp;
        uint32_t symbolTablePtr;
        uint32_t numberOfSymbols;
        uint16_t optionalHeaderSize;
        uint16_t characteristics;
    };

    struct PEOptionalHeader32 {
        uint16_t magic;
        uint8_t majorLinkerVersion;
        uint8_t minorLinkerVersion;
        uint32_t sizeOfCode;
        uint32_t sizeOfInitializedData;
        uint32_t sizeOfUninitializedData;
        uint32_t addressOfEntryPoint;
        uint32_t baseOfCode;
        uint32_t baseOfData;
        uint32_t imageBase;
        uint32_t sectionAlignment;
        uint32_t fileAlignment;
        uint16_t majorOSVersion;
        uint16_t minorOSVersion;
        uint16_t majorImageVersion;
        uint16_t minorImageVersion;
        uint16_t majorSubsystemVersion;
        uint16_t minorSubsystemVersion;
        uint32_t reserved;
        uint32_t sizeOfImage;
        uint32_t sizeOfHeaders;
        uint32_t checksum;
        uint16_t subsystem;
        uint16_t dllCharacteristics;
        uint32_t sizeOfStackReserve;
        uint32_t sizeOfStackCommit;
        uint32_t sizeOfHeapReserve;
        uint32_t sizeOfHeapCommit;
        uint32_t loaderFlags;
        uint32_t numberOfRvaAndSizes;
        IMAGE_DATA_DIRECTORY dataDirectories[16];
    };

    struct PESectionHeader {
        char name[8];
        uint32_t virtualSize;
        uint32_t virtualAddress;
        uint32_t sizeOfRawData;
        uint32_t pointerToRawData;
        uint32_t pointerToRelocations;
        uint32_t pointerToLinenumbers;
        uint16_t numberOfRelocations;
        uint16_t numberOfLinenumbers;
        uint32_t characteristics;
    };
#pragma pack(pop)

    class PEImage : public IImage {
    public:
        PEImage() : m_baseAddress(0), m_entryPoint(0), m_memoryData(nullptr), m_memorySize(0) {}
        ~PEImage() override;

        bool load(const uint8_t* data, size_t size) override;
        uint32_t getBaseAddress() const override { return m_baseAddress; }
        uint32_t getEntryPoint() const override { return m_entryPoint; }
        const uint8_t* getMemoryData() const override { return m_memoryData; }
        size_t getMemorySize() const override { return m_memorySize; }
        const std::vector<std::unique_ptr<XLoader::Section>>& getSections() const override { return m_sections; }
        const std::vector<std::unique_ptr<Import>>& getImports() const override { return m_imports; }

    private:
        bool loadHeaders(const uint8_t* data, size_t size);
        bool loadSections(const uint8_t* data, size_t size);
        bool loadImports(const uint8_t* data, size_t size);
        void buildMemoryImage(const uint8_t* data, size_t size);

        uint32_t m_baseAddress;
        uint32_t m_entryPoint;
        uint8_t* m_memoryData;
        size_t m_memorySize;

        DOSHeader m_dosHeader;
        COFFHeader m_coffHeader;
        PEOptionalHeader32 m_optHeader;

        std::vector<std::unique_ptr<Section>> m_sections;
        std::vector<std::unique_ptr<Import>> m_imports;
    };

}