cmake_minimum_required(VERSION 3.28)
project(qmg_native_movement_reference LANGUAGES C)

# This optional local-only executable is a GPL combined work. No reference
# implementation is copied into this MIT project or linked into qmg_core.
set(Q2_REFERENCE_SOURCE "E:/_SOURCE/_CODE/Quake-2-master" CACHE PATH "Read-only official Quake II 3.19 source")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/reference.json" reference_json)
string(JSON reference_count LENGTH "${reference_json}" dependencies)
math(EXPR reference_last "${reference_count}-1")
foreach(index RANGE 0 ${reference_last})
    string(JSON reference_file GET "${reference_json}" dependencies ${index} relative_path)
    string(JSON expected_hash GET "${reference_json}" dependencies ${index} sha256)
    if(NOT EXISTS "${Q2_REFERENCE_SOURCE}/${reference_file}")
        message(FATAL_ERROR "Missing external reference: ${reference_file}")
    endif()
    file(SHA256 "${Q2_REFERENCE_SOURCE}/${reference_file}" actual_hash)
    if(NOT actual_hash STREQUAL expected_hash)
        message(FATAL_ERROR "External reference hash differs from pinned 3.19 source: ${reference_file}")
    endif()
endforeach()
add_executable(qmg_native_pmove
    harness.c
    "${Q2_REFERENCE_SOURCE}/qcommon/pmove.c"
    "${Q2_REFERENCE_SOURCE}/qcommon/cmodel.c"
    "${Q2_REFERENCE_SOURCE}/game/q_shared.c"
    "${Q2_REFERENCE_SOURCE}/qcommon/md4.c")
target_include_directories(qmg_native_pmove PRIVATE "${Q2_REFERENCE_SOURCE}/qcommon")
target_compile_definitions(qmg_native_pmove PRIVATE C_ONLY _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(qmg_native_pmove PRIVATE /fp:strict /Od)
else()
    target_compile_options(qmg_native_pmove PRIVATE -O0 -ffp-contract=off)
    target_link_libraries(qmg_native_pmove PRIVATE m)
endif()
# Deliberately no install/package target. Build products stay in project scratch.
