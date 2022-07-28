project(live555ProxyServer)

file(GLOB HEADERS *.hh)
file(GLOB SOURCES *.cpp)

include_directories(./ include ../UsageEnvironment/include ../groupsock/include ../liveMedia/include ../BasicUsageEnvironment/include)

set(LIBS
    liveMedia
    groupsock
    BasicUsageEnvironment
    UsageEnvironment
    )

add_executable(${PROJECT_NAME} ${HEADERS} ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_directories(${PROJECT_NAME} PRIVATE ${OPENSSL_LIB_DIR})
target_link_libraries(${PROJECT_NAME} ${LIBS} ${OPENSSL_LIBS} ${RT_LIBS})


install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin
        BUNDLE DESTINATION bin
        LIBRARY DESTINATION lib
        FRAMEWORK DESTINATION lib
        BUNDLE DESTINATION bin
        ARCHIVE DESTINATION lib)
