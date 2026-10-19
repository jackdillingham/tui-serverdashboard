cmake_minimum_required(VERSION 3.20)

project(serverdash LANGUAGES CXX)

include(FetchContent)

set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v7.0.3
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(ftxui)

find_package(Threads REQUIRED)

add_executable(
  serverdash
  src/main.cpp
  src/dashboard.cpp
  src/kubernetes_client.cpp
  src/process.cpp
)
target_include_directories(serverdash PRIVATE src/include)
target_compile_features(serverdash PRIVATE cxx_std_17)
target_link_libraries(serverdash PRIVATE ftxui::ftxui Threads::Threads)
