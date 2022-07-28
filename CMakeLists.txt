cmake_minimum_required(VERSION 3.15)

project(live555)


set (CMAKE_CXX_STANDARD 11)


if (WIN32 OR MINGW)
    list(APPEND RT_LIBS ws2_32;crypt32)
endif()
if (UNIX)
    list(APPEND RT_LIBS dl;pthread)
endif(UNIX)


# Set some platform specific defines that we need.
if (WIN32 OR MINGW)
    add_definitions( -D_CRT_SECURE_NO_WARNINGS -D_CRT_SECURE_NO_DEPRECATE -DNO_GETIFADDRS )
elseif(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 10.9)
    add_definitions( -DSOCKLEN_T=socklen_t -DHAVE_SOCKADDR_LEN=1 -DTIME_BASE=int -stdlib=libc++ -DNEWLOCALE_NOT_USED )
else()
    add_definitions( -DSOCKLEN_T=socklen_t -D_LARGEFILE_SOURCE=1 -D_FILE_OFFSET_BITS=64)
endif ()

if (XLOCALE_NOT_USED)
    add_definitions(-DXLOCALE_NOT_USED)
endif ()

add_definitions( -DALLOW_RTSP_SERVER_PORT_REUSE )

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/INSTALL)
    message( "CMAKE_INSTALL_PREFIX is now set to ${CMAKE_INSTALL_PREFIX}")
else()
    message( "CMAKE_INSTALL_PREFIX is set to ${CMAKE_INSTALL_PREFIX}")
endif()

add_subdirectory(UsageEnvironment)
add_subdirectory(groupsock)
add_subdirectory(BasicUsageEnvironment)
add_subdirectory(liveMedia)
add_subdirectory(mediaServer)
add_subdirectory(proxyServer)
add_subdirectory(hlsProxy)
add_subdirectory(testProgs)
add_subdirectory(svlive555)

if(BUILD_SHARED_LIBS)
    install(FILES cmake/shared/live555-config.cmake DESTINATION lib/cmake/live555/ )
else()
    install(FILES cmake/static/live555-config.cmake DESTINATION lib/cmake/live555/ )
endif()