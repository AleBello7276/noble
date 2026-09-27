#pragma once
#include <memory>
#include <vector>
#include <string>
#include <cstdint>

class ImageLoader;
class Section;

enum class ImportType {
    Function,
    Variable
};

enum class XboxLibrary {
    XboxKrnl,
    Xam,
    Xbdm,
    Xapi
};

namespace XLoader {

    struct Import {
        XboxLibrary library;
        ImportType type;
        std::string name;
        uint32_t ordinal;
        uint32_t tableAddr;
        uint32_t funcImportAddr;

        Import(XboxLibrary lib, ImportType t, const std::string& n, uint32_t ord)
            : library(lib), type(t), name(n), ordinal(ord),
            tableAddr(0), funcImportAddr(0) {
        }
    };


    class Section {
    public:
        Section(const std::string& name, uint32_t vAddr, uint32_t vSize,
            uint32_t pAddr, uint32_t pSize, bool r, bool w, bool x)
            : m_name(name), m_virtualAddr(vAddr), m_virtualSize(vSize),
            m_physicalAddr(pAddr), m_physicalSize(pSize),
            m_readable(r), m_writable(w), m_executable(x) {
        }

        const std::string& getName() const { return m_name; }
        uint32_t getVirtualAddress() const { return m_virtualAddr; }
        uint32_t getVirtualSize() const { return m_virtualSize; }
        uint32_t getPhysicalAddress() const { return m_physicalAddr; }
        uint32_t getPhysicalSize() const { return m_physicalSize; }
        bool isReadable() const { return m_readable; }
        bool isWritable() const { return m_writable; }
        bool isExecutable() const { return m_executable; }

        bool containsAddress(uint32_t addr) const {
            return addr >= m_virtualAddr && addr < (m_virtualAddr + m_virtualSize);
        }

    private:
        std::string m_name;
        uint32_t m_virtualAddr;
        uint32_t m_virtualSize;
        uint32_t m_physicalAddr;
        uint32_t m_physicalSize;
        bool m_readable;
        bool m_writable;
        bool m_executable;
    };


    class IImage {
    public:
        virtual ~IImage() = default;

        virtual bool load(const uint8_t* data, size_t size) = 0;
        virtual uint32_t getBaseAddress() const = 0;
        virtual uint32_t getEntryPoint() const = 0;
        virtual const uint8_t* getMemoryData() const = 0;
        virtual size_t getMemorySize() const = 0;
        virtual const std::vector<std::unique_ptr<Section>>& getSections() const = 0;
        virtual const std::vector<std::unique_ptr<Import>>& getImports() const = 0;
    };

    enum class ImageType {
        Unknown,
        PE,
        XEX2
    };

    class ImageLoader {
    public:
        static std::unique_ptr<IImage> load(const std::string& path);
        static std::unique_ptr<IImage> loadFromMemory(const uint8_t* data, size_t size);

    private:
        static ImageType detectType(const uint8_t* data, size_t size);
    };

} 