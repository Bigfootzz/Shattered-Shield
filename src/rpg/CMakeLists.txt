# Shattered Shield RPG layer: sqlite3 (vendored amalgamation 3.54.1, public
# domain) + the persistence module. Built as a static lib so both the client
# (whose dedicated-server mode runs the game server) and the standalone
# server can link it.
add_library(rpg STATIC
    rpg.cpp
    abilities.cpp
    items.cpp
    sqlite3.c)

target_include_directories(rpg PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Threads REQUIRED)
target_link_libraries(rpg PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
