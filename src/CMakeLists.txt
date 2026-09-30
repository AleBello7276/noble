cmake_minimum_required(VERSION 3.30)

project(noble)


set(Core 
)
if(WIN32)
    list(APPEND Core noble/core/platform/WinAlloc.cpp)
else()
    list(APPEND Core noble/core/platform/PosixAlloc.cpp)
endif()


set(Loader 
    noble/Loader/PEImage.h
    noble/Loader/XexImage.h
    noble/Loader/ImageLoader.cpp
    noble/Loader/ImageLoader.h
    noble/Loader/table/ImportTable.h
    noble/Loader/AES/AES.h
    noble/Loader/AES/AES.cpp
)

set(Emulator 
    noble/emulator/Emulator.cpp
    noble/emulator/Memory.cpp
    noble/emulator/PPCModule.cpp
)

set(Cpu
    noble/cpu/Scheduler.cpp
    noble/cpu/CpuExecutor.cpp
    noble/cpu/cl/CraneliftJIT.cpp
    noble/cpu/cl/EmitterContext.cpp
)

set(Kernel
    noble/kernel/Kernel.cpp
    noble/kernel/KProcess.cpp
    noble/kernel/hle/Shims.cpp
    noble/kernel/hle/Exports.cpp
    noble/kernel/hle/krnl/Threading.cpp
)


set(SRC
    noble/noble.cpp
    noble/Logger.h 
	

    ${Loader}
    ${Emulator}
    ${Core}
    ${Cpu} 
    ${Kernel} 
)

set(INCLUDE
    ${CMAKE_CURRENT_SOURCE_DIR}/noble/
    ${CMAKE_CURRENT_SOURCE_DIR}/ffi/
)


include_directories(${INCLUDE})
add_executable(noble ${CMAKE_CURRENT_SOURCE_DIR}/${SRC})


# Rust 

find_program(CARGO_EXECUTABLE cargo REQUIRED)
set(RUST_FFI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ffi")
set(RUST_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/rust-target")
if(NOT DEFINED RUST_CARGO_HOME)
    set(RUST_CARGO_HOME "${CMAKE_CURRENT_BINARY_DIR}/cargo-home")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(RUST_PROFILE debug)
    set(RUST_PROFILE_FLAG)
else()
    set(RUST_PROFILE release)
    set(RUST_PROFILE_FLAG --release)
endif()

if(WIN32)
    set(RUST_LIBRARY_NAME noble_ffi.lib)
else()
    set(RUST_LIBRARY_NAME libnoble_ffi.a)
endif()

set(RUST_LIBRARY "${RUST_TARGET_DIR}/${RUST_PROFILE}/${RUST_LIBRARY_NAME}")

add_custom_command(
    OUTPUT "${RUST_LIBRARY}"
    COMMAND "${CMAKE_COMMAND}" -E env
        "CARGO_TARGET_DIR=${RUST_TARGET_DIR}"
        "CARGO_HOME=${RUST_CARGO_HOME}"
        "${CARGO_EXECUTABLE}" build
        --manifest-path "${RUST_FFI_DIR}/Cargo.toml"
        --locked ${RUST_PROFILE_FLAG}
    DEPENDS
        "${RUST_FFI_DIR}/Cargo.toml"
        "${RUST_FFI_DIR}/Cargo.lock"
        "${RUST_FFI_DIR}/src/lib.rs"
        "${RUST_FFI_DIR}/src/cranelift.rs"
    WORKING_DIRECTORY "${RUST_FFI_DIR}"
    VERBATIM
)

add_custom_target(noble_ffi_build DEPENDS "${RUST_LIBRARY}")
add_library(noble_ffi STATIC IMPORTED GLOBAL)
set_target_properties(noble_ffi PROPERTIES IMPORTED_LOCATION "${RUST_LIBRARY}")
add_dependencies(noble_ffi noble_ffi_build)
target_link_libraries(noble PRIVATE noble_ffi)
if(WIN32)
    target_link_libraries(noble PRIVATE ws2_32 ntdll userenv bcrypt)
endif()
