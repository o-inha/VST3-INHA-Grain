cmake_minimum_required(VERSION 3.22)

project(INHACore VERSION 0.1.2 LANGUAGES CXX)

add_library(inha_core INTERFACE)
add_library(INHA::Core ALIAS inha_core)
target_compile_features(inha_core INTERFACE cxx_std_17)
target_include_directories(inha_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

if (WIN32)
    add_library(inha_license STATIC
        src/license/LicenseManager.cpp
    )
    add_library(INHA::License ALIAS inha_license)
    target_compile_features(inha_license PUBLIC cxx_std_17)
    target_include_directories(inha_license PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    target_link_libraries(inha_license PUBLIC INHA::Core PRIVATE
        winhttp bcrypt advapi32
    )
    if (MSVC)
        target_compile_definitions(inha_license PRIVATE
            WINVER=0x0A00 _WIN32_WINNT=0x0A00 UNICODE _UNICODE
            WIN32_LEAN_AND_MEAN NOMINMAX
        )
        target_compile_options(inha_license PRIVATE /W4 /permissive- /EHsc /utf-8)
    endif()
endif()

option(INHA_CORE_BUILD_TESTS "Build dependency-free INHA Core tests" ON)
if (INHA_CORE_BUILD_TESTS)
    enable_testing()
    add_executable(InhaCoreTests tests/CoreTests.cpp)
    target_link_libraries(InhaCoreTests PRIVATE INHA::Core)
    if (MSVC)
        target_compile_options(InhaCoreTests PRIVATE /W4 /permissive- /EHsc /utf-8)
    else()
        target_compile_options(InhaCoreTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME InhaCoreTests COMMAND InhaCoreTests)
endif()
