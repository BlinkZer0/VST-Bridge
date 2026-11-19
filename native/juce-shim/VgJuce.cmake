# SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
#
# VST gHost's universal JUCE adapter: JUCE's CMake API (juce_add_plugin, juce_add_binary_data,
# juce::juce_* module targets, ...) re-implemented for Android, so an unmodified JUCE plugin project
# builds as a native Android library that exports the vgn interface (native/vgn/vgn.h).
#
# How a project gets here (native/CMakeLists.txt arranges all three):
#   FetchContent(JUCE ...)      -> FETCHCONTENT_SOURCE_DIR_JUCE points at this folder
#   find_package(JUCE CONFIG)   -> JUCE_DIR points at this folder (JUCEConfig.cmake)
#   add_subdirectory(<JUCE>)    -> the functions/targets already exist; adding is harmless
#
# Needs: VG_JUCE_DIR (a real JUCE source tree, used for module code) and VG_VGN_DIR (native/vgn).
# Every plugin made by juce_add_plugin becomes lib vgn_<target>.so; the app finds them by that prefix.
include_guard(GLOBAL)

if(NOT VG_JUCE_DIR OR NOT EXISTS "${VG_JUCE_DIR}/modules/juce_core/juce_core.h")
    message(FATAL_ERROR "VgJuce: VG_JUCE_DIR must point at a JUCE source tree")
endif()
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/vg_binary_data.cmake)

set(JUCE_SOURCE_DIR "${VG_JUCE_DIR}" CACHE INTERNAL "")
set(JUCE_MODULES_DIR "${VG_JUCE_DIR}/modules" CACHE INTERNAL "")
file(READ "${VG_JUCE_DIR}/modules/juce_core/system/juce_StandardHeader.h" _vg_std)
string(REGEX MATCH "JUCE_MAJOR_VERSION[ 	]+([0-9]+)" _ "${_vg_std}")
set(JUCE_VERSION_MAJOR "${CMAKE_MATCH_1}" CACHE INTERNAL "")

# JUCE's Android code uses the NDK's cpufeatures helper (the Projucer adds it the same way).
add_library(vg_cpufeatures STATIC ${ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c)
target_include_directories(vg_cpufeatures PUBLIC ${ANDROID_NDK}/sources/android/cpufeatures)
target_compile_options(vg_cpufeatures PRIVATE -w -fvisibility=hidden)

# ---------------------------------------------------------------- modules

# Reads "key: value" lines from a module header's declaration block.
function(_vg_module_decl header key out)
    file(STRINGS "${header}" lines REGEX "^[ \t]*${key}:")
    set(v "")
    if(lines)
        list(GET lines 0 line)
        string(REGEX REPLACE "^[ \t]*${key}:[ \t]*" "" v "${line}")
        string(REGEX REPLACE "[ \t,]+" ";" v "${v}")
        list(REMOVE_ITEM v "")
    endif()
    set(${out} "${v}" PARENT_SCOPE)
endfunction()

# juce_add_module(<path to module folder> [ALIAS_NAMESPACE ns]) — same as JUCE's.
function(juce_add_module modulePath)
    cmake_parse_arguments(M "" "ALIAS_NAMESPACE" "" ${ARGN})
    get_filename_component(modulePath "${modulePath}" ABSOLUTE)
    get_filename_component(name "${modulePath}" NAME)
    get_filename_component(parent "${modulePath}" DIRECTORY)
    if(TARGET ${name})
        return()
    endif()
    set(header "${modulePath}/${name}.h")
    if(NOT EXISTS "${header}")
        set(header "${modulePath}/${name}.hpp")
    endif()
    # Module code = files directly in the module folder named <module>*.c/.cpp, minus other platforms'.
    file(GLOB candidates "${modulePath}/${name}*.c" "${modulePath}/${name}*.cc" "${modulePath}/${name}*.cpp" "${modulePath}/${name}*.cxx")
    set(srcs "")
    foreach(f IN LISTS candidates)
        get_filename_component(we "${f}" NAME_WE)
        string(TOLOWER "${we}" we)
        if(NOT we MATCHES "_(ios|linux|mac|osx|windows)$")
            list(APPEND srcs "${f}")
        endif()
    endforeach()
    add_library(${name} INTERFACE)
    target_sources(${name} INTERFACE ${srcs})
    target_include_directories(${name} INTERFACE "${parent}")
    target_compile_definitions(${name} INTERFACE JUCE_MODULE_AVAILABLE_${name}=1)
    _vg_module_decl("${header}" dependencies deps)
    foreach(d IN LISTS deps)
        if(NOT TARGET ${d} AND EXISTS "${parent}/${d}")
            juce_add_module("${parent}/${d}")
        endif()
        if(TARGET ${d})
            target_link_libraries(${name} INTERFACE ${d})
        endif()
    endforeach()
    _vg_module_decl("${header}" searchpaths sp)
    foreach(p IN LISTS sp)
        target_include_directories(${name} INTERFACE "${modulePath}/${p}")
    endforeach()
    _vg_module_decl("${header}" minimumCppStandard std)
    if(std)
        target_compile_features(${name} INTERFACE cxx_std_${std})
    endif()
    # Module-specific settings JUCE's own CMake adds (JUCEModuleSupport.cmake).
    if(name STREQUAL "juce_gui_extra" AND EXISTS "${modulePath}/native/typescript/webview-interop/package.json")
        file(READ "${modulePath}/native/typescript/webview-interop/package.json" pj)
        string(JSON wv GET "${pj}" version)
        target_compile_definitions(${name} INTERFACE "JUCE_WEBVIEW_INTEROP_LIBRARY_VERSION=\"${wv}\"")
    endif()
    if(name STREQUAL "juce_opengl")
        target_compile_definitions(${name} INTERFACE JUCE_ANDROID_GL_ES_VERSION_3_0=1)
        target_link_libraries(${name} INTERFACE EGL GLESv3)
    endif()
    if(M_ALIAS_NAMESPACE)
        add_library(${M_ALIAS_NAMESPACE}::${name} ALIAS ${name})
    endif()
endfunction()

function(juce_add_modules)
    cmake_parse_arguments(M "" "ALIAS_NAMESPACE;INSTALL_PATH" "" ${ARGN})
    foreach(m IN LISTS M_UNPARSED_ARGUMENTS)
        if(M_ALIAS_NAMESPACE)
            juce_add_module("${m}" ALIAS_NAMESPACE ${M_ALIAS_NAMESPACE})
        else()
            juce_add_module("${m}")
        endif()
    endforeach()
endfunction()

file(GLOB _vg_modules LIST_DIRECTORIES true "${VG_JUCE_DIR}/modules/juce_*")
foreach(m IN LISTS _vg_modules)
    get_filename_component(n "${m}" NAME)
    if(IS_DIRECTORY "${m}" AND NOT n STREQUAL "juce_audio_plugin_client")
        juce_add_module("${m}")
    endif()
endforeach()
foreach(m IN LISTS _vg_modules)
    get_filename_component(n "${m}" NAME)
    if(TARGET ${n} AND NOT TARGET juce::${n})
        add_library(juce::${n} ALIAS ${n})
    endif()
endforeach()

# Settings every JUCE consumer needs on Android (the Projucer's Android exporter sets the same).
target_compile_definitions(juce_core INTERFACE JUCE_ANDROID=1 JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_STANDALONE_APPLICATION=0 JUCE_USE_ANDROID_OBOE=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0 JUCE_PLUGINHOST_VST3=0 JUCE_PLUGINHOST_AU=0 JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_LADSPA=0 JUCE_PLUGINHOST_ARA=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_compile_definitions(juce_core INTERFACE NDEBUG=1 _NDEBUG=1)
target_link_libraries(juce_core INTERFACE android log OpenSLES vg_cpufeatures)

# The plugin client module is what this adapter replaces (vgn_adapter.cpp): an empty target here.
foreach(t juce_audio_plugin_client juce_recommended_config_flags juce_recommended_lto_flags juce_recommended_warning_flags)
    add_library(${t} INTERFACE)
    add_library(juce::${t} ALIAS ${t})
endforeach()

# ---------------------------------------------------------------- targets

function(_vg_bool out v)
    if(v)
        set(${out} 1 PARENT_SCOPE)
    else()
        set(${out} 0 PARENT_SCOPE)
    endif()
endfunction()

function(_vg_fourcc out code fallback)
    if(NOT code)
        set(code "${fallback}")
    endif()
    string(SUBSTRING "${code}    " 0 4 code)
    string(HEX "${code}" hex)
    set(${out} "0x${hex}" PARENT_SCOPE)
endfunction()

# Plugin -> a native Android library exporting the vgn interface.
function(juce_add_plugin target)
    set(one PRODUCT_NAME COMPANY_NAME COMPANY_WEBSITE COMPANY_EMAIL DESCRIPTION VERSION BUNDLE_ID
        PLUGIN_MANUFACTURER_CODE PLUGIN_CODE IS_SYNTH NEEDS_MIDI_INPUT NEEDS_MIDI_OUTPUT IS_MIDI_EFFECT
        EDITOR_WANTS_KEYBOARD_FOCUS)
    cmake_parse_arguments(P "" "${one}" "" ${ARGN})
    if(NOT P_PRODUCT_NAME)
        set(P_PRODUCT_NAME "${target}")
    endif()
    if(NOT P_COMPANY_NAME)
        set(P_COMPANY_NAME "yourcompany")
    endif()
    if(NOT P_VERSION)
        set(P_VERSION "${PROJECT_VERSION}")
    endif()
    if(NOT P_VERSION)
        set(P_VERSION "1.0.0")
    endif()
    string(REPLACE "." ";" vparts "${P_VERSION}.0.0")
    list(GET vparts 0 v0)
    list(GET vparts 1 v1)
    list(GET vparts 2 v2)
    math(EXPR vcode "(${v0} << 16) + (${v1} << 8) + ${v2}" OUTPUT_FORMAT HEXADECIMAL)
    _vg_bool(synth "${P_IS_SYNTH}")
    _vg_bool(midiIn "${P_NEEDS_MIDI_INPUT}")
    _vg_bool(midiOut "${P_NEEDS_MIDI_OUTPUT}")
    _vg_bool(midiFx "${P_IS_MIDI_EFFECT}")
    _vg_bool(kbd "${P_EDITOR_WANTS_KEYBOARD_FOCUS}")
    _vg_fourcc(mfr "${P_PLUGIN_MANUFACTURER_CODE}" "Manu")
    _vg_fourcc(code "${P_PLUGIN_CODE}" "Plug")

    add_library(${target} SHARED "${VG_VGN_DIR}/vgn_adapter.cpp")
    string(TOLOWER "${target}" lib)
    string(REGEX REPLACE "[^a-z0-9_]" "_" lib "${lib}")
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "vgn_${lib}"
        C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    target_include_directories(${target} PRIVATE "${VG_VGN_DIR}")
    target_compile_definitions(${target} PRIVATE
        "JucePlugin_Name=\"${P_PRODUCT_NAME}\"" "JucePlugin_Desc=\"${P_DESCRIPTION}\""
        "JucePlugin_Manufacturer=\"${P_COMPANY_NAME}\"" "JucePlugin_ManufacturerWebsite=\"${P_COMPANY_WEBSITE}\""
        "JucePlugin_ManufacturerEmail=\"${P_COMPANY_EMAIL}\"" JucePlugin_ManufacturerCode=${mfr} JucePlugin_PluginCode=${code}
        JucePlugin_IsSynth=${synth} JucePlugin_WantsMidiInput=${midiIn} JucePlugin_ProducesMidiOutput=${midiOut}
        JucePlugin_IsMidiEffect=${midiFx} JucePlugin_EditorRequiresKeyboardFocus=${kbd}
        JucePlugin_Version=${P_VERSION} JucePlugin_VersionCode=${vcode} "JucePlugin_VersionString=\"${P_VERSION}\""
        JucePlugin_Build_VST=0 JucePlugin_Build_VST3=0 JucePlugin_Build_AU=0 JucePlugin_Build_AUv3=0
        JucePlugin_Build_AAX=0 JucePlugin_Build_Standalone=0 JucePlugin_Build_Unity=0 JucePlugin_Build_LV2=0
        JucePlugin_Enable_IAA=0 JucePlugin_Enable_ARA=0)
    target_compile_options(${target} PRIVATE -O2 -Wno-everything)
    # Only the vgn interface and JNI entry points leave the library: JUCE and the plugin stay
    # private, so several plugins (even different JUCE versions) can share the app process.
    target_link_options(${target} PRIVATE -Wl,--exclude-libs,ALL -Wl,--gc-sections
        "-Wl,--version-script=${VG_VGN_DIR}/vgn.map")
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS "${VG_VGN_DIR}/vgn.map")
    set_property(GLOBAL APPEND PROPERTY VG_NATIVE_PLUGIN_TARGETS ${target})
    message(STATUS "VgJuce: plugin ${target} -> libvgn_${lib}.so (\"${P_PRODUCT_NAME}\", synth=${synth})")
endfunction()

# Apps, console tools, PIPs: not built for Android (tests etc.), but kept as targets so the
# project's own target_* calls still work.
function(_vg_unbuilt_target target)
    set(stub "${CMAKE_CURRENT_BINARY_DIR}/${target}_vg_stub.cpp")
    if(NOT EXISTS "${stub}")
        file(WRITE "${stub}" "// not built for Android\n")
    endif()
    add_library(${target} STATIC EXCLUDE_FROM_ALL "${stub}")
endfunction()
function(juce_add_console_app target)
    _vg_unbuilt_target(${target})
endfunction()
function(juce_add_gui_app target)
    _vg_unbuilt_target(${target})
endfunction()
function(juce_add_pip header)
endfunction()

function(juce_add_binary_data target)
    cmake_parse_arguments(B "" "HEADER_NAME;NAMESPACE" "SOURCES" ${ARGN})
    if(NOT B_HEADER_NAME)
        set(B_HEADER_NAME "BinaryData.h")
    endif()
    if(NOT B_NAMESPACE)
        set(B_NAMESPACE "BinaryData")
    endif()
    set(files "")
    foreach(f IN LISTS B_SOURCES)
        get_filename_component(f "${f}" ABSOLUTE)
        list(APPEND files "${f}")
    endforeach()
    vg_binary_data(${target} "${CMAKE_CURRENT_BINARY_DIR}/${target}" "${B_HEADER_NAME}" "${B_NAMESPACE}" ${files})
    # JuceHeader.h includes this header for every binary-data target the plugin links (as JUCE does).
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON VG_BINARY_DATA_HEADER "${B_HEADER_NAME}")
endfunction()

# JuceHeader.h: includes the JUCE modules the target links. Written at the end of configuration
# (cmake_language DEFER), when the project has made all its target_link_libraries calls.
function(juce_generate_juce_header target)
    # Same place as JUCE's own CMake, so Projucer-era includes like "../JuceLibraryCode/JuceHeader.h" work.
    set(dir "${CMAKE_CURRENT_BINARY_DIR}/${target}_artefacts/JuceLibraryCode")
    file(MAKE_DIRECTORY "${dir}")
    target_include_directories(${target} PRIVATE "${dir}")
    # Deferred arguments are expanded when the call runs, so bake the values in now.
    cmake_language(EVAL CODE "cmake_language(DEFER DIRECTORY [[${CMAKE_SOURCE_DIR}]] CALL _vg_write_juce_header [[${target}]] [[${dir}]] [[${PROJECT_NAME}]] [[${PROJECT_VERSION}]])")
endfunction()

function(_vg_collect_modules lib outVar)
    set(seen ${${outVar}})
    string(REGEX REPLACE "^juce::" "" m "${lib}")
    if(NOT m MATCHES "^juce_" OR NOT TARGET ${m} OR m IN_LIST seen)
        return()
    endif()
    list(APPEND seen ${m})
    get_target_property(deps ${m} INTERFACE_LINK_LIBRARIES)
    if(deps)
        foreach(d IN LISTS deps)
            _vg_collect_modules("${d}" seen)
        endforeach()
    endif()
    set(${outVar} ${seen} PARENT_SCOPE)
endfunction()

function(_vg_write_juce_header target dir pname pver)
    get_target_property(libs ${target} LINK_LIBRARIES)
    set(mods "")
    set(datahdrs "")
    if(libs)
        foreach(l IN LISTS libs)
            _vg_collect_modules("${l}" mods)
            if(TARGET ${l})
                get_target_property(h ${l} VG_BINARY_DATA_HEADER)
                if(h)
                    list(APPEND datahdrs "${h}")
                endif()
            endif()
        endforeach()
    endif()
    list(REMOVE_ITEM mods juce_audio_plugin_client juce_recommended_config_flags juce_recommended_lto_flags
        juce_recommended_warning_flags)
    list(SORT mods)
    list(REMOVE_DUPLICATES datahdrs)
    set(text "#pragma once\n// Generated by VST gHost's JUCE adapter (native/juce-shim/VgJuce.cmake).\n")
    foreach(m IN LISTS mods)
        string(APPEND text "#include <${m}/${m}.h>\n")
    endforeach()
    foreach(h IN LISTS datahdrs)
        string(APPEND text "#include \"${h}\"\n")
    endforeach()
    string(APPEND text "\n#if ! DONT_SET_USING_JUCE_NAMESPACE\nusing namespace juce;\n#endif\n\n")
    string(APPEND text "namespace ProjectInfo\n{\n    const char* const projectName = \"${pname}\";\n")
    string(APPEND text "    const char* const companyName = \"\";\n    const char* const versionString = \"${pver}\";\n")
    string(APPEND text "    const int versionNumber = 0x10000;\n}\n")
    set(old "")
    if(EXISTS "${dir}/JuceHeader.h")
        file(READ "${dir}/JuceHeader.h" old)
    endif()
    if(NOT old STREQUAL text)
        file(WRITE "${dir}/JuceHeader.h" "${text}")
    endif()
endfunction()

# Desktop-only knobs: accepted and ignored.
foreach(f juce_set_aax_sdk_path juce_set_vst2_sdk_path juce_set_vst3_sdk_path juce_set_ara_sdk_path
        juce_set_lv2_sdk_path juce_enable_copy_plugin_step juce_add_bundle_resources_directory
        juce_enable_vst3_manifest_step juce_disable_default_flags)
    cmake_language(EVAL CODE "function(${f})\nendfunction()")
endforeach()

# ---------------------------------------------------------------- project-structure helpers

# Projects often vendor JUCE (and friends) as git submodules: add_subdirectory(libs/JUCE). JUCE is
# already provided by this adapter, and submodules are empty in source archives, so:
#   - a JUCE checkout (or an empty folder named JUCE*) is skipped;
#   - a missing / empty subdirectory is skipped with a warning instead of failing the configure.
function(add_subdirectory dir)
    get_filename_component(abs "${dir}" ABSOLUTE)
    get_filename_component(leaf "${abs}" NAME)
    string(TOUPPER "${leaf}" leafUp)
    if(EXISTS "${abs}/modules/juce_core/juce_core.h" OR EXISTS "${abs}/extras/Build/CMake/JUCEUtils.cmake"
       OR (leafUp MATCHES "^JUCE" AND NOT EXISTS "${abs}/CMakeLists.txt"))
        message(STATUS "VgJuce: using the adapter's JUCE instead of ${dir}")
        return()
    endif()
    if(NOT EXISTS "${abs}/CMakeLists.txt")
        message(WARNING "VgJuce: skipping add_subdirectory(${dir}): no CMakeLists.txt (submodule not fetched?)")
        return()
    endif()
    _add_subdirectory(${dir} ${ARGN})
endfunction()

# JUCE-ecosystem extras that make desktop-only formats: accepted, nothing to build on Android.
foreach(f clap_juce_extensions_plugin juce_add_lv2_plugin)
    cmake_language(EVAL CODE "function(${f})\nendfunction()")
endforeach()

# Projucer (.jucer) projects.
include(${CMAKE_CURRENT_LIST_DIR}/VgJucer.cmake)
