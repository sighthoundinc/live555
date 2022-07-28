/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// "stream_live555_demux"
// Copyright (c) 2015 Sighthound Inc.  All rights reserved.
//
// Live555-based demux stream



#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <windows.h>
    #include <malloc.h>
#else
    #define _stricmp strcasecmp
    #define _strnicmp strncasecmp
    #define _stristr strcasestr

    #include <pthread.h>
#endif

#include <sstream>
#include <iomanip>


using std::string;

#include "svlive555.h"
#include "nalu.h"


#define RTSP_CLIENT_VERBOSITY_LEVEL     1 // by default, print verbose output from each "RTSPClient"
#define THREAD_ID sv_get_thread_id() << ": "

#define TRACE_CPP_L(l,y) if (_gTraceLevel>l) { clientCb->logTrace(y); }
#define TRACE_CPP(y) TRACE_CPP_L(0,y)

static int _g_StreamID = 0;
static int _gTraceLevel = 0;

#define _FMT1( prefix, msg )   ( ((std::ostringstream&)(std::ostringstream() << \
                        std::skipws << \
                        prefix << ": " << \
                        msg << \
                        '\0')).str().c_str() )
#define _FMT( msg ) _FMT1("live555", msg)
#define _STR( msg ) _FMT1("", msg)
#define INVALID_PTS (uint64_t)-1

#define M_STR(subsession) (const char*)subsession->mediumName() << "/" << (const char*)subsession->codecName()
#define S_ID_STR(buf)        "[URL:\"" << clientCb->sanitizeUrl((const char*)url(), buf, sizeof(buf)) << "\"]"

#define logCb(method, msg) clientCb->method(msg)

namespace sio {

namespace live555 {

//-----------------------------------------------------------------------------
static uint64_t sv_time_get_time_diff(uint64_t from, uint64_t to)
{
    if (from > to) return 0;
    return to - from;
}

//-----------------------------------------------------------------------------
static uint64_t sv_time_timeval_to_ms(struct timeval* t)
{
    return ((uint64_t)t->tv_sec)*1000 + t->tv_usec/1000;
}

//-----------------------------------------------------------------------------
static uint64_t sv_time_get_current_epoch_time()
{
    uint64_t        result;
    struct timeval  nowTime;
#ifdef _WIN32
    static const uint64_t kEpoch = ((uint64_t) 116444736000000000ULL);

    SYSTEMTIME  sysTime;
    FILETIME    fileTime;
    uint64_t    epochTime;

    GetSystemTime( &sysTime );
    SystemTimeToFileTime( &sysTime, &fileTime );
    epochTime =  ((uint64_t)fileTime.dwLowDateTime )      ;
    epochTime += ((uint64_t)fileTime.dwHighDateTime) << 32;

    nowTime.tv_sec  = (long) ((epochTime - kEpoch) / 10000000L);
    nowTime.tv_usec = (long) (sysTime.wMilliseconds * 1000);
#else
    gettimeofday(&nowTime,NULL);
#endif
    result = sv_time_timeval_to_ms(&nowTime);
    return result;
}

//-----------------------------------------------------------------------------
static uint64_t sv_time_get_elapsed_time(uint64_t from)
{
    return sv_time_get_time_diff(from, sv_time_get_current_epoch_time() );
}

//-----------------------------------------------------------------------------
static uint64_t sv_get_thread_id()
{
#ifdef _WIN32
	return (uint64_t)GetCurrentThreadId();
#else
	return (uint64_t)pthread_self();
#endif
}

//-----------------------------------------------------------------------------
void StatsSnapshot::reset()
{
    time = sv_time_get_current_epoch_time();
    framesProcessed = 0;
    framesLost = 0;
    frameSize.reset();
    interFrameTime.reset();
    frameAcquisitionTime.reset();
    ptsDivergence.reset();
}

void StatsSnapshot::combine(const StatsSnapshot& other)
{
    framesProcessed += other.framesProcessed;
    framesLost += other.framesLost;
    frameSize.combine(other.frameSize);
    interFrameTime.combine(other.interFrameTime);
    frameAcquisitionTime.combine(other.frameAcquisitionTime);
    ptsDivergence.combine(other.ptsDivergence);
}

//-----------------------------------------------------------------------------
class DemuxRTSPClientImpl;


//-----------------------------------------------------------------------------
L555DemuxSink::L555DemuxSink(UsageEnvironment& e)
    : MediaSink(e)
{
}

Boolean L555DemuxSink::continuePlaying()
{
    return (fSource == NULL)?False:True;
};

//-----------------------------------------------------------------------------
MyDemuxBasicUsageEnvironment* MyDemuxBasicUsageEnvironment::createNew(ILive555APIBridge* clientCbParam)
{
    BasicTaskScheduler* taskScheduler = BasicTaskScheduler::createNew();
    return new MyDemuxBasicUsageEnvironment(taskScheduler, clientCbParam);
}

void MyDemuxBasicUsageEnvironment::destroy(MyDemuxBasicUsageEnvironment* envir)
{
    // TaskScheduler needs to be destroyed after environment, or crash may occur
    TaskScheduler* ts = envir->taskScheduler;
    envir->reclaim();
    delete ts;
}

void MyDemuxBasicUsageEnvironment::setTraceLevel(int l)
{
    _gTraceLevel = l;
}

UsageEnvironment& MyDemuxBasicUsageEnvironment::operator<<(char const* str)
{
    TRACE_CPP_L(4, _FMT(THREAD_ID << "Live555:" << str));
    return *this;
}

UsageEnvironment& MyDemuxBasicUsageEnvironment::operator<<(int i)
{
    TRACE_CPP_L(4, _FMT(THREAD_ID << "Live555:" << i));
    return *this;
}

UsageEnvironment& MyDemuxBasicUsageEnvironment::operator<<(unsigned u)
{
    TRACE_CPP_L(4, _FMT(THREAD_ID << "Live555:" << u));
    return *this;
}

UsageEnvironment& MyDemuxBasicUsageEnvironment::operator<<(double d)
{
    TRACE_CPP_L(4, _FMT(THREAD_ID << "Live555:" << d));
    return *this;
}

UsageEnvironment& MyDemuxBasicUsageEnvironment::operator<<(void* p)
{
    TRACE_CPP_L(4, _FMT(THREAD_ID << "Live555:" << p));
    return *this;
}

MyDemuxBasicUsageEnvironment::MyDemuxBasicUsageEnvironment(TaskScheduler* _taskScheduler,
                        ILive555APIBridge* clientCbParam)
    : BasicUsageEnvironment(*_taskScheduler)
    , taskScheduler( _taskScheduler )
    , clientCb( clientCbParam )
{
}

//-----------------------------------------------------------------------------
static uint8_t kAnnexBHeader[] = { 0,0,0,1 };
static const size_t kAnnexBHeaderSize = sizeof(kAnnexBHeader);
static const int kDefaultBufferSizeKb = 2*1024;
static const int kDefaultSocketBufferSizeKb = 3*kDefaultBufferSizeKb;
static const int kBytesInKb = 1024;
static const int kDefaultReorderTresholdTimeUs = 200000; // usec ... Live555 default is 0.1s, but VLC sets it to 0.2s
static const int kReallocThresholdFactor = 10; // grow the buffer when we are within 1/10th of the limit
static const int kReallocGrowFactor = 5; // grow by 1/5th of the current size
static const int kMaxTimestampDivergence = 3000;
static const int kDefaultAudioFrameBufferSize = 2048;
static const int kFauxTimeout = 1;
static const int kBitsInByte = 8;
static const int kSecInMsec = 1000;
static const int kDefaultPacketTimeout = 5000;
static const int kDefaultInitTimeout = 60000;
static const int kDefaultSessionTimeout = 60000;
static const int FF_INPUT_BUFFER_PADDING_SIZE = 64; // ffmpeg uses 32 .. we don't want to use their headers here, yet would like to produce output ready to be input to ffmpeg
static uint8_t kZeroBlock[FF_INPUT_BUFFER_PADDING_SIZE] = {0};





//-----------------------------------------------------------------------------
Profiler::Profiler()
{
    Start();
}

void Profiler::Start()
{
    startTime = sv_time_get_current_epoch_time();
}

long Profiler::ElapsedMs()
{
    return sv_time_get_elapsed_time(startTime);
}

//=============================================================================
//  SubstreamState
//=============================================================================
SubstreamState::SubstreamState( SubstreamStateCb* cb, int mediaTypeParam, size_t defFrameSize, ILive555APIBridge* clientCbParam )
    : substreamCb ( cb )
    , mediaType( mediaTypeParam )
    , name ( mediaType == MediaType::video ? "video" : "audio" )
    , activeRead( false )
    , clientCb ( clientCbParam )
    , subsession( NULL )
    , prevDataPts ( INVALID_PTS )
    , prevFramePts( INVALID_PTS )
    , dataPts( 0 )
    , frameSize( defFrameSize )
    , socketBufferSize ( kDefaultSocketBufferSizeKb*kBytesInKb )
    , startPts ( 0 )
    , startTime ( 0 )
    , prevFrameSize ( 0 )
    , prevCaptureTime( sv_time_get_current_epoch_time() )
    , rtcpSync ( false )
    , rtcpSyncTime ( INVALID_PTS )
    , useLocalTimestamps( false )
    , localTimestampAdjustment( 0 )
    , streamHasTimingInfo ( false )
    , codecId ( Codec::unknown )
    , currentFrame ( NULL )
    , profile ( 0 )
    , level ( 0 )
    , tsCreator( clientCb->createTimestampCreator( name ) )
    , readStartTime( INVALID_PTS )
{
    lifetimeStats.reset();
    intervalStats.reset();
}


//-----------------------------------------------------------------------------
SubstreamState::~SubstreamState()
{
    TRACE_CPP(_FMT("SubstreamState::~SubstreamState - " << name << " - in"));
    if (currentFrame) {
        currentFrame->Release();
    }
    TRACE_CPP(_FMT("SubstreamState::~SubstreamState - " << name << " - out"));
}

//-----------------------------------------------------------------------------
void SubstreamState::CheckRTCPSync()
{
    if ( rtcpSyncTime == INVALID_PTS ) {
        if (subsession->rtpSource() &&
            subsession->rtpSource()->hasBeenSynchronizedUsingRTCP()==True) {
            TRACE_CPP(_FMT("RTCP sync occurred at time " << dataPts ));
            rtcpSyncTime = dataPts;
        }
    }
}

//-----------------------------------------------------------------------------
void SubstreamState::SwitchToLocalTimestamps()
{
    logCb(logInfo, _FMT("Switching to local timestamps. Last timestamp produced is " << prevFramePts));
    useLocalTimestamps = true;
    tsCreator->setTsBase(prevFramePts);
}

//-----------------------------------------------------------------------------
int SubstreamState::CalculateLocalPts(int64_t timeToReceive)
{
    int64_t             frameNum = GetFramesProcessed();
    int                 framesLost = GetFramesLost();
    bool                rtcpSyncTransition = false;
    if (rtcpSyncTime != INVALID_PTS &&
        dataPts == rtcpSyncTime &&
        subsession->rtpSource() ) {
        rtcpSyncTransition = true;
    }

    int64_t ts = currentFrame->Pts();
    if ( tsCreator->assignTs(dataPts, ts, rtcpSyncTransition) < 0 ) {
        // error .. enough of an error for us to decide to disconnect
        logCb(logError, _FMT("Error assigning PTS"));
        return -1;
    }
    currentFrame->SetPts(ts);

    // got an output frame
    // TODO: revert this back to TRACE before merging into develop
    TRACE_CPP(_FMT("Received (l) " << name << " frame #=" << frameNum <<
                        " lostFrames=" << framesLost <<
                        " compressedSize=" << currentFrame->DataSize() <<
                        " ptsIn=" << dataPts <<
                        " ptsOut=" << currentFrame->Pts() <<
                        " ptsDiff=" << ((GetFramesProcessed()>1)?(currentFrame->Pts() - prevFramePts):0) <<
                        " timeToReceive=" << timeToReceive << "ms" <<
                        " timeDelta=" << sv_time_get_elapsed_time(prevCaptureTime) <<
                        " rtcpSync=" << (rtcpSync?"true":"false") <<
                        " freq=" << subsession->rtpTimestampFrequency() <<
                        " localTimestampAdjustment=" << localTimestampAdjustment <<
                        " tsCreator:" << tsCreator->stats() ));

    return 0;
}

//-----------------------------------------------------------------------------
int SubstreamState::CalculatePts(int64_t timeToReceive)
{
    bool                rtcpSyncTransition = false;
    int64_t             relativeTimestamp,
                        relativeAdjustedTimestamp,
                        timestampDivergence, streamDuration,
                        currentTime, nextExpectedTimestamp;
    int                 expectedMinimumDelta;
    static const int    kMinPtsIncrement = 10; // This would be the increment at 100fps
    static const int    kMaxPtsIncrement = 500; // okay, 2fps would be really low, gotta warn on that
    int64_t             frameNum = GetFramesProcessed();
    int                 framesLost = GetFramesLost();

    currentTime = sv_time_get_current_epoch_time();
    // For streams that advertise FPS we can calculate the next timestamp
    // For all the other streams, we just want to make sure timestamps increment
    expectedMinimumDelta =  (subsession->videoFPS()?1000/subsession->videoFPS():kMinPtsIncrement);
    nextExpectedTimestamp = prevFramePts + expectedMinimumDelta;


    if (rtcpSyncTime != INVALID_PTS &&
        dataPts == rtcpSyncTime &&
        subsession->rtpSource() &&
        !useLocalTimestamps ) {
        rtcpSyncTransition = true;
    }

    if ( frameNum == 1 ) {
        startPts = dataPts;
        startTime = currentTime;
    } else if ( rtcpSyncTransition ) {
        localTimestampAdjustment = dataPts - nextExpectedTimestamp;
        rtcpSync = true;
        logCb(logInfo, _FMT("RTCP sync occurred at frame " << frameNum << " : "
                    " nextExpectedTimestamp=" << nextExpectedTimestamp <<
                    " actualTimestamp=" << dataPts <<
                    " localTimestampAdjustment=" << localTimestampAdjustment ));
    } else if ( dataPts < prevDataPts ) {
        // if timestamp ever retreats, switch to wall clock immediately
        logCb(logWarning, _FMT("Received timestamp " << dataPts << " after " << prevDataPts << " diff=-" << prevDataPts-dataPts << " frameNum=" << frameNum ));
        return -1;
    } else if ( prevDataPts + kMaxPtsIncrement < dataPts ) {
        logCb(logWarning, _FMT("Received timestamp " << dataPts << " after " << prevDataPts << " diff=" << dataPts-prevDataPts << " frameNum=" << frameNum ));
    }


    streamDuration = sv_time_get_elapsed_time(startTime);
    relativeTimestamp = dataPts - startPts;
    relativeAdjustedTimestamp = relativeTimestamp - localTimestampAdjustment;
    timestampDivergence = relativeTimestamp - streamDuration - localTimestampAdjustment;

    int naluContained = currentFrame->GetContainedNALU();

    if (std::abs(timestampDivergence) > kMaxTimestampDivergence &&
        !rtcpSyncTransition ) {
        // Do not make a decision to switch to a different timestamp flow, if the frame doesn't contain video data
        // The theory is that RTCP sync may occur between SPS/PPS chunks and the video data, causing an unnecessary reset
        bool doReset = false;
        if ( mediaType == MediaType::video ) {
            doReset = currentFrame->ContainsTimedData();
            if (!doReset) {
                logCb(logWarning, _FMT("Postponing timestamp reset on " << (mediaType == MediaType::video?"video":"audio") <<
                                    " channel: dataPts=" << dataPts <<
                                    " prevDataPts=" << prevDataPts <<
                                    " dataSize=" << currentFrame->DataSize() <<
                                    " frameNum=" << frameNum <<
                                    " NAL=" << naluContained <<
                                    " RTCPSyncTime=" << rtcpSyncTime ));
            }
        }
        if ( doReset ) {
            // if the timestamps had drifted away from realtime, switch to wall clock as well
            logCb(logError, _FMT("Source timestamps had diverged from the realtime. " <<
                                    " maxDivergence=" << kMaxTimestampDivergence <<
                                    " timestampDivergence=" << timestampDivergence <<
                                    " dataPts=" << dataPts <<
                                    " prevDataPts=" << prevDataPts <<
                                    " startPts=" << startPts <<
                                    " relativeTimestamp=" << relativeTimestamp <<
                                    " relativeAdjustedTimestamp=" << relativeAdjustedTimestamp <<
                                    " streamDuration=" << streamDuration <<
                                    " localTimestampAdjustment=" << localTimestampAdjustment ));
            localTimestampAdjustment = currentTime - nextExpectedTimestamp;
            return -1;
        }
    }

    intervalStats.ptsDivergence.update(timestampDivergence);
    currentFrame->SetPts(startTime + relativeAdjustedTimestamp);

    // got an output frame
    TRACE_CPP( _FMT("Received " << name << " frame #=" << frameNum <<
                        " lostFrames=" << framesLost <<
                        " compressedSize=" << currentFrame->DataSize() <<
                        " ptsIn=" << dataPts <<
                        " nalu=" << naluContained <<
                        " relativePts=" << relativeTimestamp <<
                        " adjustedPts=" << currentFrame->Pts() <<
                        " relativeAdjPts=" << relativeAdjustedTimestamp <<
                        " elapsedSinceStart=" << streamDuration <<
                        " ptsDivergence=" << timestampDivergence <<
                        " timeToReceive=" << timeToReceive << "ms" <<
                        " ptsDelta=" << ((GetFramesProcessed()>1)?(currentFrame->Pts() - prevFramePts):0) <<
                        " timeDelta=" << sv_time_get_elapsed_time(prevCaptureTime) <<
                        " rtcpSync=" << (rtcpSync?"true":"false") <<
                        " frequency=" << subsession->rtpTimestampFrequency() <<
                        " useLocalTimestamps=" << (useLocalTimestamps?"true":"false") <<
                        " localTimestampAdjustment=" << localTimestampAdjustment ));



    return 0;
}

//-----------------------------------------------------------------------------
void SubstreamState::FormatStats(StatsSnapshot& ls, bool lifetime, std::ostringstream& str)
{
    RTPSource* rtpSrc = subsession ? subsession->rtpSource() : NULL;
    int64_t elapsed = sv_time_get_elapsed_time( ls.time );
    str << name << "-" << (lifetime ? "lifetime" : "interval" ) << ":" <<
            " uptime=" << elapsed <<
            " framesSeen=" << ls.framesProcessed <<
            " framesLost=" << ls.framesLost <<
            " maxFrameSize=" << ls.frameSize.max <<
            " avgFrameSize=" << ls.frameSize.average() <<
            " avgBitrate=" << (elapsed?ls.frameSize.cumulative*kBitsInByte*kSecInMsec/elapsed:0) <<
            " maxInterFrame=" << ls.interFrameTime.max <<
            " avgInterFrame=" << ls.interFrameTime.average() <<
            " minPtsDiv=" << ls.ptsDivergence.min <<
            " maxPtsDiv=" << ls.ptsDivergence.max <<
            " avgPtsDiv=" << ls.ptsDivergence.average() <<
            " maxReadMs=" << ls.frameAcquisitionTime.max <<
            " avgReadMs=" << ls.frameAcquisitionTime.average() <<
            "; ";

    if (useLocalTimestamps) {
        str << " tsCreator: " << tsCreator->stats() << "; ";
    }

    if (lifetime && rtpSrc) {
        RTPReceptionStatsDB& db = rtpSrc->receptionStatsDB();
        RTPReceptionStatsDB::Iterator iter(db);
        RTPReceptionStats* stats = iter.next();
        if (stats) {
            unsigned packetsExpected = stats->totNumPacketsExpected();
            unsigned packetsReceived = stats->totNumPacketsReceived();
            unsigned packetsLost = packetsExpected > packetsReceived ? packetsExpected-packetsReceived : 0;
            str << " numPacketsReceivedSinceLastReset=" << stats->numPacketsReceivedSinceLastReset() <<
            " totNumPacketsReceived=" << packetsReceived <<
            " totNumKBytesReceived=" << stats->totNumKBytesReceived() <<
            " totNumPacketsExpected=" << packetsExpected <<
//                    " baseExtSeqNumReceived=" << stats->baseExtSeqNumReceived() <<
//                    " lastResetExtSeqNumReceived=" << stats->lastResetExtSeqNumReceived() <<
//                    " highestExtSeqNumReceived=" << stats->highestExtSeqNumReceived() <<
            " jitter=" << stats->jitter() <<
            " packetsDropped=" << packetsLost <<
            " framesDropped=" << rtpSrc->getDroppedFramesCount() <<
            " maxInterPacketGapMs=" <<  stats->maxInterPacketGapUS()/1000 <<
            "; ";
        }
    }
}

//-----------------------------------------------------------------------------
int64_t SubstreamState::GetUptime       () const
{
    return sv_time_get_elapsed_time(startTime);
}

//-----------------------------------------------------------------------------
int64_t SubstreamState::TimeSinceRead   ( ) const
{
    return readStartTime == INVALID_PTS ? -1 : sv_time_get_current_epoch_time()-readStartTime;
}

//-----------------------------------------------------------------------------
bool SubstreamState::Reading            ( ) const
{
    return readStartTime != INVALID_PTS;
}

//-----------------------------------------------------------------------------
void SubstreamState::ProcessStats(std::ostringstream& str)
{
    lifetimeStats.combine(intervalStats);
    FormatStats(intervalStats, false, str);
    FormatStats(lifetimeStats, true, str);
    intervalStats.reset();
}

//-----------------------------------------------------------------------------
int SubstreamState::GetFramesProcessed() const
{
    return lifetimeStats.framesProcessed + intervalStats.framesProcessed;
}

//-----------------------------------------------------------------------------
int SubstreamState::GetFramesLost() const
{
    return lifetimeStats.framesLost + intervalStats.framesLost;
}

//-----------------------------------------------------------------------------
int SubstreamState::GetBitrate() const
{
    // run for 10s before we can say what the bitrate is
    if (!subsession) {
        return 0;
    }

    static const int kBitrateDeterminationTime = 10000;
    int64_t elapsed = sv_time_get_elapsed_time(startTime);
    if (elapsed < kBitrateDeterminationTime) {
        return 0;
    }
    return kSecInMsec*kBitsInByte*(lifetimeStats.frameSize.cumulative + intervalStats.frameSize.cumulative)/elapsed;
}

//-----------------------------------------------------------------------------
size_t SubstreamState::StorageAvailable   ( )
{
    return currentFrame->FreeSpace();
}

//-----------------------------------------------------------------------------
bool SubstreamState::ValidateStorage    ( size_t additionalSizeRequired )
{
    int prevSize = currentFrame->AllocSize();
    if ( !currentFrame->EnsureFreeSpace(additionalSizeRequired) ) {
        logCb(logError, _FMT("Failed to grow the frame: prevSize=" << prevSize << " dataSize=" << additionalSizeRequired));
        return false;
    }
    return true;
}

//-----------------------------------------------------------------------------
bool SubstreamState::AppendData(uint8_t* data, size_t size, bool updateSize)
{
    int prevSize = currentFrame->AllocSize();
    if  ( !currentFrame->Append(data, size, updateSize) ) {
        logCb(logError, _FMT("Failed to allocate additional " << size <<
                            " bytes for the frame. Currently allocated " <<
                            prevSize));
        return false;
    }
    return true;
}

//-----------------------------------------------------------------------------
bool SubstreamState::PrepareRead        ( )
{
    if ( !IsActive() ) {
        return false;
    }

    if ( currentFrame == NULL ) {
        currentFrame = clientCb->createFrameBuffer( frameSize, mediaType == MediaType::video );
        if ( currentFrame == NULL ) {
            logCb(logError, _FMT("Failed to allocate new frame of " << frameSize << " bytes"));
            return false;
        }
        currentFrame->SetMediaType ( mediaType );
        if ( codecId == Codec::h264 ) {
            if ( !AppendData( kAnnexBHeader, kAnnexBHeaderSize) ) {
                return false;
            }
        }
        //TRACE_CPP_L(10, _FMT("Allocated new " << name << " frame: size=" << currentFrame->AllocSize() << " data=" << currentFrame->DataSize() ));
    }

    if ( readStartTime == INVALID_PTS ) {
        unsigned char* ptr = currentFrame->WritePtr();
        unsigned int size = currentFrame->AllocSize() - currentFrame->DataSize();
        readStartTime = sv_time_get_current_epoch_time();
        subsession->readSource()->getNextFrame(ptr, size, OnData, this, OnClose, this);
    }

    return true;
}

//-----------------------------------------------------------------------------
void SubstreamState::OnData                     ( unsigned int size,
                                        unsigned int truncated,
                                        struct timeval pts,
                                        unsigned int duration )
{
    if ( streamHasTimingInfo ) {
        dataPts = sv_time_timeval_to_ms(&pts);
    } else {
        dataPts = sv_time_get_current_epoch_time();
    }

    CheckRTCPSync();

    TRACE_CPP_L( 3, _FMT(THREAD_ID << "OnData/" << name <<
                            " offset=" << currentFrame->DataSize() <<
                            " size=" << size <<
                            " truncated=" << truncated <<
                            " pts=" << dataPts ) );

    if ( truncated > 0 ) {
        // frames we're allocating are not big enough for this stream; let this one go, and allocate more next time
        static const int kGrowthFactor = 2;
        int newFrameSize = frameSize + kGrowthFactor*truncated;
        logCb(logInfo, _FMT("Data truncated. Growing frame size from " << frameSize << " to " << newFrameSize ));
        frameSize = newFrameSize;
        // dispose of the current frame; reallocation will need to happen
        currentFrame->Release();
        currentFrame = NULL;
    } else {
        // count the data
        currentFrame->OnDataWritten( size );
        // finalize the frame
        FinalizeFrame();
    }

    int64_t timeSinceRead = TimeSinceRead();
    static const int kMaxReadTime = 333; // 3fps? That'd be too long
    if ( timeSinceRead > kMaxReadTime ) {
        TRACE_CPP(_FMT("getNextFrame took " << timeSinceRead << "ms"));
    }

    // getNextFrame will have to be called again
    readStartTime = INVALID_PTS;
}

//-----------------------------------------------------------------------------
void SubstreamState::FinalizeFrame       ( )
{
    int result = 0;
    assert (currentFrame != NULL);

    RTPSource* rtpSrc = subsession ? subsession->rtpSource() : NULL;
    int64_t timeElapsed = sv_time_get_elapsed_time(readStartTime);

    if ( mediaType == MediaType::video && codecId == Codec::h264 ) {
        currentFrame->ParseNALU();
    }

   if ( dataPts == prevDataPts && streamHasTimingInfo ) {
        // another packet for the previous frame
        // pts from the sender matches ... it's a continuation of the frame
        TRACE_CPP( _FMT("Received " << name << " frame #=" << GetFramesProcessed() <<
                        " (continuation) " <<
                        " nalu=" << currentFrame->GetContainedNALU() <<
                        " compressedSize=" << currentFrame->DataSize() <<
                        " ptsIn=" << dataPts ) );
        prevFrameSize += currentFrame->DataSize();
        currentFrame->SetPts( prevFramePts );
    } else {
        // new frame, update statistics
        intervalStats.framesProcessed++;
        intervalStats.framesLost = -1;
        if (rtpSrc) {
            // rtp source gives us the livetime -- we need to get the interval stat
            intervalStats.framesLost = rtpSrc->getDroppedFramesCount() -
                                        lifetimeStats.framesLost;
        }
        // since size may be accumulated across multiple frames, update for previous frame
        if ( prevFrameSize != 0 ) {
            intervalStats.frameSize.update(prevFrameSize);
        }
        intervalStats.interFrameTime.update(sv_time_get_elapsed_time(prevCaptureTime));
        intervalStats.frameAcquisitionTime.update(timeElapsed);

        if ( !useLocalTimestamps ) {
            if ( CalculatePts(timeElapsed) < 0 ) {
                // when we can't assign timestamp using RTP information, we're switching to local wallclock;
                // from this point on, wall clock will be used for all channels
                substreamCb->OnLocalTimestampSwitch();
                result = CalculateLocalPts(timeElapsed);
            }
        } else {
            result = CalculateLocalPts(timeElapsed);
        }

        if ( result < 0 ) {
            // we've detected a timing error ... signal it up, and exit
            currentFrame->Release();
            currentFrame = NULL;
            substreamCb->OnSubstreamError();
            return;
        }

        prevDataPts = dataPts;
        prevFramePts = currentFrame->Pts();
        prevCaptureTime = sv_time_get_current_epoch_time();
        prevFrameSize = currentFrame->DataSize();
    }

    // make sure any buffer returned from us is zero-padded, for the benefit of ffmpeg
    AppendData(kZeroBlock, FF_INPUT_BUFFER_PADDING_SIZE, false);

    // we're returning the current frame, so do not free it
    FrameBuffer* frameToDeposit = currentFrame;
    currentFrame = NULL;

    // queue the frame
    // TRACE_CPP_L( 10, _FMT("Depositing new " << name << " frame: size=" << frameToDeposit->dataSize ));
    bool merged = false;
    substreamCb->OnSubstreamFrameReady(frameToDeposit, merged);
}

//-----------------------------------------------------------------------------
void SubstreamState::OnClose                    ( )
{
    substreamCb->OnSubstreamClosed();
}

//=============================================================================
//  DemuxRTSPClientImpl
//=============================================================================
DemuxRTSPClientImpl::DemuxRTSPClientImpl(  BasicUsageEnvironment* myEnvir,
            ILive555APIBridge*      clientCbParam,
            char const*             rtspURL,
            int                     verbosityLevel,
            char const*             applicationName,
            int                     forceTCPParam,
            portNumBits             tunnelOverHTTPPortNum,
            bool                    removeUsernamePasswordParam)
    : RTSPClient( *myEnvir,
                    rtspURL,
                    verbosityLevel,
                    applicationName,
                    tunnelOverHTTPPortNum,
                    -1 )
    , iter( NULL )
    , session( NULL )
    , setupInProgressSubsession( NULL )
    , sessionTimeout ( 60000 )
    , serverSupportsGetParameter ( false )
    , enableGetParameter( true )
    , lastSessionRefreshTime ( sv_time_get_current_epoch_time() )
    , duration( 0.0 )
    , forceTCP( forceTCPParam?True:False )
    , initTimeout( 0 )
    , packetTimeout ( 0 )
    , clientCb( clientCbParam )
    , eventLoopWatchVariable ( 0 )
    , state ( Idle )
    , taskRunning( 0 )
    , rtspURLCopy( rtspURL )
    , pendingOperations ( 0 )
    , timeoutTask( 0 )
    , timeoutOccurred( 0 )
    , streamClosed( 0 )
    , statsIntervalMSec ( 0 )
    , statsLastReportTime( sv_time_get_current_epoch_time() )
    , sps ( NULL )
    , spsSize ( 0 )
    , width ( -1 )
    , height ( -1 )
    , pps ( NULL )
    , ppsSize ( 0 )
    , audio ( this, kDefaultAudioFrameBufferSize, clientCbParam )
    , video ( this, kDefaultBufferSizeKb*kBytesInKb, clientCbParam )
    , aggregateNALU ( true )
    , authenticator ( NULL )
    , removeUserNamePassword( removeUsernamePasswordParam )
{
}

//-------------------------------------------------------------------------
DemuxRTSPClientImpl::~DemuxRTSPClientImpl()
{
    TRACE_CPP(_FMT("DemuxRTSPClientImpl destructor - begin"));

    if ( state != Closing ) {
        // calling Close() repeatedly may cause a crash, since
        // Medium::close(this); deletes the object
        //
        // Out expectation is to never see this, since the only way this destructor
        // is invoked is with the stack where
        //      Medium::close(this);
        //      DemuxRTSPClientImpl::Close()
        //
        // DemuxRTSPClientImpl::Close sets
        //      state = Closing;
        // so we should never end up here.
        //
        // If we do end up here, we need to understand what is going on, and kill this process
        // quietly, before it crashes anyway.
        logCb(logError, _FMT("Closing RTSP demux in state " << state));
        exit(-1);
    }

    FrameBuffer* ptr = GetFrame(true);
    while (ptr) {
        ptr->Release();
        ptr = GetFrame(true);
    }

    free(sps);
    free(pps);

    if ( authenticator ) {
        delete authenticator;
        authenticator = NULL;
    }

    TRACE_CPP(_FMT("DemuxRTSPClientImpl destructor - end"));
}


//-------------------------------------------------------------------------
int         DemuxRTSPClientImpl::GetAudioSampleRate() const
{
    switch (GetAudioCodecId()) {
    case Codec::pcma:
    case Codec::pcmu: return 8000;
    default        : break;
    }
    int res = 0;
    const char* config = audio.GetStrAttr("config");
    if ( config != NULL ) {
        res = samplingFrequencyFromAudioSpecificConfig(config);
    }
    return res;
}

//-------------------------------------------------------------------------
int        DemuxRTSPClientImpl::Init()
{
    TRACE_CPP(_FMT("DemuxRTSPClientImpl::Init - begin " << rtspURLCopy));
    if ( state != Idle ) {
        logCb(logError, _FMT("DemuxRTSPClientImpl::Init - end (invalid state)"));
        return -1;
    }

    if ( removeUserNamePassword ) {
        if ( RemoveAuthInfoFromURL() < 0 ) {
            return -1;
        }
    }

    state = Setup;
    sendDescribeCommand(OnDescribeResponse, authenticator);
    if (state == Setup) {
        // Run the task -- exit when connected, or when error is handled
        TRACE_CPP(_FMT( "DemuxRTSPClientImpl::Init - running task"));
        Run(initTimeout);
        TRACE_CPP(_FMT( "DemuxRTSPClientImpl::Init - done running task"));
    }
    TRACE_CPP(_FMT("DemuxRTSPClientImpl::Init - end " << rtspURLCopy));
    return (state==Streaming)?0:-1;
}

//-------------------------------------------------------------------------
int         DemuxRTSPClientImpl::Run(int timeout)
{
    // TRACE_CPP_L(2,_FMT(THREAD_ID << "DemuxRTSPClientImpl::Run - begin"));
    TaskScheduler& scheduler = envir().taskScheduler();

    if (state == Idle || state == Error ) {
        logCb(logError, _FMT("DemuxRTSPClientImpl::Run - end (invalid state)"));
        return -1;
    }
    eventLoopWatchVariable = 0;
    taskRunning = 1;
    timeoutValue = 0;
    if( timeout > 0 ) {
        /* Create a task that will be called if we wait more than timeout ms */
        timeoutValue = timeout;
        eventLoopStart = sv_time_get_current_epoch_time();
        timeoutTask = scheduler.scheduleDelayedTask( timeout*1000,
                                                    OnTimeout,
                                                    this );
    }
    scheduler.doEventLoop(&eventLoopWatchVariable);
    if( timeout > 0 ) {
        scheduler.unscheduleDelayedTask( timeoutTask );
        timeoutValue = 0;
    }
    taskRunning = 0;
    assert (eventLoopWatchVariable!=0);
    // TRACE_CPP_L(2,_FMT(THREAD_ID << "DemuxRTSPClientImpl::Run - end"));
    return 0;
}

//-------------------------------------------------------------------------
int        DemuxRTSPClientImpl::Close()
{
    ILive555APIBridge* clientCbSave = clientCb;
    TRACE_CPP(_FMT("DemuxRTSPClientImpl::Close - begin"));
    if ( state != Setup &&
            state != Streaming &&
            state != Error ) {
        logCb(logError, _FMT("DemuxRTSPClientImpl::Close - end (invalid state)"));
        return -1;
    }
    state = Closing;

    LogStats();

    if ( iter ) {
        delete iter;
        iter = NULL;
    }


    // First, check whether any subsessions have still to be closed:
    if (session != NULL) {
        Boolean someSubsessionsWereActive = False;
        MediaSubsessionIterator localIter(*session);
        MediaSubsession*        subsession;

        while ((subsession = localIter.next()) != NULL) {
            if (subsession->sink != NULL) {
                Medium::close(subsession->sink);
                subsession->sink = NULL;

                if (subsession->rtcpInstance() != NULL) {
                    // in case the server sends a RTCP "BYE"
                    // while handling "TEARDOWN"
                    subsession->rtcpInstance()->setByeHandler(NULL, NULL);
                }

                someSubsessionsWereActive = True;
            }
        }

        if (someSubsessionsWereActive) {
            // Send a RTSP "TEARDOWN" command,
            // to tell the server to shutdown the stream.
            // Don't bother handling the response to the "TEARDOWN".
            sendTeardownCommand(*session, NULL);
        }
        TRACE_CPP(_FMT("DemuxRTSPClientImpl::Close - Medium::close(session) in"));
        Medium::close(session);
        TRACE_CPP(_FMT("DemuxRTSPClientImpl::Close - Medium::close(session) in"));
        session = NULL;
    }

    // Do not access class members after this
    TRACE_CPP(_FMT("DemuxRTSPClientImpl::Close - Medium::close(this) in"));
    Medium::close(this);
    TRACE_CPP(_FMT("DemuxRTSPClientImpl::Close - Medium::close(this) out"));

    if (_gTraceLevel>0) { clientCbSave->logTrace( _FMT("DemuxRTSPClientImpl::Close - end")); };
    return 0;
}


//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::LogStats()
{
    std::ostringstream str;

    video.ProcessStats(str);

    if ( audio.IsActive() ) {
        audio.ProcessStats(str);
    }

    logCb(logInfo, _FMT("Stats: " << str.str().c_str() << " saved=" << savedFrames.size()));
    statsLastReportTime = sv_time_get_current_epoch_time();
}

//-------------------------------------------------------------------------
bool DemuxRTSPClientImpl::HaveTwoVideoFrames()
{
    bool seenOne = false;
    for ( FrameContainer::iterator it = savedFrames.begin(); it!=savedFrames.end(); it++) {
        if ( (*it)->GetMediaType() == MediaType::video ) {
            if (seenOne)
                return true;
            seenOne = true;
        }
    }
    return false;
}

//-------------------------------------------------------------------------
bool DemuxRTSPClientImpl::CanReturnFrame()
{
    // audio frame can always be returned right away
    // video frames can be returned without additional checks, if we do not
    // aggregate NALUs, or if we have 2 unmerged video frames in the queue
    return ( savedFrames.size() > 0 &&
        ( savedFrames.front()->GetMediaType() == MediaType::audio ||
        !aggregateNALU ||
        video.GetCodecId() != Codec::h264 ||
        HaveTwoVideoFrames()) );
}

//-------------------------------------------------------------------------
FrameBuffer* DemuxRTSPClientImpl::GetFrame( bool flush )
{
    FrameBuffer* ret = NULL;
    if ( CanReturnFrame() || (flush && savedFrames.size()) ) {
        ret = savedFrames.front();
        savedFrames.pop_front();
    }
    return ret;
}

//-------------------------------------------------------------------------
FrameBuffer* DemuxRTSPClientImpl::ReadFrame()
{
    FrameBuffer* res = NULL;
    int64_t startTime = sv_time_get_current_epoch_time();

    while (true) {
        // ---------------------------------------------------------------------
        // Validate state
        // ---------------------------------------------------------------------
        if (!video.IsActive()) {
            logCb(logError, _FMT("Failed to read - no video subsession"));
            goto Error;
        }

        if ( state != Streaming ) {
            logCb(logError, _FMT("Failed to read - invalid state " << state ));
            goto Error;
        }

        UpdateSession();

        // ---------------------------------------------------------------------
        // see if stats need to be reported
        // ---------------------------------------------------------------------
        if ( statsIntervalMSec &&
            statsIntervalMSec < sv_time_get_elapsed_time(statsLastReportTime) ) {
            LogStats();
        }

        // ---------------------------------------------------------------------
        // Call getNextFrame for as long as we don't get both channels locked
        // and loaded, or until we can return a frame.
        //
        // This needs to be done, because a frame may be read while in getNextFrame
        // for the other channel, and we then can call Run() without actually
        // waiting for the frames on the channel, thus causing a timeout.
        // ---------------------------------------------------------------------
        do {
            // ---------------------------------------------------------------------
            // make sure we're reading data
            // ---------------------------------------------------------------------
            if ( (audio.IsActive() && !audio.PrepareRead()) ||
                !video.PrepareRead() ) {
                logCb(logError, _FMT("Error preparing to read a frame!"));
                goto Error;
            }

            // ---------------------------------------------------------------------
            // see if a frame is already available
            // ---------------------------------------------------------------------
            res = GetFrame();
            if ( res != NULL ) {
                // pass returned frames through the size filter
                res->Shrink();
                return res;
            }
        } while ( (audio.IsActive() && !audio.Reading()) || !video.Reading() );

        // ---------------------------------------------------------------------
        // if not, run to timeout, or until we're signaled to return something
        // ---------------------------------------------------------------------
        int64_t elapsed = sv_time_get_elapsed_time(startTime);
        int64_t runStart = sv_time_get_current_epoch_time();
        if ( elapsed >= packetTimeout ) {
            timeoutOccurred = 1;
        } else {
            assert( video.Reading() && (!audio.IsActive() || audio.Reading()) );
            Run(packetTimeout - elapsed);
        }

        if (timeoutOccurred) {
            logCb(logError, _FMT("Read timeout has occurred - timeSinceVideoRead=" << video.TimeSinceRead() <<
                                " timeSinceAudioRead=" << audio.TimeSinceRead() <<
                                " timeSinceRun=" << sv_time_get_elapsed_time(runStart) <<
                                " inQueue=" << savedFrames.size() <<
                                " canReturn=" << CanReturnFrame()));
            LogStats();
            goto Error;
        }

        if (streamClosed) {
            logCb(logError, _FMT("Video stream had been closed while reading"));
            goto Error;
        }
    }


Error:
    state = Error;
    return NULL;
}


//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::SetBufferSize(int bufferSizeKb)
{
    if ( bufferSizeKb != 0 &&
            (state == Idle || bufferSizeKb*kBytesInKb>video.GetFrameSize() )) {
        video.SetFrameSize( bufferSizeKb*kBytesInKb );
    }
}

//-------------------------------------------------------------------------
int DemuxRTSPClientImpl::GetBufferSize() const
{
    return video.GetFrameSize()/kBytesInKb + ((video.GetFrameSize()%kBytesInKb!=0)?1:0);
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::SetSocketBufferSize(int bufferSizeKb)
{
    if ( bufferSizeKb != 0 &&
            (state == Idle || bufferSizeKb*kBytesInKb>video.GetSocketBufferSize() )) {
        video.SetSocketBufferSize ( bufferSizeKb*kBytesInKb );
        if ( state == Streaming ) {
            ApplySocketBufferSize();
        }
    }
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::ApplySocketBufferSize()
{
    RTPSource* rtpSrc = setupInProgressSubsession ? setupInProgressSubsession->rtpSource() : NULL;

    if ( rtpSrc &&
            setupInProgressSubsession->rtpSource()->RTPgs() != NULL ) {
            if ( video.GetSubsession() == setupInProgressSubsession ) {
            int fd = rtpSrc->RTPgs()->socketNum();
            increaseReceiveBufferTo( envir(), fd, video.GetSocketBufferSize() );
            logCb(logDebug, _FMT("Setting socket buffer to " << video.GetSocketBufferSize()));
        } else {
            TRACE_CPP(_FMT("Not modifying audio session socket buffer"));
        }
    } else {
        logCb(logWarning, _FMT("Couldn't set socket buffer to " << video.GetSocketBufferSize()));
    }
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::SetupNextSubsession()
{
    TRACE_CPP(_FMT("DemuxRTSPClientImpl::SetupNextSubsession - begin"));
    assert ( setupInProgressSubsession == NULL );
    MediaSubsession* subsession = iter->next();
    if (subsession != NULL) {
        bool bIsVideo = _stricmp((const char*)subsession->mediumName(), "video") == 0 &&
                        IsVideoCodecSuported(subsession, NULL) &&
                        video.GetSubsession() == NULL;
        bool bIsAudio = _stricmp((const char*)subsession->mediumName(), "audio") == 0 &&
                        IsAudioCodecSuported(subsession, NULL) &&
                        audio.GetSubsession() == NULL;

        if (!bIsVideo && !bIsAudio) {
            logCb(logDebug, _FMT("Skipping the \"" << M_STR(subsession) << "\" subsession"));
            // give up on this subsession; go to the next one
            SetupNextSubsession();
        } else if (!subsession->initiate()) {
            logCb(logError, _FMT("Failed to initiate the \"" <<
                                    M_STR(subsession) << "\" subsession: " <<
                                    envir().getResultMsg()));
            // give up on this subsession; go to the next one
            SetupNextSubsession();
        } else {
            logCb(logDebug, _FMT("Initiate the \"" <<
                                    M_STR(subsession) << "\" subsession (client port " <<
                                    subsession->clientPortNum() << ")"));
            NotifyOpStarted();

            if (bIsVideo) {
                video.SetSubsession( subsession );
            } else {
                audio.SetSubsession( subsession );
            }
            setupInProgressSubsession = subsession;

            ApplySocketBufferSize();

            // VLC does that ... they may know something we don't
            RTPSource* src = setupInProgressSubsession->rtpSource();
            src->setPacketReorderingThresholdTime(kDefaultReorderTresholdTimeUs);

            // Continue setting up this subsession,
            // by sending a RTSP "SETUP" command:
            sendSetupCommand(*setupInProgressSubsession,
                        OnSetupResponse,
                        False,
                        forceTCP);
        }
    } else if (session->absStartTime() != NULL) {
        NotifyOpStarted();
        // We've finished setting up all of the subsessions.
        // Now, send a RTSP "PLAY" command to start the streaming:
        // Special case: The stream is indexed by 'absolute' time,
        // so send an appropriate "PLAY" command:
        sendPlayCommand(*session,
                        OnPlayResponse,
                        session->absStartTime(),
                        session->absEndTime());
    } else {
        duration = session->playEndTime() - session->playStartTime();
        NotifyOpStarted();
        sendPlayCommand(*session, OnPlayResponse);
    }

    // run the task until the response to PLAY is received
    if (!taskRunning) {
        // we may be in the event loop already
        Run(initTimeout);
    }
    TRACE_CPP(_FMT("DemuxRTSPClientImpl::SetupNextSubsession - end"));
}

//-------------------------------------------------------------------------
int        DemuxRTSPClientImpl::RemoveAuthInfoFromURL()
{
    logCb(logTrace, _FMT("Removing username/password from RTSP URI"));

    char* username = NULL;
    char* password = NULL;
    NetAddress  addr;
    portNumBits port;
    char const* urlSuffix = NULL;
    if ( RTSPClient::parseRTSPURL(rtspURLCopy.c_str(), username, password, addr, port, &urlSuffix ) == False ) {
        logCb(logError, _FMT("Failed to parse RTSP URL: " << envir().getResultMsg()));
        return -1;
    }
    if ( username || password ) {
        authenticator = new Authenticator(username, password);
        string strToRemove;
        if (username) {
            strToRemove += username;
        }
        if ( password ) {
            strToRemove += ":";
            strToRemove += password;
        }
        strToRemove += "@";

        string::size_type pos = rtspURLCopy.find(strToRemove);

        if ( pos != string::npos ) {
            rtspURLCopy.erase(pos, strToRemove.length());
            setBaseURL(rtspURLCopy.c_str());
        } else {
            logCb(logTrace, _FMT("Not modifying RTSP URL: no username/password substring found"));
        }
    } else {
        logCb(logTrace, _FMT("URI doesn't contain username or password"));
    }

    if ( username ) delete [] username;
    if ( password ) delete [] password;
    return 0;
}


//-------------------------------------------------------------------------
int DemuxRTSPClientImpl::SetSpropsParams(MediaSubsession* subs)
{
    const char *sprop = subs->fmtp_spropparametersets();

    if (sprop != NULL) {
        unsigned int numSPropRecords = 0;
        SPropRecord* sPropRecords = parseSPropParameterSets(sprop,
                                                        numSPropRecords);

        for (unsigned i = 0; i < numSPropRecords; ++i) {
            if (sPropRecords[i].sPropLength == 0) continue; // bad data
            u_int8_t nal_unit_type = (sPropRecords[i].sPropBytes[0])&0x1F;
            if (nal_unit_type == 7/*SPS*/) {
                sps = (uint8_t*)malloc(sPropRecords[i].sPropLength+kAnnexBHeaderSize);
                if (sps) {
                    int profile, level;
                    svlive555_parse_sps(sPropRecords[i].sPropBytes, sPropRecords[i].sPropLength,
                                        &width, &height, &profile, &level);
                    memcpy(sps, kAnnexBHeader, kAnnexBHeaderSize);
                    memcpy(sps+kAnnexBHeaderSize,
                            sPropRecords[i].sPropBytes,
                            sPropRecords[i].sPropLength);
                    spsSize = sPropRecords[i].sPropLength+kAnnexBHeaderSize;
                    video.SetProfile(profile);
                    video.SetLevel(level);
                    logCb(logInfo, _FMT("Stream parameters: w="<<width<<" h="<<height<<" profile="<<profile<<" level="<<level));
                }
//                    logCb(logInfo, _FMT("SPS: "<<std::setw(spsSize)<<sps));
            } else if (nal_unit_type == 8/*PPS*/) {
                pps = (uint8_t*)malloc(sPropRecords[i].sPropLength+kAnnexBHeaderSize);
                if (pps) {
                    memcpy(pps, kAnnexBHeader, kAnnexBHeaderSize);
                    memcpy(pps+kAnnexBHeaderSize,
                            sPropRecords[i].sPropBytes,
                            sPropRecords[i].sPropLength);
                    ppsSize = sPropRecords[i].sPropLength+kAnnexBHeaderSize;
                }
//                    logCb(logInfo, _FMT("PPS: "<<std::setw(ppsSize)<<pps));
            }
        }

        if (sPropRecords) {
            delete [] sPropRecords;
        }
    }

    return 0;
}


//-------------------------------------------------------------------------
bool DemuxRTSPClientImpl::IsVideoCodecSuported(MediaSubsession* subsession, SubstreamState* param)
{
    bool res = true;
    int  codecId;

    const char* strCodecName = subsession->codecName();

    if (!_stricmp(strCodecName,"h264")) {
        codecId = Codec::h264;
    } else
    if (!_stricmp(strCodecName,"mp4") ||
        !_stricmp(strCodecName,"mp4v-es") ) {
        codecId = Codec::mp4;
    } else
    if (!_stricmp(strCodecName,"jpeg") ||
        !_stricmp(strCodecName,"jpg") ||
        !_stricmp(strCodecName,"mjpeg") ||
        !_stricmp(strCodecName,"mjpg") ) {
        codecId = Codec::mjpeg;
    } else {
        logCb(logError, _FMT("Unsupported video codec: " << strCodecName));
        res = false;
    }

    if (res && param) {
        param->SetCodecId( codecId );
    }
    return res;
}

//-------------------------------------------------------------------------
bool DemuxRTSPClientImpl::IsAudioCodecSuported(MediaSubsession* subsession, SubstreamState* param)
{
    bool res = true;
    int  codecId;

    const char* strCodecName = subsession->codecName();

    if ( clientCb->audioTranscodingEnabled() && !_stricmp(strCodecName,"pcmu") ) {
        codecId = Codec::pcmu;
    } else
    if ( clientCb->audioTranscodingEnabled() && !_stricmp(strCodecName,"pcma") ) {
        codecId = Codec::pcma;
    } else
    if (!_stricmp(strCodecName, "mpeg4-generic")) {
        // a=fmtp:108 streamtype=5; profile-level-id=15; mode=AAC-hbr; config=1410; SizeLength=13; IndexLength=3; IndexDeltaLength=3; Profile=1;
        const char* mode = subsession->attrVal_str("mode");
        if ((mode && !_stricmp(mode, "AAC-hbr")) ||
            (mode && !_stricmp(mode, "AAC-lbr"))) {
            codecId = Codec::aac;
        } else {
            logCb(logError, _FMT("Unsupported MP4 codec: " << (mode?mode:"NULL")));
            res = false;
        }
    } else {
        logCb(logError, _FMT("Unsupported audio codec: " << strCodecName));
        res = false;
    }

    if (res && param) {
        param->SetCodecId( codecId );
    }
    return res;
}

//-------------------------------------------------------------------------
int DemuxRTSPClientImpl::InitCodecContext()
{
    int result = 0;

    if (!setupInProgressSubsession) {
        return -1;
    }

    if (!_stricmp(setupInProgressSubsession->mediumName(), "video")) {
        if (!IsVideoCodecSuported(setupInProgressSubsession, &video)) {
            result = -1;
        }
    } else if (!_stricmp(setupInProgressSubsession->mediumName(), "audio")) {
        if (!IsAudioCodecSuported(setupInProgressSubsession, &audio)) {
            result = -1;
        }
    } else {
        logCb(logError, _FMT("Unsupported medium: " << setupInProgressSubsession->mediumName()));
        result = -1;
    }

    return result;
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::NotifyOpStarted()
{
    pendingOperations++;
    assert( state != Error );
    eventLoopWatchVariable = 0;
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::NotifyOpFinished()
{
    if (--pendingOperations) {
        eventLoopWatchVariable = 1;
    }
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::OnCriticalError()
{
    // make sure event loop exits
    state = Error;
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::_DescribeResponse(int resultCode, char* resultString)
{
    NotifyOpFinished();

    TRACE_CPP(_FMT("DemuxRTSPClientImpl::OnDescribeResponse - begin"));
    if (resultCode != 0) {
        logCb(logError, _FMT("DemuxRTSPClientImpl::OnDescribeResponse - end - error: " <<
                            "Failed to get a SDP description: " << (const char*)resultString));
        delete[] resultString;
        OnCriticalError();
        return;
    }

    char* const sdpDescription = resultString;
    logCb(logDebug, _FMT("Got SDP description:\n" << sdpDescription));

    // Create a media session object from this SDP description:
    session = MediaSession::createNew(envir(), sdpDescription);
    delete[] sdpDescription; // because we don't need it anymore
    if ( session == NULL ) {
        logCb(logError, _FMT("Failed to create a MediaSession object from the SDP description: "
                        << (const char*)envir().getResultMsg()));
    } else if (!session->hasSubsessions()) {
        logCb(logError, _FMT("This session has no media subsessions (i.e., no \"m=\" lines"));
    } else {
        // Then, create and set up our data source objects for the session.
        // We do this by iterating over the session's 'subsessions',
        // calling "MediaSubsession::initiate()", and then sending a RTSP
        // "SETUP" command, on each one.
        // (Each 'subsession' will have its own data source.)
        iter = new MediaSubsessionIterator(*session);
        SetupNextSubsession();
        TRACE_CPP(_FMT("DemuxRTSPClientImpl::OnDescribeResponse - end" ));
        return;
    }

    // An unrecoverable error occurred with this stream.
    OnCriticalError();
    TRACE_CPP(_FMT("DemuxRTSPClientImpl::OnDescribeResponse - end - error" ));
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::_SetupResponse   (int resultCode, char* resultString)
{
    TRACE_CPP(_FMT("DemuxRTSPClientImpl::OnSetupResponse - begin"));
    NotifyOpFinished();

    if (resultCode != 0) {
        logCb(logError, _FMT("Failed to initiate the \"" << M_STR(setupInProgressSubsession) <<
                        "\" setupInProgressSubsession: " << envir().getResultMsg()));
    } else if (InitCodecContext()<0) {
        // error log message had been emitted
    } else if (SetSpropsParams(setupInProgressSubsession)<0) {
        // error log message had been emitted
    } else {
        logCb(logInfo, _FMT("Set up the \"" << M_STR(setupInProgressSubsession) <<
                                    "\" subsession (client port " <<
                                    setupInProgressSubsession->clientPortNum() << ")"));





        // Having successfully setup the subsession,
        // create a data sink for it, and call "startPlaying()" on it.
        // (This will prepare the data sink to receive data;
        // the actual flow of data from the client won't start happening until later,
        // after we've sent a RTSP "PLAY" command.)
        setupInProgressSubsession->sink = new L555DemuxSink(envir());
        // perhaps use your own custom "MediaSink" subclass instead
        if (setupInProgressSubsession->sink == NULL) {
            logCb(logError, _FMT("Failed to create a data sink for the \"" <<
                                M_STR(setupInProgressSubsession) << "\" subsession: " <<
                                envir().getResultMsg()));
        } else {
            logCb(logDebug, _FMT("Created a data sink for the \"" <<
                                M_STR(setupInProgressSubsession) << "\" subsession"));
            // a hack to let subsession handler functions get the "RTSPClient"
            // from the subsession
            setupInProgressSubsession->miscPtr = this;
            FramedSource* src = setupInProgressSubsession->readSource();
            setupInProgressSubsession->sink->startPlaying(*src,
                                            OnSubsessionPlayDone,
                                            setupInProgressSubsession);
            // Also set a handler to be called if a RTCP "BYE" arrives for this subsession:
            RTCPInstance* rtcp = setupInProgressSubsession->rtcpInstance();
            if (rtcp != NULL) {
                rtcp->setByeHandler(OnSubsessionRTCPBye, setupInProgressSubsession);
            }
        }
    }
    delete[] resultString;
    // Set up the next subsession, if any:
    setupInProgressSubsession = NULL;
    SetupNextSubsession();
    TRACE_CPP(_FMT("DemuxRTSPClientImpl::OnSetupResponse - end"));
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::_PingResponse(int resultCode,
                        char* resultString,
                        bool _serverSupportsGetParameter)
{
    if ( resultCode != 0 ) {
        logCb(logWarning, _FMT("Error " << resultCode << " (" << resultString << ") after keep-alive attempt!"));
        if ( resultCode < 0 ) {
            // no response from the server ...
        }
        serverSupportsGetParameter = false;
    } else {
        TRACE_CPP(_FMT("Session refresh success!"));
        serverSupportsGetParameter = _serverSupportsGetParameter;
    }
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::OnOptionsResponse(RTSPClient* rtspClient, int resultCode, char* resultString)
{
    Boolean serverSupportsGetParameter = False;
    if (resultCode == 0) {
    // Note whether the server told us that it supports the "GET_PARAMETER" command:
        serverSupportsGetParameter = RTSPOptionIsSupported("GET_PARAMETER", resultString);
    }
    ((DemuxRTSPClientImpl*)rtspClient)->_PingResponse(resultCode,
                                                            resultString,
                                                            serverSupportsGetParameter == True);
    delete[] resultString;
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::OnGetParameterResponse(RTSPClient* rtspClient, int resultCode, char* resultString)
{
    ((DemuxRTSPClientImpl*)rtspClient)->_PingResponse(resultCode, resultString, True);
    delete[] resultString;
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::UpdateSession()
{
    if ( sv_time_get_elapsed_time(lastSessionRefreshTime) > sessionTimeout &&
            session != NULL ) {
        TRACE_CPP(_FMT("Sending session refresh"));

        if ( serverSupportsGetParameter && enableGetParameter ) {
            sendGetParameterCommand(*session, OnGetParameterResponse, NULL);
        } else {
            sendOptionsCommand( OnOptionsResponse );
        }

        lastSessionRefreshTime = sv_time_get_current_epoch_time();
    }
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::_PlayResponse    (int resultCode, char* resultString)
{
    char buffer[2048];
    TRACE_CPP(_FMT("DemuxRTSPClientImpl::OnPlayResponse - begin" ));
    NotifyOpFinished();
    if (resultCode != 0) {
        logCb(logError, _FMT("Failed to start playback on \"" << S_ID_STR(buffer) <<
                            "\" session: " << resultString));
        delete[] resultString;
    } else {
        if ( audio.IsActive() && !audio.PrepareRead() ) {
            logCb(logError, "Failed to prepare audio substream for reading");
            state = Error;
        } else if ( !video.PrepareRead() ) {
            logCb(logError, "Failed to prepare video substream for reading");
            state = Error;
        } else {
            logCb(logDebug, _FMT("Started playback on \"" << S_ID_STR(buffer) << "\" session" ));
            delete[] resultString;
            state = Streaming;

            lastSessionRefreshTime = sv_time_get_current_epoch_time();
            sessionTimeout = sessionTimeoutParameter() ? sessionTimeoutParameter()*1000 : kDefaultSessionTimeout;
            sessionTimeout -= sessionTimeout/10; // refresh the session before the actual timeout expires ... 10% buffer should do it
        }
    }
    TRACE_CPP(_FMT("DemuxRTSPClientImpl::OnPlayResponse - end" ));
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::OnSubsessionDone(MediaSubsession* subsession)
{
    TRACE_CPP(_FMT("DemuxRTSPClientImpl::OnSubsessionDone - begin" ));
    // Begin by closing this subsession's stream:
    Medium::close(subsession->sink);
    subsession->sink = NULL;

    // Next, check whether *all* subsessions' streams have now been closed:
    MediaSession& session = subsession->parentSession();
    MediaSubsessionIterator iter(session);
    while ((subsession = iter.next()) != NULL) {
        if (subsession->sink != NULL) {
            logCb(logTrace,_FMT("DemuxRTSPClientImpl::OnSubsessionDone - end." <<
                                "Some sessions still active" ));
            return; // this subsession is still active
        }
    }

    // All subsessions' streams have now been closed, so shutdown the client:
    TRACE_CPP(_FMT("DemuxRTSPClientImpl::OnSubsessionDone - end" ));
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::_SubsessionRTCPBye(MediaSubsession* subsession)
{
    TRACE_CPP(_FMT("DemuxRTSPClientImpl::OnSubsessionRTCPBye - begin " <<
                        M_STR(subsession)));
    OnSubsessionDone(subsession);
    TRACE_CPP(_FMT("DemuxRTSPClientImpl::OnSubsessionRTCPBye - end"));
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::_SubsessionPlayDone(MediaSubsession* subsession)
{
    TRACE_CPP(_FMT("DemuxRTSPClientImpl::OnSubsessionPlayDone - begin " <<
                        M_STR(subsession)));
    OnSubsessionDone(subsession);
    TRACE_CPP(_FMT("DemuxRTSPClientImpl::OnSubsessionPlayDone - end"));
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::_Timeout()
{
    if ( timeoutValue > kFauxTimeout ) {
        int64_t diff = sv_time_get_elapsed_time(eventLoopStart);
        logCb(logError,_FMT("Timeout: elapsed=" << diff << " timeout=" << timeoutValue ));
    }
    timeoutOccurred = 1;
    eventLoopWatchVariable = 1;
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::OnAfterGettingFrame   ( unsigned frameSize,
                            unsigned numTruncatedBytes,
                            struct timeval presentationTime,
                            unsigned durationInMicroseconds)
{
    TRACE_CPP_L( 3, _FMT(THREAD_ID << "afterGettingFrame: size=" << frameSize <<
                                            " truncated="<<numTruncatedBytes<<
                                            " duration="<<durationInMicroseconds));
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::OnDescribeResponse(RTSPClient* rtspClient,
                                int resultCode,
                                char* resultString)
{
    ((DemuxRTSPClientImpl*)rtspClient)->_DescribeResponse(resultCode,
                                                    resultString);
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::OnSetupResponse   (RTSPClient* rtspClient,
                                int resultCode,
                                char* resultString)
{
    ((DemuxRTSPClientImpl*)rtspClient)->_SetupResponse(resultCode,
                                                    resultString);
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::OnPlayResponse    (RTSPClient* rtspClient,
                                int resultCode,
                                char* resultString)
{
    ((DemuxRTSPClientImpl*)rtspClient)->_PlayResponse(resultCode,
                                                    resultString);
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::OnSubsessionRTCPBye ( void* clientData )
{
    MediaSubsession* subsession = (MediaSubsession*)clientData;
    ((DemuxRTSPClientImpl*)subsession->miscPtr)->_SubsessionRTCPBye(subsession);
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::OnSubsessionPlayDone ( void* clientData )
{
    MediaSubsession* subsession = (MediaSubsession*)clientData;
    ((DemuxRTSPClientImpl*)subsession->miscPtr)->_SubsessionPlayDone(subsession);
}

//-------------------------------------------------------------------------
void DemuxRTSPClientImpl::OnTimeout ( void* clientData )
{
    ((DemuxRTSPClientImpl*)clientData)->_Timeout();
}

//-----------------------------------------------------------------------------
void DemuxRTSPClientImpl::OnLocalTimestampSwitch ( )
{
    video.SwitchToLocalTimestamps();
    audio.SwitchToLocalTimestamps();
}

//-----------------------------------------------------------------------------
void DemuxRTSPClientImpl::OnSubstreamFrameReady  ( FrameBuffer* frame,
                                                bool& merged )
{
    merged = false;
    if ( frame->GetMediaType() == MediaType::video &&
         aggregateNALU &&
         video.GetCodecId() == Codec::h264 ) {
        // search for the last saved video frame,
        // and if it has the same PTS,  merge the two
        FrameContainer::reverse_iterator it = savedFrames.rbegin();
        while ( it != savedFrames.rend() ) {
            if ( (*it)->GetMediaType() == MediaType::video ) {
                merged = (*it)->Merge(frame);
                if ( merged ) {
                    frame->Release();
                    break;
                }
            }
            it++;
        }
    }
    if ( !merged ) {
        savedFrames.push_back( frame );
    }
    eventLoopWatchVariable = 1;
}

//-----------------------------------------------------------------------------
void DemuxRTSPClientImpl::OnSubstreamClosed      ( )
{
    streamClosed = 1;
    eventLoopWatchVariable = 1;
}

//-----------------------------------------------------------------------------
void DemuxRTSPClientImpl::OnSubstreamError       ( )
{
    state = Error;
    eventLoopWatchVariable = 1;
}

//-----------------------------------------------------------------------------
void L555DemuxSink::afterGettingFrame   (void* clientData,
                                unsigned frameSize,
                                unsigned numTruncatedBytes,
                                struct timeval presentationTime,
                                unsigned durationInMicroseconds)
{
    ((DemuxRTSPClientImpl*)clientData)->OnAfterGettingFrame(frameSize,
                                                numTruncatedBytes,
                                                presentationTime,
                                                durationInMicroseconds);
}


}; // namespace live555

}; // namespace sio