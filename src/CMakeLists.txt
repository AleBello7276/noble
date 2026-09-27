cmake_minimum_required(VERSION 3.30)

project(Naive+)


set(Loader 
    src/Loader/PEImage.h
    src/Loader/XexImage.h
    src/Loader/ImageLoader.cpp
    src/Loader/ImageLoader.h
    src/Loader/table/ImportTable.h
    src/Loader/AES/AES.h
    src/Loader/AES/AES.cpp
)


set(CODEC
	src/Codec/ppc_codec.h
)

set(IR
    #src/IR/IRGen.h
    #src/IR/IRGen.cpp

    #src/IR/IRFunc.cpp
    #src/IR/IRFunc.h
    #src/IR/IRGenerator.cpp
    #src/IR/IRGenerator.h
)

set(VM
	src/VM/RuntimeVM.cpp
	src/VM/RuntimeVM.h
	src/VM/PPCContext.h
)

set(SRC
    src/LLVM360.cpp
    src/Logger.h 
	src/PPCModule.cpp
	src/PPCModule.h

    ${Loader}
    ${CODEC}
    ${IR}
	${VM}
)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/external/llvm_build/include)
add_executable(Naive+ ${CMAKE_CURRENT_SOURCE_DIR}/${SRC})
target_link_libraries(Naive+ PRIVATE EXT_LLVM)