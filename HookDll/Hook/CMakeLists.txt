cmake_minimum_required(VERSION 3.20)
# Cross-compilation toolchain is set via: cmake -DCMAKE_TOOLCHAIN_FILE=../mingw64-toolchain.cmake ..
project(dinput8 CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Detours include path
set(DETOURS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Detours-master/include")
set(DETOURS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Detours-master/src")

# Note: We're using the Detours headers but not compiling from source due to MinGW incompatibility
# The Detours functionality will be linked at runtime via the Windows SDK

# Detours source files compiled into the DLL (requires MinGW SEH compat shim)
set(DETOURS_SOURCES
    ${DETOURS_SRC_DIR}/detours.cpp
    ${DETOURS_SRC_DIR}/disasm.cpp
    ${DETOURS_SRC_DIR}/image.cpp
    ${DETOURS_SRC_DIR}/modules.cpp
    ${DETOURS_SRC_DIR}/creatwth.cpp
)

# Force-include MinGW __try/__except compatibility header for each Detours source
foreach(DETOURS_FILE ${DETOURS_SOURCES})
    set_source_files_properties(${DETOURS_FILE} PROPERTIES
        COMPILE_FLAGS "-include ${DETOURS_SRC_DIR}/detours_mingw_compat.h"
    )
endforeach()

# Source files for dinput8.dll
set(HOOK_SOURCES
    BaseMethodHook.cpp
    DataQueue.cpp
    dllmain.cpp
    GameEngineUpdate.cpp
    GetPrivateStash.cpp
    GrimTypes.cpp
    HookLog.cpp
    InventorySack_AddItem.cpp
    OnDemandSeedInfo.cpp
    SetHardcore.cpp
    SettingsReader.cpp
    stdafx.cpp
    TcpClient.cpp
)

set(HOOK_HEADERS
    BaseMethodHook.h
    DataQueue.h
    Exports.h
    GameEngineUpdate.h
    GetPrivateStash.h
    GrimTypes.h
    HookLog.h
    InventorySack_AddItem.h
    JsonSerializer.h
    Logger.h
    MessageType.h
    OnDemandSeedInfo.h
    SetHardcore.h
    SettingsReader.h
    TcpClient.h
    VTableDispatch.h
    Vec3f.h
    resource.h
    stdafx.h
)

# Create DLL target (includes both hook sources and Detours compiled from source)
add_library(dinput8 SHARED ${HOOK_SOURCES} ${DETOURS_SOURCES} ${HOOK_HEADERS})

# Include directories
target_include_directories(dinput8 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../Shared
    ${DETOURS_SRC_DIR}
)

# Link libraries
target_link_libraries(dinput8 PRIVATE
    ws2_32
    kernel32
    user32
    ole32
    oleaut32
    shell32
    advapi32
)

# Compiler flags for Windows DLL
target_compile_options(dinput8 PRIVATE
    -Wall
    -m64
    -fPIC
    -D_X64_
    -D_AMD64_
    -D_WINDOWS
    -D_WIN64
    -DUNICODE
    -D_UNICODE
    -Wno-multichar
    -fpermissive
)

# Linker flags for Windows DLL
target_link_options(dinput8 PRIVATE
    -shared
    -Wl,--out-implib=libdinput8.a
    -Wl,--subsystem,windows
    -static-libgcc
    -static-libstdc++
    -m64
)

# Set output directory
set_target_properties(dinput8 PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    PREFIX ""
    SUFFIX ".dll"
)

# Custom command to verify DLL was created
add_custom_command(TARGET dinput8 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "===== DLL built: $<TARGET_FILE:dinput8> ====="
    COMMAND file $<TARGET_FILE:dinput8> || echo "File info not available"
)

# Optional: Install target
install(TARGETS dinput8 
    LIBRARY DESTINATION bin
    RUNTIME DESTINATION bin
)

message(STATUS "Building dinput8.dll for Windows x64")
message(STATUS "Detours Headers: ${DETOURS_SRC_DIR}")
