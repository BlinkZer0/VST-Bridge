# SPDX-License-Identifier: LicenseRef-VstGhost-Proprietary
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
# JUCE is AGPLv3 or commercial. VST gHost is proprietary, so the bundled plugins are built under
# JUCE's commercial (Personal/Indie/Pro) terms; GPL/AGPL third-party plugins cannot be bundled.

include(FetchContent)

set(VG_PLUGIN_SRC_ROOT "" CACHE PATH "Folder with local checkouts of plugin projects (optional)")
set(VG_NATIVE_PLUGINS "" CACHE STRING "Only build these projects (names, ;-separated); empty = all")

# One JUCE for every project (projects' own JUCE tags are overridden).
FetchContent_Declare(vg_juce URL https://github.com/juce-framework/JUCE/archive/refs/tags/9.0.5.tar.gz
    URL_HASH SHA256=d92207f51ba728d3d195b6d5e93ecd049717a915fc250ff6844c32ea7272315a
    SOURCE_SUBDIR do-not-configure DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(vg_juce)
set(VG_JUCE_DIR "${vg_juce_SOURCE_DIR}")
# JUCE hard-codes JUCE_MODAL_LOOPS_PERMITTED=0 on Android. The adapter supplies the missing piece (a
# nested message loop, see JuceModalLoop.kt and vgn_adapter.cpp), so let the flag be set like on
# other platforms: plugins written with PopupMenu::show() / AlertWindow::showOkCancelBox() build.
set(_pd "${VG_JUCE_DIR}/modules/juce_core/system/juce_PlatformDefs.h")
file(READ "${_pd}" _pdtext)
string(REPLACE "#if JUCE_ANDROID
 /** @cond */
 #define JUCE_MODAL_LOOPS_PERMITTED 0"
               "#if JUCE_ANDROID && ! defined (JUCE_MODAL_LOOPS_PERMITTED)
 /** @cond */
 #define JUCE_MODAL_LOOPS_PERMITTED 0" _pdnew "${_pdtext}")
if(NOT _pdnew STREQUAL _pdtext)
    file(WRITE "${_pd}" "${_pdnew}")
    message(STATUS "VgJuce: juce_PlatformDefs.h patched (modal loops may be enabled on Android)")
endif()
set(VG_VGN_DIR "${CMAKE_CURRENT_LIST_DIR}/vgn")

# Route every way a project can ask for JUCE to the adapter, then load it.
set(FETCHCONTENT_SOURCE_DIR_JUCE "${CMAKE_CURRENT_LIST_DIR}/juce-shim" CACHE PATH "" FORCE)
set(JUCE_DIR "${CMAKE_CURRENT_LIST_DIR}/juce-shim" CACHE PATH "" FORCE)
include(${CMAKE_CURRENT_LIST_DIR}/juce-shim/VgJuce.cmake)

# vg_add_juce_project(<name> [URL <url> SHA256 <sha> | GIT <repo> COMMIT <sha> [SUBMODULES <paths...>]]
#                     [LOCAL <folder name>] [SUBDIR <dir with CMakeLists.txt>] [JUCER <project.jucer>])
# JUCER: a Projucer project instead of a CMakeLists.txt (read by juce-shim/VgJucer.cmake).
# TEST_ONLY: never part of a default build; only when named in VG_NATIVE_PLUGINS.
# With neither URL nor GIT the project is local-only: built from VG_PLUGIN_SRC_ROOT/<LOCAL> when that
# folder exists, otherwise skipped (not an error, so the default build stays reproducible).
function(vg_add_juce_project name)
    cmake_parse_arguments(A "TEST_ONLY" "URL;SHA256;GIT;COMMIT;LOCAL;SUBDIR;JUCER" "SUBMODULES" ${ARGN})
    if(VG_NATIVE_PLUGINS AND NOT name IN_LIST VG_NATIVE_PLUGINS)
        return()
    endif()
    if(A_TEST_ONLY AND NOT name IN_LIST VG_NATIVE_PLUGINS)
        return()   # test projects (third-party, not shippable) only when asked for by name
    endif()
    string(TOUPPER "vg_src_${name}" up)
    set(local "")
    if(VG_PLUGIN_SRC_ROOT AND A_LOCAL AND EXISTS "${VG_PLUGIN_SRC_ROOT}/${A_LOCAL}")
        set(local "${VG_PLUGIN_SRC_ROOT}/${A_LOCAL}")
        set(FETCHCONTENT_SOURCE_DIR_${up} "${local}")
    endif()
    if(NOT A_URL AND NOT A_GIT)
        if(NOT local)
            message(STATUS "VgJuce: project ${name} is local-only and ${A_LOCAL} is not under VG_PLUGIN_SRC_ROOT: skipped")
            return()
        endif()
        set(vg_src_${name}_SOURCE_DIR "${local}")
    elseif(A_GIT)
        # A pinned commit; only the submodules the plugin itself needs (never its own JUCE copy).
        FetchContent_Declare(vg_src_${name} GIT_REPOSITORY ${A_GIT} GIT_TAG ${A_COMMIT}
            GIT_SUBMODULES ${A_SUBMODULES} GIT_PROGRESS FALSE SOURCE_SUBDIR do-not-configure)
    else()
        FetchContent_Declare(vg_src_${name} URL ${A_URL} URL_HASH SHA256=${A_SHA256}
            SOURCE_SUBDIR do-not-configure DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    endif()
    if(A_URL OR A_GIT)
        FetchContent_MakeAvailable(vg_src_${name})
    endif()
    set(dir "${vg_src_${name}_SOURCE_DIR}")
    if(A_SUBDIR)
        set(dir "${dir}/${A_SUBDIR}")
    endif()
    message(STATUS "VgJuce: project ${name} from ${dir}")
    if(A_JUCER)
        vg_add_jucer_plugin(${name} "${dir}/${A_JUCER}")
    else()
        add_subdirectory("${dir}" "${CMAKE_CURRENT_BINARY_DIR}/projects/${name}")
    endif()
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
# No Strings (cello) and The Ham (organ): JUCE 8 + WebView2 web UIs. Not published as public
# archives yet, so local-only: -Pvg.pluginSrcRoot=<folder with "No Strings" and "The Ham">.
vg_add_juce_project(nostrings LOCAL "No Strings" SUBDIR vst)
vg_add_juce_project(theham LOCAL "The Ham" SUBDIR vst)

# Third-party GPL/AGPL plugins (e.g. Odin2, GPL-3) are not bundled: the app is proprietary. They
# build through the adapter (modal loops included, see vgn_adapter.cpp) but must not ship in it.
# Plugins with very large parameter layouts need clang's nesting limit raised:
add_compile_options(-fbracket-depth=1024)

# Projucer-project test case (GPL-3, test only, never shipped): Dexed 0.9.6 still has a .jucer.
# Build with -Pvg.nativePlugins=dexedjucer to exercise VgJucer.cmake.
vg_add_juce_project(dexedjucer TEST_ONLY JUCER Dexed.jucer
    GIT https://github.com/asb2m10/dexed.git COMMIT 11ae4d7ef597b2d52f45e59ab56b342252d0bd88   # v0.9.6
    SUBMODULES libs/tuning-library libs/surgesynthteam_tuningui)
