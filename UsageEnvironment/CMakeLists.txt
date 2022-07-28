project(UsageEnvironment)

file(GLOB HEADERS include/*.hh)
file(GLOB SOURCES *.cpp)

include_directories(include ../groupsock/include)

add_library(${PROJECT_NAME} STATIC ${HEADERS} ${SOURCES})

install(DIRECTORY include/ DESTINATION include/${PROJECT_NAME} FILES_MATCHING PATTERN "*.hh")

if(NOT BUILD_SHARED_LIBS)
        install(TARGETS ${PROJECT_NAME}
                RUNTIME DESTINATION bin
                LIBRARY DESTINATION lib
                FRAMEWORK DESTINATION lib
                BUNDLE DESTINATION bin
                ARCHIVE DESTINATION lib)
endif ()