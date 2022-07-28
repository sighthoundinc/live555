
get_filename_component(SELF_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)

set(prefix "${SELF_DIR}/../../..") 
set(exec_prefix "${prefix}")
set(libdir "${exec_prefix}/lib")
set(incdir "${prefix}/include")
set(LIVE555_PREFIX "${prefix}")
set(LIVE555_EXEC_PREFIX "${prefix}")
set(LIVE555_LIBDIR "${exec_prefix}/lib")
set(LIVE555_INCLUDE_DIRS "${incdir}/BasicUsageEnvironment" "${incdir}/groupsock" "${incdir}/liveMedia" "${incdir}/UsageEnvironment")
set(LIVE555_LIBRARIES "${libdir}/${CMAKE_SHARED_LIBRARY_PREFIX}liveMedia${CMAKE_SHARED_LIBRARY_SUFFIX}")
