#pragma once
#include <cstdint>

struct reg128 {
    union   {
        struct {
            uint64_t low;
            uint64_t high;
        };
        double dwords[2];
        float words[4];
        uint8_t bytes[16];
    };    
};

struct crReg {
#ifdef BIG_ENDIAN
    uint32_t cr7 : 4;
    uint32_t cr6 : 4;
    uint32_t cr5 : 4;
    uint32_t cr4 : 4;
    uint32_t cr3 : 4;
    uint32_t cr2 : 4;
    uint32_t cr1 : 4;
    uint32_t cr0 : 4;
#endif
#ifdef LITTLE_ENDIAN 
    uint32_t cr0 : 4;
    uint32_t cr1 : 4;
    uint32_t cr2 : 4;
    uint32_t cr3 : 4;
    uint32_t cr4 : 4;
    uint32_t cr5 : 4;
    uint32_t cr6 : 4;
    uint32_t cr7 : 4;
#endif
};

struct PPCContext {
    uint32_t msr;
    uint64_t ctr;
    uint64_t lr;
    uint32_t cia; // current instruction address
    crReg cr;
    uint32_t gpr[32];
    double fpr[32];
    reg128 vxr[128];
};