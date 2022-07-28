project(liveMedia)

file(GLOB HEADERS include/*.hh)
file(GLOB SOURCES *.cpp *.c)

include_directories(include ../UsageEnvironment/include ../BasicUsageEnvironment/include ../groupsock/include)

add_library(${PROJECT_NAME} ${HEADERS} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ${OPENSSL_INCLUDE_DIR})

if(BUILD_SHARED_LIBS)
        if (APPLE)
                target_link_libraries(${PROJECT_NAME} UsageEnvironment groupsock BasicUsageEnvironment)
                set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-Wl,-all_load")
        elseif (MSVC)
                target_link_libraries(${PROJECT_NAME} UsageEnvironment groupsock BasicUsageEnvironment)
                set_target_properties(${PROJECT_NAME} "/WHOLEARCHIVE:UsageEnvironment /WHOLEARCHIVE:groupsock /WHOLEARCHIVE:BasicUsageEnvironment")
        else()
                SET (LIB_UsageEnvironment -Wl,--whole-archive UsageEnvironment -Wl,--no-whole-archive)
                SET (LIB_groupsock -Wl,--whole-archive groupsock -Wl,--no-whole-archive)
                SET (LIB_BasicUsageEnvironment -Wl,--whole-archive BasicUsageEnvironment -Wl,--no-whole-archive)
                target_link_libraries(${PROJECT_NAME} ${LIB_UsageEnvironment} ${LIB_groupsock} ${LIB_BasicUsageEnvironment})
        endif ()
        target_link_directories(${PROJECT_NAME} PRIVATE ${OPENSSL_LIB_DIR})
        target_link_libraries(${PROJECT_NAME} ${LIBS} ${OPENSSL_LIBS})
endif ()

install(DIRECTORY include/ DESTINATION include/${PROJECT_NAME} FILES_MATCHING PATTERN "*.h*")

install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin
        BUNDLE DESTINATION bin
        LIBRARY DESTINATION lib
        FRAMEWORK DESTINATION lib
        BUNDLE DESTINATION bin
        ARCHIVE DESTINATION lib)
