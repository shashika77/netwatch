cmake_minimum_required(VERSION 3.12)
project(NetWatch VERSION 3.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /O2 /permissive-)
else()
    add_compile_options(-Wall -Wextra -pedantic -O2)
endif()

# Executables
add_executable(netwatch netwatch.cpp)
add_executable(netping netping.cpp)

if(WIN32)
    target_link_libraries(netwatch PRIVATE ws2_32 iphlpapi)
    target_link_libraries(netping PRIVATE ws2_32 iphlpapi)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(netwatch PRIVATE Threads::Threads)
    target_link_libraries(netping PRIVATE Threads::Threads)
endif()

include(GNUInstallDirs)
install(TARGETS netwatch netping
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
