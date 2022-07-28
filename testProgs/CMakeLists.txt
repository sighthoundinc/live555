project(testProgs)

file(GLOB HEADERS *.hh)
file(GLOB SOURCES *.cpp)

include_directories(./ include ../UsageEnvironment/include ../groupsock/include ../liveMedia/include ../BasicUsageEnvironment/include)

if (WIN32 OR MINGW)
    if (MSVC)
        set(LIB_OPTION_PRE "")
        set(LIB_OPTION_POST "")
    else()
        set(LIB_OPTION_PRE -Wl,--whole-archive)
        set(LIB_OPTION_POST -Wl,--no-whole-archive)
    endif()
else()
    set(LIB_OPTION_PRE "")
    set(LIB_OPTION_POST "")
endif()

function (add_exes NAMES EXTRA_LIBS )
    foreach(__NAME ${NAMES})
        set(SRC "")
        list(APPEND SRC "${__NAME}.cpp")
        list(APPEND SRC "announceURL.cpp")
        if( ${__NAME} STREQUAL "openRTSP" OR ${__NAME} STREQUAL "playSIP" )
            list(APPEND SRC "playCommon.cpp" )
        endif()
        add_executable( "${__NAME}" "${SRC}" )
        target_include_directories(${__NAME} PRIVATE ${OPENSSL_INCLUDE_DIR})
        target_link_directories(${__NAME} PRIVATE ${OPENSSL_LIB_DIR})
        target_link_libraries(${__NAME} ${EXTRA_LIBS} ${OPENSSL_LIBS} ${RT_LIBS})
    endforeach(__NAME)
endfunction(add_exes)

set(L555_LIBS
    ${LIB_OPTION_PRE}
    liveMedia
    groupsock
    BasicUsageEnvironment
    UsageEnvironment
    ${LIB_OPTION_POST}
    )


set(L555_TESTS
    testMP3Streamer
    testMP3Receiver
    testRelay
    testReplicator
    testMPEG1or2Splitter
    testMPEG1or2VideoStreamer
    testMPEG1or2VideoReceiver
    testMPEG2TransportReceiver
    testMPEG1or2AudioVideoStreamer
    testMPEG2TransportStreamer
    testMPEG4VideoStreamer
    testH264VideoStreamer
    testH265VideoStreamer
    testDVVideoStreamer
    testWAVAudioStreamer
    testAMRAudioStreamer
    testOnDemandRTSPServer
    testMKVStreamer
    testOggStreamer
    vobStreamer
    testRTSPClient
    openRTSP
    playSIP
    sapWatch
    testMPEG1or2ProgramToTransportStream
    testH264VideoToTransportStream
    testH265VideoToTransportStream
    MPEG2TransportStreamIndexer
    testMPEG2TransportStreamTrickPlay
    registerRTSPStream
    )

add_exes("${L555_TESTS}" "${L555_LIBS}")


install(TARGETS ${L555_TESTS}
        RUNTIME DESTINATION bin
        BUNDLE DESTINATION bin
        LIBRARY DESTINATION lib
        FRAMEWORK DESTINATION lib
        BUNDLE DESTINATION bin
        ARCHIVE DESTINATION lib)
