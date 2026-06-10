# CompilerFlags.cmake
# Common compiler flags for MicroscopeIBOM

if(MSVC)
    # MSVC flags
    target_compile_options(${PROJECT_NAME} PRIVATE
        /W4             # Warning level 4
        /utf-8          # UTF-8 source and execution charset
        /MP             # Multi-processor compilation
        /permissive-    # Strict standard conformance
        /Zc:__cplusplus # Correct __cplusplus macro
    )

    # Release optimizations
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(${PROJECT_NAME} PRIVATE /O2 /GL /Oi)
        target_link_options(${PROJECT_NAME} PRIVATE /LTCG)
    endif()

    # Disable annoying MSVC warnings
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS
        NOMINMAX                # Prevent windows.h min/max macros
        WIN32_LEAN_AND_MEAN     # Reduce windows.h includes
    )

elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # GCC / Clang flags
    target_compile_options(${PROJECT_NAME} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
        -Wno-missing-field-initializers
    )

    # Release optimizations.
    #
    # Arch tuning: -march=native (default) detects the build host's ISA — great
    # when compiling ON the Jetson AGX Orin, but it makes the binary
    # non-reproducible and non-portable to a different Jetson (Nano/NX, other
    # cores). To pin the target explicitly (e.g. for the Orin's Cortex-A78AE
    # cores) configure with:  -DIBOM_TARGET_CPU=cortex-a78ae
    # which emits -mcpu=<cpu> (sets both arch and tune) instead of -march=native.
    set(IBOM_TARGET_CPU "" CACHE STRING
        "Explicit CPU for -mcpu= (e.g. cortex-a78ae for Jetson Orin). Empty = -march=native.")
    if(IBOM_TARGET_CPU)
        set(_ibom_arch_flag -mcpu=${IBOM_TARGET_CPU})
    else()
        set(_ibom_arch_flag -march=native)
    endif()
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        target_compile_options(${PROJECT_NAME} PRIVATE -O3 ${_ibom_arch_flag} -flto)
        target_link_options(${PROJECT_NAME} PRIVATE -flto)
    endif()

    # Debug flags
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(${PROJECT_NAME} PRIVATE -g -O0)
    endif()
endif()

# AddressSanitizer
if(IBOM_ENABLE_ASAN AND NOT MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(${PROJECT_NAME} PRIVATE -fsanitize=address)
endif()
