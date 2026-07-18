cmake_minimum_required(VERSION 3.20)

project(th07 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenGL COMPONENTS GLES3 REQUIRED)

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
    src/main.cpp
    src/graphics/Gles.cpp
    src/graphics/Software.cpp
    src/pbg4/Lzss.cpp
    src/pbg4/Pbg4Archive.cpp
    src/pbg4/Pbg4File.cpp
    src/utils.cpp
)

add_executable(th07 ${SOURCES})

target_include_directories(th07 PRIVATE src src/pbg4)

if(EMSCRIPTEN)
    target_compile_options(th07 PRIVATE
        "--use-port=sdl2"
        "--use-port=sdl2_image:formats=jpg"
        "--use-port=sdl2_ttf"
        "-O2"
    )
    target_link_options(th07 PRIVATE
        "--use-port=sdl2"
        "--use-port=sdl2_image:formats=jpg"
        "--use-port=sdl2_ttf"
        "--use-preload-plugins"
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
else()
    find_package(SDL2 REQUIRED)
    find_package(SDL2_ttf REQUIRED)
    find_package(SDL2_image REQUIRED)

    target_link_libraries(th07 PRIVATE
        SDL2::SDL2
        SDL2_ttf::SDL2_ttf
        SDL2_image::SDL2_image
        OpenGL::GLES3
    )
endif()
