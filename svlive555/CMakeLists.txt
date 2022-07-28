cmake_minimum_required(VERSION 3.2)
include(GenerateExportHeader)
project(svlive555)


# Export factory methods

if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" )
    set(CMAKE_FRAMEWORK TRUE)
endif()


set(SVLIVE555_SOURCES
    svlive555.cpp
    nalu.cpp
    )

set(SVLIVE555_INCLUDE
    ${CMAKE_CURRENT_LIST_DIR}/../liveMedia/include
    ${CMAKE_CURRENT_LIST_DIR}/../groupsock/include
    ${CMAKE_CURRENT_LIST_DIR}/../BasicUsageEnvironment/include
    ${CMAKE_CURRENT_LIST_DIR}/../UsageEnvironment/include
    )

message("SVLIVE555_INCLUDE is ${SVLIVE555_INCLUDE}")

set(SVLIVE555_LIBS
    liveMedia
    groupsock
    BasicUsageEnvironment
    UsageEnvironment
    )



add_library(${PROJECT_NAME} SHARED "${SVLIVE555_SOURCES}")

target_include_directories(${PROJECT_NAME} PRIVATE ${SVLIVE555_INCLUDE} ${CMAKE_BINARY_DIR}/${PROJECT_NAME} ${OPENSSL_INCLUDE_DIR})
target_link_directories(${PROJECT_NAME} PRIVATE ${CMAKE_INSTALL_PREFIX}/lib ${OPENSSL_LIB_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE ${SVLIVE555_LIBS} ${OPENSSL_LIBS} ${RT_LIBS})
target_compile_definitions(${PROJECT_NAME} PRIVATE svlive555_EXPORTS=1 )
target_compile_definitions(${PROJECT_NAME} PRIVATE __STDC_CONSTANT_MACROS )


generate_export_header(${PROJECT_NAME} EXPORT_FILE_NAME ${CMAKE_BINARY_DIR}/${PROJECT_NAME}/${PROJECT_NAME}_export.h)
install(FILES ${CMAKE_BINARY_DIR}/${PROJECT_NAME}/${PROJECT_NAME}_export.h DESTINATION include/${PROJECT_NAME})

if ("${CMAKE_SYSTEM_NAME}" STREQUAL "iOS" )
    set_target_properties(${PROJECT_NAME} PROPERTIES MACOSX_FRAMEWORK_IDENTIFIER com.live555.${PROJECT_NAME})
endif()

install(DIRECTORY ./ DESTINATION include/${PROJECT_NAME}/ FILES_MATCHING PATTERN "svlive555*.h")

install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin
        BUNDLE DESTINATION bin
        LIBRARY DESTINATION lib
        FRAMEWORK DESTINATION lib
        BUNDLE DESTINATION bin
        ARCHIVE DESTINATION lib)
