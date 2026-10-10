# Fails when game art could be embedded in the exe. generated/rexglue.cmake
# embeds metadata/icons whenever that folder exists, and `rexglue init
# achievements` writes the disc's title image there. Released binaries must
# carry no game data (CLAUDE.md), so the folder must never exist.
#
# Runs at configure time (include() from CMakeLists.txt) and before every build
# of the game exe, as a script:
#   cmake -DWWE_SOURCE_ROOT=<repo> -P cmake/no_game_art.cmake
if(NOT DEFINED WWE_SOURCE_ROOT)
    get_filename_component(WWE_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()
if(EXISTS "${WWE_SOURCE_ROOT}/metadata/icons")
    message(FATAL_ERROR
        "${WWE_SOURCE_ROOT}/metadata/icons exists. It holds game art that the "
        "build would embed in wweallstars.exe; delete the folder. The exe uses "
        "the project's original icon (art/icon).")
endif()
