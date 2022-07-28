project(groupsock)

file(GLOB HEADERS include/*.h include/*.hh)
file(GLOB SOURCES *.cpp *.c)

include_directories(include ../UsageEnvironment/include)

add_library(${PROJECT_NAME} STATIC ${HEADERS} ${SOURCES})

install(DIRECTORY include/ DESTINATION include/${PROJECT_NAME} FILES_MATCHING PATTERN "*.h*")

if(NOT BUILD_SHARED_LIBS)
        install(TARGETS ${PROJECT_NAME}
                RUNTIME DESTINATION bin
                LIBRARY DESTINATION lib
                FRAMEWORK DESTINATION lib
                BUNDLE DESTINATION bin
                ARCHIVE DESTINATION lib)
endif ()
