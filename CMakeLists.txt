cmake_minimum_required(VERSION 3.20)

project(th07 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SOURCES
    src/AnmManager.cpp
    src/AnmVm.cpp
    src/AsciiManager.cpp
    src/BombData.cpp
    src/BulletManager.cpp
    src/Chain.cpp
    src/Controller.cpp
    src/EclManager.cpp
    src/EffectManager.cpp
    src/Ending.cpp
    src/EnemyEclInstr.cpp
    src/EnemyManager.cpp
    src/FileSystem.cpp
    src/GameErrorContext.cpp
    src/GameManager.cpp
    src/GameWindow.cpp
    src/Gui.cpp
    src/ItemManager.cpp
    src/MainMenu.cpp
    src/MidiOutput.cpp
    src/MusicRoom.cpp
    src/Player.cpp
    src/ReplayManager.cpp
    src/ResultScreen.cpp
    src/Rng.cpp
    src/ScreenEffect.cpp
    src/SoundPlayer.cpp
    src/Stage.cpp
    src/Supervisor.cpp
    src/TextHelper.cpp
    src/Touch.cpp
    src/main.cpp
    src/graphics/Gles.cpp
    src/graphics/Software.cpp
    src/pbg4/Lzss.cpp
    src/pbg4/Pbg4Archive.cpp
    src/pbg4/Pbg4File.cpp
    src/utils.cpp
)

if(ANDROID)
    add_library(main SHARED ${SOURCES})
    set(TH_EXEC_NAME "main")
else()
    add_executable(th07 ${SOURCES})
    set(TH_EXEC_NAME "th07")
endif()

target_include_directories(${TH_EXEC_NAME} PRIVATE src src/pbg4)

if(EMSCRIPTEN)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIGURATION>")

    target_compile_options(th07 PRIVATE
        "-O2"
    )
    target_link_options(th07 PRIVATE
        "--preload-file"
        "${CMAKE_SOURCE_DIR}/assets@/"
        "-sALLOW_MEMORY_GROWTH"
        "-sMAX_WEBGL_VERSION=2"
        "--use-preload-cache"
        "-O2"
        "-sINVOKE_RUN=0"
        "-sEXPORTED_RUNTIME_METHODS=['callMain']"
        "-lidbfs.js"
        "--shell-file"
        "${CMAKE_SOURCE_DIR}/resources/shell.html"
    )
    set(CMAKE_EXECUTABLE_SUFFIX ".html")
endif()

if(NOT ANDROID AND NOT EMSCRIPTEN)
    find_package(OpenGL COMPONENTS GLES3 REQUIRED)
endif()

if(EMSCRIPTEN)
    # emscripten has no sdl3_image port, so it has to use a vendored sdl
    set(USE_VENDORED_SDL ON)
else()
    find_package(SDL3 QUIET)
    find_package(SDL3_ttf QUIET)
    find_package(SDL3_image QUIET)

    if(NOT SDL3_FOUND OR NOT SDL3_ttf_FOUND OR NOT SDL3_image_FOUND)
        message(STATUS "system SDL3 libraries not found. using vendored sdl3")
        set(USE_VENDORED_SDL ON)
    else()
        set(USE_VENDORED_SDL OFF)
    endif()
endif()

if(USE_VENDORED_SDL)
    set(SDLIMAGE_ANI OFF)
    set(SDLIMAGE_AVIF OFF)
    set(SDLIMAGE_BMP OFF)
    set(SDLIMAGE_GIF OFF)
    set(SDLIMAGE_JPG ON) # really, we only need jpg
    set(SDLIMAGE_BACKEND_STB ON)
    set(SDLIMAGE_JXL OFF)
    set(SDLIMAGE_LBM OFF)
    set(SDLIMAGE_PCX OFF)
    set(SDLIMAGE_PNG OFF)
    set(SDLIMAGE_PNM OFF)
    set(SDLIMAGE_QOI OFF)
    set(SDLIMAGE_SVG OFF)
    set(SDLIMAGE_TGA OFF)
    set(SDLIMAGE_TIF OFF)
    set(SDLIMAGE_WEBP OFF)
    set(SDLIMAGE_XCF OFF)
    set(SDLIMAGE_XPM OFF)
    set(SDLIMAGE_XV OFF)
    add_subdirectory(vendored/SDL EXCLUDE_FROM_ALL)
    add_subdirectory(vendored/SDL_image EXCLUDE_FROM_ALL)
    add_subdirectory(vendored/SDL_ttf EXCLUDE_FROM_ALL)
endif()

target_link_libraries(${TH_EXEC_NAME} PRIVATE
    SDL3::SDL3
    SDL3_image::SDL3_image
    SDL3_ttf::SDL3_ttf
)

if(ANDROID)
    target_link_libraries(${TH_EXEC_NAME} PRIVATE GLESv3)
elseif(NOT EMSCRIPTEN)
    target_link_libraries(${TH_EXEC_NAME} PRIVATE OpenGL::GLES3)
endif()
