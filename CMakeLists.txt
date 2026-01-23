cmake_minimum_required(VERSION 3.20)
project(toasty LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(WIN32)
    # Windows build
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    
    add_executable(toasty main.cpp resource.rc)
    
    target_link_libraries(toasty PRIVATE
        windowsapp
        runtimeobject
        shlwapi
        shell32
        ole32
        propsys
    )
    
    target_compile_options(toasty PRIVATE
        $<$<CONFIG:Release>:/O1 /GL>
    )
    target_link_options(toasty PRIVATE
        $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
    )

elseif(APPLE)
    # macOS build
    enable_language(OBJCXX)
    
    add_executable(toasty main_mac.mm)
    
    # Find and link macOS frameworks
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
    find_library(USERNOTIFICATIONS_FRAMEWORK UserNotifications REQUIRED)
    
    target_link_libraries(toasty PRIVATE
        ${FOUNDATION_FRAMEWORK}
        ${APPKIT_FRAMEWORK}
        ${USERNOTIFICATIONS_FRAMEWORK}
    )
    
    # Copy icons to build directory
    file(GLOB ICON_FILES "${CMAKE_SOURCE_DIR}/icons/*.png")
    foreach(ICON ${ICON_FILES})
        get_filename_component(ICON_NAME ${ICON} NAME)
        configure_file(${ICON} ${CMAKE_BINARY_DIR}/icons/${ICON_NAME} COPYONLY)
    endforeach()
    
    # Optimize for size in Release
    target_compile_options(toasty PRIVATE
        $<$<CONFIG:Release>:-Os>
    )

else()
    message(FATAL_ERROR "Unsupported platform. Toasty supports Windows and macOS only.")
endif()
