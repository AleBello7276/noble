cmake_minimum_required(VERSION 3.30)

project(noble)


set(Loader 
    noble/Loader/PEImage.h
    noble/Loader/XexImage.h
    noble/Loader/ImageLoader.cpp
    noble/Loader/ImageLoader.h
    noble/Loader/table/ImportTable.h
    noble/Loader/AES/AES.h
    noble/Loader/AES/AES.cpp
)


set(SRC
    noble/noble.cpp
    noble/Logger.h 
	noble/PPCModule.cpp
	noble/PPCModule.h

    ${Loader}
)

set(INCLUDE
    ${CMAKE_CURRENT_SOURCE_DIR}/noble/
    ${CMAKE_CURRENT_SOURCE_DIR}/ffi/
)


include_directories(${INCLUDE})
add_executable(noble ${CMAKE_CURRENT_SOURCE_DIR}/${SRC})
