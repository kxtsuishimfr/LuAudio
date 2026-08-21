function(luaudio_add_plugin target)
    cmake_parse_arguments(ARG "" "" "SOURCES" ${ARGN})

    if (NOT ARG_SOURCES)
        message(FATAL_ERROR "luaudio_add_plugin requires SOURCES")
    endif()

    add_library(${target} MODULE ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE LuAudio::PluginSDK)
    target_compile_features(${target} PRIVATE cxx_std_20)
    set_target_properties(${target} PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
endfunction()