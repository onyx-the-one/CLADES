cmake_minimum_required(VERSION 3.18)
cmake_policy(SET CMP0135 NEW)
project(clades VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release build flags
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -O2)

include(FetchContent)

# ---- SDL2 ------------------------------------------------------------------
find_package(SDL2 QUIET)
if(NOT SDL2_FOUND)
    FetchContent_Declare(SDL2
        URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.2/SDL2-2.30.2.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON  CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL2)
endif()

# ---- Dear ImGui ------------------------------------------------------------
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.90.8.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
if(SDL2_FOUND)
    target_include_directories(imgui_lib PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(imgui_lib PUBLIC ${SDL2_LIBRARIES})
else()
    target_link_libraries(imgui_lib PUBLIC SDL2::SDL2-static)
endif()

# ---- ImPlot ----------------------------------------------------------------
FetchContent_Declare(implot
    URL https://github.com/epezent/implot/archive/refs/tags/v0.16.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(implot)

add_library(implot_lib STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)
target_include_directories(implot_lib PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot_lib PUBLIC imgui_lib)

# ---- CLADES ----------------------------------------------------------------
add_executable(clades
    main.cpp
    model.cpp
    io.cpp
    age_strat.cpp
    multi_strain.cpp
)
target_include_directories(clades PRIVATE .)
target_link_libraries(clades PRIVATE imgui_lib implot_lib)

# OpenGL
find_package(OpenGL REQUIRED)
target_link_libraries(clades PRIVATE OpenGL::GL)

if(SDL2_FOUND)
    target_include_directories(clades PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(clades PRIVATE ${SDL2_LIBRARIES})
else()
    target_link_libraries(clades PRIVATE SDL2::SDL2-static)
endif()

# Linux needs dl/pthread
if(UNIX)
    target_link_libraries(clades PRIVATE dl pthread)
endif()
