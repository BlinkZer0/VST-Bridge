# SPDX-License-Identifier: GPL-3.0-or-later
#
# Phase 5: JUCE plugins built from their own, unmodified source as native Android libraries
# (lib vgn_<target>.so, exporting the vgn interface in vgn/vgn.h). Included from
# app/src/main/cpp/CMakeLists.txt.
#
# The universal JUCE adapter (juce-shim/VgJuce.cmake) re-implements JUCE's CMake API for Android,
# so adding a plugin = one vg_add_juce_project() line: a JUCE CMake project, fetched as a pinned
# archive (URL + SHA-256) or taken from a local folder.
#
# Local checkouts: set VG_PLUGIN_SRC_ROOT (Gradle: -Pvg.pluginSrcRoot=...) to a folder containing
# the projects' folders; a project whose local folder exists there is built from it.
#
# JUCE is AGPLv3 (or commercial): these libraries are AGPLv3-covered; the rest of VST gHost stays
# GPL-3.0-or-later, which GPLv3 section 13 allows combining with.

include(FetchContent)

set(VG_PLUGIN_SRC_ROOT "" CACHE PATH "Folder with local checkouts of plugin projects (optional)")
set(VG_NATIVE_PLUGINS "" CACHE STRING "Only build these projects (names, ;-separated); empty = all")

# One JUCE for every project (projects' own JUCE tags are overridden).
FetchContent_Declare(vg_juce URL https://github.com/juce-framework/JUCE/archive/refs/tags/9.0.5.tar.gz
    URL_HASH SHA256=d92207f51ba728d3d195b6d5e93ecd049717a915fc250ff6844c32ea7272315a
    SOURCE_SUBDIR do-not-configure DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(vg_juce)
set(VG_JUCE_DIR "${vg_juce_SOURCE_DIR}")
set(VG_VGN_DIR "${CMAKE_CURRENT_LIST_DIR}/vgn")

# Route every way a project can ask for JUCE to the adapter, then load it.
set(FETCHCONTENT_SOURCE_DIR_JUCE "${CMAKE_CURRENT_LIST_DIR}/juce-shim" CACHE PATH "" FORCE)
set(JUCE_DIR "${CMAKE_CURRENT_LIST_DIR}/juce-shim" CACHE PATH "" FORCE)
include(${CMAKE_CURRENT_LIST_DIR}/juce-shim/VgJuce.cmake)

# vg_add_juce_project(<name> (URL <url> SHA256 <sha> | GIT <repo> COMMIT <sha> [SUBMODULES <paths...>])
#                     [LOCAL <folder name>] [SUBDIR <dir with CMakeLists.txt>])
function(vg_add_juce_project name)
    cmake_parse_arguments(A "" "URL;SHA256;GIT;COMMIT;LOCAL;SUBDIR" "SUBMODULES" ${ARGN})
    if(VG_NATIVE_PLUGINS AND NOT name IN_LIST VG_NATIVE_PLUGINS)
        return()
    endif()
    string(TOUPPER "vg_src_${name}" up)
    if(VG_PLUGIN_SRC_ROOT AND A_LOCAL AND EXISTS "${VG_PLUGIN_SRC_ROOT}/${A_LOCAL}")
        set(FETCHCONTENT_SOURCE_DIR_${up} "${VG_PLUGIN_SRC_ROOT}/${A_LOCAL}")
    endif()
    if(A_GIT)
        # A pinned commit; only the submodules the plugin itself needs (never its own JUCE copy).
        FetchContent_Declare(vg_src_${name} GIT_REPOSITORY ${A_GIT} GIT_TAG ${A_COMMIT}
            GIT_SUBMODULES ${A_SUBMODULES} GIT_PROGRESS FALSE SOURCE_SUBDIR do-not-configure)
    else()
        FetchContent_Declare(vg_src_${name} URL ${A_URL} URL_HASH SHA256=${A_SHA256}
            SOURCE_SUBDIR do-not-configure DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    endif()
    FetchContent_MakeAvailable(vg_src_${name})
    set(dir "${vg_src_${name}_SOURCE_DIR}")
    if(A_SUBDIR)
        set(dir "${dir}/${A_SUBDIR}")
    endif()
    message(STATUS "VgJuce: project ${name} from ${dir}")
    add_subdirectory("${dir}" "${CMAKE_CURRENT_BINARY_DIR}/projects/${name}")
endfunction()

# ---- The plugins (the user's own; more JUCE projects go here the same way) ----
vg_add_juce_project(epiano LOCAL E-PianoVST SUBDIR vst
    URL https://github.com/BlinkZer0/E-PianoVST/archive/e35613a1ba452a26970dffb7863922282549cbd9.tar.gz
    SHA256 b76eb6e30c7168af3875b326429791855ffdb04a255d6bc27204584941df1791)
vg_add_juce_project(brass LOCAL Brass-Knuckles SUBDIR vst
    URL https://github.com/BlinkZer0/Brass-Knuckles/archive/e3da785776c00f2916cb41d8a061d3d6b1ddd0a8.tar.gz
    SHA256 f11286f5e1730e0b41a23554831c7ec2815bd7a9774c8453cc55d9c5a6c616d5)
vg_add_juce_project(pedalfaster LOCAL Pedal=Faster
    URL https://github.com/BlinkZer0/Pedal-Faster/archive/037df15ef53009a40b9f4fb272d10a08d9076530.tar.gz
    SHA256 2919368f70fa97c3cdce4f7928cbc0875107cea57a16573f00414d8f7060b97c)

# ---- Third-party open-source JUCE plugins (unmodified upstream source) ----
# Odin2 (GPL-3.0): https://github.com/TheWaveWarden/odin2
vg_add_juce_project(odin2 LOCAL odin2
    GIT https://github.com/TheWaveWarden/odin2.git COMMIT 265e9e227581357efb87ed7217381f8a40c5e2bf
    SUBMODULES libs/tuning-library libs/json)
