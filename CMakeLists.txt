cmake_minimum_required(VERSION 3.20)

project(th07 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TH_EXTERNAL_ASSETS "Do not bundle assets (on platforms that bundle them) and use the user/executable directories instead" OFF)
set(TH07_IOS_VERSION "0.4.0" CACHE STRING "iOS marketing version")
set(TH07_IOS_BUILD "28" CACHE STRING "iOS build number")
set(TH07_IOS_BUNDLE_ID "com.th07.sdl3.ios" CACHE STRING "iOS bundle identifier")

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
    src/MobileDiagnostics.cpp
    src/MobileUi.cpp
    src/MultiplayerResources.cpp
    src/Online.cpp
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
    src/pbg4/Lzss.cpp
    src/pbg4/Pbg4Archive.cpp
    src/pbg4/Pbg4File.cpp
    src/sj2utf8/sj2utf8.cpp
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

if(TH_EXTERNAL_ASSETS)
    target_compile_definitions(${TH_EXEC_NAME} PRIVATE TH_EXTERNAL_ASSETS)
endif()

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

if(NOT ANDROID AND NOT EMSCRIPTEN AND NOT APPLE)
    find_package(OpenGL COMPONENTS GLES3 REQUIRED)
endif()

if(APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)

    set_source_files_properties(src/SoundPlayer.cpp PROPERTIES LANGUAGE OBJCXX)
    set(APP_RESOURCES "${CMAKE_SOURCE_DIR}/assets/th07.dat"
                      "${CMAKE_SOURCE_DIR}/assets/thbgm.dat"
                      "${CMAKE_SOURCE_DIR}/assets/msgothic.ttc")

if(NOT TH_EXTERNAL_ASSETS)
    target_sources(${TH_EXEC_NAME} PRIVATE ${APP_RESOURCES})
    set_source_files_properties(${APP_RESOURCES} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
endif()

if (IOS OR NOT TH_EXTERNAL_ASSETS)
    set_target_properties(${TH_EXEC_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "東方妖々夢"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${TH07_IOS_BUNDLE_ID}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${TH07_IOS_VERSION}"
    )
endif()

    if(IOS)
        set(BUILD_SHARED_LIBS OFF)
        configure_file("${CMAKE_SOURCE_DIR}/ios/Info.plist.in"
                       "${CMAKE_BINARY_DIR}/th07-Info.plist" @ONLY)
        set_target_properties(${TH_EXEC_NAME} PROPERTIES
            MACOSX_BUNDLE_INFO_PLIST "${CMAKE_BINARY_DIR}/th07-Info.plist"
            XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${TH07_IOS_BUNDLE_ID}"
            XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "14.0"
            XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++17"
            XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++"
            XCODE_ATTRIBUTE_GCC_OPTIMIZATION_LEVEL "3"
            XCODE_ATTRIBUTE_LLVM_LTO "YES_THIN"
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
            XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
        )
        target_compile_definitions(${TH_EXEC_NAME} PRIVATE
            TH07_MOBILE=1
            TH07_IOS=1
            TH07_IOS_VERSION="${TH07_IOS_VERSION}"
            TH07_IOS_BUILD="${TH07_IOS_BUILD}")
        target_compile_options(${TH_EXEC_NAME} PRIVATE
            -fsigned-char
            -fno-fast-math
            -ffp-contract=off)
        set_source_files_properties(ios/LaunchScreen.storyboard
                                    PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        set_source_files_properties(ios/Assets.xcassets
                                    PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
        target_sources(${TH_EXEC_NAME} PRIVATE
            ios/LaunchScreen.storyboard
            ios/Assets.xcassets
            ios/LocalNetworkPermission.mm
            ios/OnlineLauncher.mm
            ios/OnlineTextInput.mm
            ios/BluetoothPeerTransport.mm)
        target_link_libraries(${TH_EXEC_NAME} PRIVATE
            "-framework OpenGLES"
            "-framework AudioToolbox"
            "-framework AVFoundation"
            "-framework CoreAudio"
            "-framework CoreGraphics"
            "-framework QuartzCore"
            "-framework UIKit"
            "-framework Foundation"
            "-framework MultipeerConnectivity"
        )
    else()
        target_compile_definitions(${TH_EXEC_NAME} PRIVATE USING_GL)
        set_target_properties(${TH_EXEC_NAME} PROPERTIES
            XCODE_ATTRIBUTE_INFOPLIST_KEY_CFBundleDisplayName "東方妖々夢　～ Perfect Cherry Blossom"
        )
        find_package(OpenGL REQUIRED)
        target_link_libraries(${TH_EXEC_NAME} PRIVATE
            OpenGL::GL
            "-framework AudioToolbox"
            "-framework CoreAudio"
            "-framework CoreVideo"
            "-framework Cocoa"
            "-framework IOKit"
            "-framework ForceFeedback"
            "-framework Carbon"
        )
    endif()
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
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static dependencies" FORCE)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

    # TH07 renders directly through OpenGL ES. SDL's GPU, Metal and Vulkan
    # backends are unused, and current SDL GPU Metal sources reference
    # BC texture APIs that Apple explicitly marks unavailable on iOS.
    if(IOS)
        set(SDL_GPU OFF CACHE BOOL "" FORCE)
        set(SDL_RENDER ON CACHE BOOL "" FORCE)
        set(SDL_RENDER_GPU OFF CACHE BOOL "" FORCE)
        set(SDL_RENDER_METAL OFF CACHE BOOL "" FORCE)
        set(SDL_METAL OFF CACHE BOOL "" FORCE)
        set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
        set(SDL_RENDER_VULKAN OFF CACHE BOOL "" FORCE)
        set(SDL_OPENGLES ON CACHE BOOL "" FORCE)
    endif()

    set(SDLIMAGE_BACKEND_STB ON)
    set(SDLIMAGE_BACKEND_IMAGEIO OFF)

    set(SDLIMAGE_ANI OFF)
    set(SDLIMAGE_AVIF OFF)
    set(SDLIMAGE_BMP OFF)
    set(SDLIMAGE_GIF OFF)
    set(SDLIMAGE_JPG ON) # really, we only need jpg
    set(SDLIMAGE_JPG_SAVE OFF)
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

    set(SDLTTF_VENDORED ON)
    set(SDLTTF_HARFBUZZ OFF)
    set(SDLTTF_PLUTOSVG OFF)
    set(SDLTTF_SAMPLES OFF)
    set(SDLTTF_INSTALL OFF)

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
elseif(NOT EMSCRIPTEN AND NOT APPLE)
    target_link_libraries(${TH_EXEC_NAME} PRIVATE OpenGL::GLES3)
endif()
