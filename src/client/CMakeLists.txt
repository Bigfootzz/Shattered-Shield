add_definitions(-DFPS -DCLIENT -DRR_SERVER_BINARY="server_${BUILD_PLATFORM_BIN}_${BUILD_TARGET_ARCH}" -DRR_SERVER_BINARY_DIR="bin_${BUILD_PLATFORM_BIN}" -DRR_INSTALL_LOCATIONS="${BUILD_SYSTEM_INSTALL_LOCATIONS}")

option(WITH_BULLET "Build with bullet physics support" 1)

if(${WITH_BULLET})
    include_directories(${BULLET_INCLUDE_DIRS})
    add_definitions(-DWITH_BULLET)
    message(STATUS "Bullet enabled (${BULLET_LIBRARIES})")
    set(BUILD_EXTRA_LIBS "${BUILD_EXTRA_LIBS};${BULLET_LIBRARIES}")
endif()

if(${BUILD_CC})
    add_definitions(-DRR_USE_CV=1)
endif()

if(${BUILD_TARGET_WINDOWS})
    set(BUILD_EXTRA_LIBS ${BUILD_EXTRA_LIBS} opengl32 ws2_32 winmm)
    add_definitions(-mwindows)
elseif(${BUILD_TARGET_LINUX})
    set(BUILD_EXTRA_LIBS ${BUILD_EXTRA_LIBS} dl rt)
elseif(${BUILD_TARGET_SOLARIS})
    set(BUILD_EXTRA_LIBS ${BUILD_EXTRA_LIBS} socket nsl)
endif()

set(CLIENT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/crypto
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/geom
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/stream
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/tools
    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/zip

    ${CMAKE_CURRENT_SOURCE_DIR}/../shared/version

    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/3dgui
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/bih
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/blend
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/client
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/command
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/console
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/decal
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/dynlight
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/grass
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/main
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/material
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/menus
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/movie
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/normal
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/octa
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/octaedit
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/octarender
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/physics
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/pvs
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/rendergl
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/rendermodel
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/renderparticles
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/rendersky
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/rendertext
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/renderva
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/server
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/shader
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/sound
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/texture
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/translate
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/water
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/world
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/worldio
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/serverbrowser
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/shadowmap
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/lightmap
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/glare
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/blob
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/localserver
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/jsonNews
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/webRadio

    ${CMAKE_CURRENT_SOURCE_DIR}/../fpsgame/ai/ai
    ${CMAKE_CURRENT_SOURCE_DIR}/../fpsgame/ai/bot
    ${CMAKE_CURRENT_SOURCE_DIR}/../fpsgame/ai/monster
    ${CMAKE_CURRENT_SOURCE_DIR}/../fpsgame/ai/waypoint

    ${CMAKE_CURRENT_SOURCE_DIR}/../fpsgame/client
    ${CMAKE_CURRENT_SOURCE_DIR}/../fpsgame/entities
    ${CMAKE_CURRENT_SOURCE_DIR}/../fpsgame/fps
    ${CMAKE_CURRENT_SOURCE_DIR}/../fpsgame/gamemode.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../fpsgame/movable
    ${CMAKE_CURRENT_SOURCE_DIR}/../fpsgame/render
    ${CMAKE_CURRENT_SOURCE_DIR}/../fpsgame/scoreboard
    ${CMAKE_CURRENT_SOURCE_DIR}/../fpsgame/server
    ${CMAKE_CURRENT_SOURCE_DIR}/../fpsgame/weapon
    ${CMAKE_CURRENT_SOURCE_DIR}/../fpsgame/auth
)

if(${BUILD_TARGET_OSX})
    set(CLIENT_SOURCES
        ${CLIENT_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/../engine/macutils.mm)
elseif(${BUILD_TARGET_WINDOWS})
    set(CLIENT_SOURCES
        ${CLIENT_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/../vcpp/SDL_win32_main.c
    )

    option(CLIENT_USE_WINDRES "Add icon to executable." 0)

    if(${CLIENT_USE_WINDRES})
        set(CLIENT_SOURCES
            ${CLIENT_SOURCES}
            ${CMAKE_CURRENT_SOURCE_DIR}/../vcpp/rr.rc
        )

        set(CMAKE_RC_COMPILER_INIT windres)
        ENABLE_LANGUAGE(RC)
        SET(CMAKE_RC_COMPILE_OBJECT "<CMAKE_RC_COMPILER> -O coff -I ${CMAKE_CURRENT_SOURCE_DIR}/.. -i <SOURCE> -o <OBJECT>")
    endif()
endif()

set(RR_CLIENT_BINARY "client_${BUILD_PLATFORM_BIN}_${BUILD_TARGET_ARCH}${BUILD_VERSION_POSTFIX}" CACHE INTERNAL "Revelade revolution client binary name.")
add_executable(${RR_CLIENT_BINARY} ${CLIENT_SOURCES})

if(${WITH_BULLET})
    # System Bullet is used now; no ExternalProject "Bullet" target exists.
endif()

target_link_libraries(${RR_CLIENT_BINARY}
    ${ENET_LIBRARIES}
    ${SDL_LIBRARIES}
    ${OPENGL_LIBRARIES}
    ${ZLIB_LIBRARIES}
    ${UV_LIBRARIES}
    ${TOMCRYPT_LIBRARIES}
    ${HTTP_PARSER_LIBRARIES}
    ${YAJL_LIBRARIES}
    ${BUILD_EXTRA_LIBS}
    rpg
)

# SDL 1.2 built without x11-shared needs X11 at final link.
if(NOT ${BUILD_TARGET_WINDOWS} AND NOT ${BUILD_TARGET_OSX})
    find_package(X11 REQUIRED)
    target_link_libraries(${RR_CLIENT_BINARY} ${X11_LIBRARIES})
endif()

install(TARGETS
    ${RR_CLIENT_BINARY}
    RUNTIME DESTINATION ${INSTALL_RUNTIME_DIR}
    LIBRARY DESTINATION ${INSTALL_LIBRARY_DIR}
    ARCHIVE DESTINATION ${INSTALL_ARCHIVE_DIR})
