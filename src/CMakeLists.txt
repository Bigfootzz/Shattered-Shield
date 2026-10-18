find_package(ZLIB REQUIRED)

set(OpenCV_FOUND 0)
find_package(OpenCV)
find_package(OpenGL)
find_package(PkgConfig)

option(BUILD_CLIENT "Build the client" ${OPENGL_FOUND})
option(BUILD_CC "Build the camera controller" 0)
option(BUILD_SERVER "Build the server" 1)

message(STATUS "Detecting LuaJIT...")
if(NOT BUILD_TARGET_WINDOWS AND PKG_CONFIG_FOUND)
    message(STATUS "Using pkg-config to detect LuaJIT...")
    pkg_check_modules(LUAJIT QUIET luajit)
    if(NOT LUAJIT_FOUND)
        pkg_check_modules(LUAJIT QUIET luajit-5.1)
    endif()
    if(LUAJIT_FOUND)
        message(STATUS "Found LuaJIT.")
        message(STATUS "include: ${LUAJIT_INCLUDE_DIRS}")
    else()
        message(FATAL_ERROR "LuaJIT not found.")
    endif()
elseif(NOT DEFINED LUAJIT_LIBRARIES)
    message(STATUS "Using local LuaJIT.")
    set(LUAJIT_LIBRARY_DIRS "")
    set(LUAJIT_INCLUDE_DIRS "")
    set(LUAJIT_LIBRARIES luajit)
endif()

if(${BUILD_TARGET_OSX})
    set(BUILD_EXTRA_LIBS ${BUILD_EXTRA_LIBS} -pagezero_size 10000 -image_base 100000000)
elseif(${BUILD_TARGET_WINDOWS} AND ${CMAKE_C_COMPILER} MATCHES "mingw")
    set(BUILD_EXTRA_LIBS ${BUILD_EXTRA_LIBS} -static-libgcc)
endif()

message(STATUS "Detecting SDL...")
if(NOT ${BUILD_TARGET_WINDOWS} AND NOT ${BUILD_TARGET_OSX})
    if(NOT BUILD_SDL_LOCAL AND PKG_CONFIG_FOUND)
        message(STATUS "Using pkg-config to detect SDL...")
        pkg_check_modules(SDL QUIET sdl2)
        if(SDL_FOUND)
            message(STATUS "Found SDL2.")
            message(STATUS "include: ${SDL_INCLUDE_DIRS}")
            link_directories(${SDL_LIBRARY_DIRS})
        else()
            message(FATAL_ERROR "SDL2 not found.")
        endif()
        pkg_check_modules(SDL2_IMAGE REQUIRED SDL2_image)
        pkg_check_modules(SDL2_MIXER REQUIRED SDL2_mixer)
    else()
        if(PKG_CONFIG_FOUND)
            message(STATUS "Using local SDL.")
        else()
            message(STATUS "Using local SDL. (could not find PKG_CONFIG)")
        endif()
        set(SDL_LIBRARIES "SDL")
    endif()
    set(SDL_LIBRARIES "${SDL_LIBRARIES};${SDL2_IMAGE_LIBRARIES};${SDL2_MIXER_LIBRARIES}")
    include_directories(${SDL2_IMAGE_INCLUDE_DIRS} ${SDL2_MIXER_INCLUDE_DIRS})
    link_directories(${SDL2_IMAGE_LIBRARY_DIRS} ${SDL2_MIXER_LIBRARY_DIRS})
    add_definitions(-D_REENTRANT -D_THREAD_SAFE)
elseif(${BUILD_TARGET_OSX})
    find_library(SDL_LIBRARY SDL)
    find_library(SDL_IMAGE_LIBRARY SDL_image)
    find_library(SDL_MIXER_LIBRARY SDL_mixer)

    find_library(CORESERVICES_LIBRARY CoreServices)
    find_library(COCOA_LIBRARY Cocoa)

    set(SDL_LIBRARIES "${SDL_LIBRARY};${SDL_MIXER_LIBRARY};${SDL_IMAGE_LIBRARY};${CORESERVICES_LIBRARY};${COCOA_LIBRARY}")
    
    find_path(SDL_INCLUDE_DIR SDL.h)
    find_path(SDL_IMAGE_INCLUDE_DIR SDL_image.h)
    find_path(SDL_MIXER_INCLUDE_DIR SDL_mixer.h)
    set(SDL_INCLUDE_DIRS "${SDL_INCLUDE_DIR};${SDL_IMAGE_INCLUDE_DIR};${SDL_MIXER_INCLUDE_DIR}")
endif()

CONFIGURE_FILE(
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/rr_version.h.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/rr_version.h)

CONFIGURE_FILE(
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/config.h.cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/shared/config.h)

# Bundled libraries/ dir is not in the repo; use system packages instead.
pkg_check_modules(ENET REQUIRED libenet)
pkg_check_modules(UV REQUIRED libuv)
pkg_check_modules(YAJL REQUIRED yajl)
link_directories(${ENET_LIBRARY_DIRS} ${UV_LIBRARY_DIRS} ${YAJL_LIBRARY_DIRS})
find_path(HTTP_PARSER_INCLUDE_DIRS http_parser.h)
find_library(HTTP_PARSER_LIBRARIES http_parser)
if(NOT HTTP_PARSER_INCLUDE_DIRS OR NOT HTTP_PARSER_LIBRARIES)
    message(FATAL_ERROR "http-parser not found.")
endif()

if(${BUILD_SERVER})
    option(ENABLE_OPENDBX_DATABASE 0 "Enable OpenDBX database support.")
    if(${ENABLE_OPENDBX_DATABASE})
        message(STATUS "OpenDBX enabled.")
        add_subdirectory(libraries/OpenDBX-uv)
        add_subdirectory(libraries/OpenDBX-uv-lua)
    endif()
    add_subdirectory(libraries/luvit)
    add_subdirectory(libraries/luapp)
endif()

if(${BUILD_CLIENT})
    # Bundled libraries/bullet3 is not in the repo; use system Bullet instead.
    find_package(Bullet REQUIRED)
    set(BULLET_LIBRARIES
        ${BULLET_SOFTBODY_LIBRARY}
        ${BULLET_DYNAMICS_LIBRARY}
        ${BULLET_COLLISION_LIBRARY}
        ${BULLET_MATH_LIBRARY})
    set(BULLET_INCLUDE_DIRS ${BULLET_INCLUDE_DIRS})
    message(STATUS "Using system Bullet (${BULLET_LIBRARIES})")
endif()

include_directories(
    ${ENET_INCLUDE_DIRS}
    ${UV_INCLUDE_DIRS}
    ${LUVIT_INCLUDE_DIRS}
    ${TOMCRYPT_INCLUDE_DIRS}
    ${YAJL_INCLUDE_DIRS}
    ${HTTP_PARSER_INCLUDE_DIRS}
    ${SDL_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/shared
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
    ${CMAKE_CURRENT_SOURCE_DIR}/fpsgame
)

if(${BUILD_CC})
    add_subdirectory(controller-camera)
else()
    message(STATUS "NOT building the controller camera")
endif()

if(${BUILD_CLIENT})
    add_subdirectory(client)
else()
    message(WARNING "not building the client")
endif()

if(${BUILD_SERVER})
    add_subdirectory(server)
else()
    message(WARNING "not building the server")
endif()

add_custom_target(doc_dir
    ${CMAKE_COMMAND} -E make_directory ${INSTALL_DOC_DIR})

add_custom_target(doc
    DEPENDS doc_dir)

find_package(Doxygen)
if(DOXYGEN_FOUND)
    set(RR_DOC_DIR ${INSTALL_DOC_DIR}/api/revelade-revolution)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile.in
        ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile)

    add_custom_target(doc_rr_api_dir
        ${CMAKE_COMMAND} -E make_directory ${RR_DOC_DIR})

    add_custom_target(doc_rr_api
        ${DOXYGEN_EXECUTABLE}
        ${CMAKE_CURRENT_BINARY_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS doc_rr_dir
        COMMENT "Generating API documentation with Doxygen" VERBATIM)

    set(RR_WANT_DOXYGEN 0 CACHE INTERNAL "want revelade revolution API documentation")
    if(${RR_WANT_DOXYGEN})
        add_dependencies(doc doc_rr_api)
    endif()
endif(DOXYGEN_FOUND)

find_package(Sphinx)

if(SPHINX_FOUND)
    add_subdirectory(docs)
endif()

if (${BUILD_SERVER})
    add_custom_target(cfgcheck
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
        DEPENDS ${LUAPP_BINARY_NAME}
        COMMAND ${LUAPP_BINARY_NAME} -v cfgfix --check-only)
        
endif()

IF(${CMAKE_BUILD_TYPE} MATCHES "Debug")
    add_custom_target(src_validate ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules/ValidateSource.cmake ALL)
else()
    add_custom_target(src_validate ${CMAKE_COMMAND} -DSRC_DIR=${CMAKE_CURRENT_SOURCE_DIR} -P ${CMAKE_CURRENT_SOURCE_DIR}/CMakeModules/ValidateSource.cmake)
endif()
