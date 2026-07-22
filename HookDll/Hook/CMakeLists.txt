cmake_minimum_required(VERSION 3.20)
project(dinput8 CXX)

# Detect cross-compilation to Windows
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Use MinGW-w64 cross-compiler
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Boost 1.64 QUIET COMPONENTS system thread)

if(NOT Boost_FOUND)
    message(WARNING "Boost not found for MinGW. Attempting compilation without Boost linking.")
    message(WARNING "You will need to build Boost for MinGW cross-compilation separately.")
endif()

# Detours include path
set(DETOURS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Detours-master/include")
set(DETOURS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Detours-master/src")

# Note: We're using the Detours headers but not compiling from source due to MinGW incompatibility
# The Detours functionality will be linked at runtime via the Windows SDK

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

# Create DLL target
add_library(dinput8 SHARED ${HOOK_SOURCES} ${HOOK_HEADERS})

# Include directories - add explicit Boost path from Linux system
target_include_directories(dinput8 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../Shared
    ${DETOURS_SRC_DIR}
    /usr/include  # For Boost headers (fallback for cross-compilation)
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

if(Boost_FOUND)
    target_link_libraries(dinput8 PRIVATE ${Boost_LIBRARIES})
    target_include_directories(dinput8 PRIVATE ${Boost_INCLUDE_DIRS})
    message(STATUS "Boost libraries linked: ${Boost_LIBRARIES}")
else()
    message(STATUS "Boost not linked - will need separate configuration")
endif()

# Compiler flags for Windows DLL
target_compile_options(dinput8 PRIVATE
    -Wall
    -m64
    -fPIC
    -D_X64_
    -D_WINDOWS
    -D_WIN64
    -Wno-multichar
    -I/usr/include  # Add system include path for Boost headers
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
if(Boost_FOUND)
    message(STATUS "Boost Include: ${Boost_INCLUDE_DIRS}")
endif()
message(STATUS "Detours Headers: ${DETOURS_SRC_DIR}")
message(STATUS "Cross-compiler: ${CMAKE_CXX_COMPILER}")
