cmake_minimum_required(VERSION 3.20)

project(ShadowEngine LANGUAGES C)

set(SHADOW_ENGINE_MAIN
    src/shadow_engine_patch.c
    src/shadow_engine_patch.def)

set(SHADOW_ENGINE_MODULES
    src/modules/00_shared_config_state.inc
    src/modules/10_runtime_primitives.inc
    src/modules/20_manager_owner_profile.inc
    src/modules/30_renderer_queue_diagnostics.inc
    src/modules/40_render_record_repair.inc
    src/modules/50_resource_lifecycle_trace.inc
    src/modules/60_engine_expansion.inc
    src/modules/70_bootstrap_orchestration.inc
    src/modules/README.md)

set_source_files_properties(${SHADOW_ENGINE_MODULES}
    PROPERTIES HEADER_FILE_ONLY TRUE LANGUAGE C)

# This target exists for CLion indexing and source navigation. It deliberately
# does not compile with the CMake-selected compiler: verified releases use
# bundled TinyCC through build.ps1.
add_custom_target(shadow_engine_sources SOURCES
    ${SHADOW_ENGINE_MAIN}
    ${SHADOW_ENGINE_MODULES})

find_program(SHADOW_ENGINE_POWERSHELL NAMES pwsh powershell)
if(SHADOW_ENGINE_POWERSHELL)
    add_custom_target(shadow_engine_tinycc
        COMMAND ${SHADOW_ENGINE_POWERSHELL}
            -NoProfile
            -ExecutionPolicy Bypass
            -File ${CMAKE_SOURCE_DIR}/build.ps1
            -VerifyReproducible
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        USES_TERMINAL
        COMMENT "Building ShadowEngine with the verified TinyCC pipeline")
endif()
