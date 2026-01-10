cmake_minimum_required(VERSION 3.20)
project(toasty LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Use static runtime for standalone exe
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(toasty main.cpp resource.rc)

# Link Windows Runtime libraries
target_link_libraries(toasty PRIVATE
    windowsapp
    runtimeobject
    shlwapi
    shell32
    ole32
    propsys
)

# Optimize for size in Release
target_compile_options(toasty PRIVATE
    $<$<CONFIG:Release>:/O1 /GL>
)
target_link_options(toasty PRIVATE
    $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
)
