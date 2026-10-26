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

# Executable
add_executable(netwatch netwatch.cpp)

if(WIN32)
    target_link_libraries(netwatch PRIVATE ws2_32 iphlpapi)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(netwatch PRIVATE Threads::Threads)
endif()

include(GNUInstallDirs)
install(TARGETS netwatch
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Symlink netping -> netwatch on non-Windows
if(UNIX)
    install(CODE "execute_process(COMMAND \${CMAKE_COMMAND} -E create_symlink netwatch \${CMAKE_INSTALL_FULL_BINDIR}/netping)")
endif()
