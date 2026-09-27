#include "Logger.h"
#include "Loader/ImageLoader.h"
#include <Loader/XEXImage.h>
#include <Loader/PEImage.h>
#include "IR/IRGen.h"
#include "Codec/ppc_codec.h"
#include "PPCModule.h"

//#include "IR/IRGenerator.h"
//#include <IR/Unit/UnitTesting.h>
//#include <IR/IRFunc.h>
//

//// Naive+ stuff
//IRGenerator* g_irGen;
//llvm::LLVMContext cxt;
//llvm::Module* mod = new llvm::Module("Xenon", cxt);
//llvm::IRBuilder<llvm::NoFolder> builder(cxt);
//



int main(int argc, char* argv[]) {
   


    PPCModule testMod = PPCModule("rsuite1.exe", false, false);
    


    //loadedXex = new XexImage(L"LLVMTest1.xex");
    //loadedXex->LoadXex();
    //g_irGen = new IRGenerator(loadedXex, mod, &builder);
    //g_irGen->Initialize();
    
    // 394a0001

    const codec::DecodedInst instObj = codec::PPCCodec::decode(0xffff4A39);
    codec::PPCInstrType instrType = instObj.mInstTemplate.type();
    printf("Decoded instruction: %s\n", instObj.mInstTemplate.dump(instObj.mData).c_str());
    //codec::PPCCodec::decode(0x394a0001);
    uint32_t encodedAddi = codec::Addi::encode(10, 10, 1);

    // Splash texts
    printf("Hello, World!\n");
    printf("Say hi to the new galaxy note\n");

    // 19/02/2025
	printf("Trans rights!!!  @permdog99\n");
    printf("Live and learn  @ashrindy\n");
    printf("On dog  @.nover.\n");
	printf("Gotta Go Fast  @neoslyde\n");
}


