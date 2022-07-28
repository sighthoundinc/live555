#ifndef STREAM_LIVE555_DEMUX_H
#define STREAM_LIVE555_DEMUX_H


#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>
#include <RTSPCommon.hh>

#include <memory>
#include <string>
#include <cmath>
#include <list>
#include <assert.h>

#include "svlive555_export.h"

namespace sio {

namespace live555 {

//-----------------------------------------------------------------------------
template <class T>
class StatsItem
{
public:
    void reset()
    {
        min = max = last = cumulative = sampleCount = 0;
    }

    void update(T value)
    {
        if (value<min || sampleCount==0) min=value;
        if (value>max || sampleCount==0) max=value;
        last = value;
        cumulative += value;
        sampleCount++;
    }

    void add(T value)
    {
        update(last + value);
    }

    T average()
    {
        return sampleCount ? cumulative / sampleCount : 0;
    }

    void combine(const StatsItem<T>& other)
    {
        if (other.sampleCount!=0) {
            if (other.min<min || sampleCount==0) min = other.min;
            if (other.max>max || sampleCount==0) max = other.max;
        }
        sampleCount += other.sampleCount;
        cumulative += other.cumulative;
    }
public:
    T        min = 0;
    T        max = 0;
    T        last = 0;
    T        cumulative = 0;
    size_t   sampleCount = 0;
};

//-----------------------------------------------------------------------------
class StatsSnapshot
{
public:
    uint64_t            time;
    size_t              framesProcessed;
    size_t              framesLost;
    StatsItem<int64_t>  frameSize;
    StatsItem<int64_t>  interFrameTime;
    StatsItem<int64_t>  frameAcquisitionTime;
    StatsItem<int64_t>  ptsDivergence;

public:
    void reset();
    void combine(const StatsSnapshot& other);
};

//-----------------------------------------------------------------------------
class ITimestampCreator
{
public:
    virtual ~ITimestampCreator() {};
    virtual int assignTs(int64_t receivedTs, int64_t& assignedTs, bool rtcpSyncOccurred) = 0;
    virtual std::string stats() const = 0;
    virtual void setTsBase(int64_t base) = 0;
};


//-----------------------------------------------------------------------------
class L555DemuxSink : public MediaSink
{
public:
    L555DemuxSink(UsageEnvironment& e);
    virtual Boolean continuePlaying();
    static void afterGettingFrame   (void* clientData,
                                    unsigned frameSize,
                                    unsigned numTruncatedBytes,
                                    struct timeval presentationTime,
                                    unsigned durationInMicroseconds);
};

//-----------------------------------------------------------------------------
struct Codec {
    enum {
    aac,
    pcma,
    pcmu,
    h264,
    mp4,
    mjpeg,
    unknown
    };
};

struct MediaType {
    enum {
    video,
    audio,
    unknown
    };
};

class FrameBuffer
{
public:
    virtual ~FrameBuffer() = default;
    virtual int         GetCodec() const = 0;
    virtual void        SetCodec(int) = 0;
    virtual int         GetMediaType() const = 0;
    virtual void        SetMediaType(int) = 0;
    virtual bool        Merge(FrameBuffer* other) = 0;
    virtual void        Shrink() = 0;
    virtual void        Release() = 0;
    virtual uint64_t    Pts() const = 0;
    virtual void        SetPts(uint64_t ts) = 0;
    virtual uint64_t    DataSize() const = 0;
    virtual uint64_t    AllocSize() const = 0;
    virtual bool        Append(uint8_t* data, size_t size, bool updateSize=true) = 0;
    virtual uint64_t    FreeSpace() const = 0;
    virtual bool        EnsureFreeSpace(uint64_t additonalRequired) = 0;
    virtual uint8_t*    WritePtr() = 0;
    virtual uint8_t*    ReadPtr() = 0;
    virtual int         GetContainedNALU() const = 0;
    virtual void        OnDataWritten(size_t size) = 0;
    virtual bool        ParseNALU() = 0;
    virtual bool        IsKeyframe() const = 0;
    virtual bool        ContainsTimedData() const = 0;
};

//-----------------------------------------------------------------------------
class ILive555APIBridge
{
public: // factories
    virtual ~ILive555APIBridge() = default;
    virtual ITimestampCreator* createTimestampCreator(const char* name) = 0;
    virtual FrameBuffer*       createFrameBuffer(size_t size, bool isVideo) = 0;
    virtual bool               audioTranscodingEnabled() = 0;
    virtual const char*        sanitizeUrl(const char* str, char* buffer, size_t sizeOfBuffer) = 0;
    virtual void               release() = 0;

public: // logging
    virtual void logError(const char* msg) = 0;
    virtual void logWarning(const char* msg) = 0;
    virtual void logInfo(const char* msg) = 0;
    virtual void logDebug(const char* msg) = 0;
    virtual void logTrace(const char* msg) = 0;
};

//-----------------------------------------------------------------------------
class SVLIVE555_EXPORT MyDemuxBasicUsageEnvironment: public BasicUsageEnvironment
{
public:
    static MyDemuxBasicUsageEnvironment* createNew(ILive555APIBridge* clientCbParam);
    static void destroy(MyDemuxBasicUsageEnvironment*);

    virtual UsageEnvironment& operator<<(char const* str);
    virtual UsageEnvironment& operator<<(int i);
    virtual UsageEnvironment& operator<<(unsigned u);
    virtual UsageEnvironment& operator<<(double d);
    virtual UsageEnvironment& operator<<(void* p);

    static void setTraceLevel(int l);

protected:
    MyDemuxBasicUsageEnvironment(TaskScheduler* taskScheduler,
                            ILive555APIBridge* clientCbParam);

private:
    TaskScheduler*      taskScheduler;
    ILive555APIBridge*  clientCb;
};

//-----------------------------------------------------------------------------
class Profiler
{
    int64_t startTime;
public:
    Profiler();
    void Start();
    long ElapsedMs();
};

//-----------------------------------------------------------------------------
class SubstreamStateCb
{
public:
    virtual void OnLocalTimestampSwitch ( ) = 0;
    virtual void OnSubstreamFrameReady  ( FrameBuffer* frame, bool& merged ) = 0;
    virtual void OnSubstreamClosed      ( ) = 0;
    virtual void OnSubstreamError       ( ) = 0;
};

//-----------------------------------------------------------------------------
class SubstreamState
{
public:
    SubstreamState          ( SubstreamStateCb* cb,
                                int mediaType,
                                size_t defFrameSize,
                                ILive555APIBridge* clientCbParam );
    ~SubstreamState         ( );
    void SwitchToLocalTimestamps ( );
    void ProcessStats       ( std::ostringstream& str);
    bool IsActive           ( ) const { return subsession && subsession->readSource(); }
    bool PrepareRead        ( );
    bool Reading            ( ) const;
private:    // local utility meetings
    bool AppendData         ( uint8_t* data,
                                size_t size,
                                bool updateSize=true);
    size_t StorageAvailable ( );
    bool ValidateStorage    ( size_t additionalSizeRequired );
    void FormatStats        ( StatsSnapshot& ls,
                                bool lifetime,
                                std::ostringstream& str );
    int CalculateLocalPts   ( int64_t timeToReceive );
    int CalculatePts        ( int64_t timeToReceive );
    void CheckRTCPSync      ( );
    void FinalizeFrame      ( );

public:     // getters and setters
    void SetProfile         (int _profile) { profile = _profile; };
    int  GetProfile         ( ) const { return profile; };
    void SetLevel           (int _level) { level = _level; };
    int  GetLevel           ( ) const { return level; };
    void SetCodecId         (int _codecId) { codecId = _codecId; streamHasTimingInfo = (codecId != (int)Codec::mjpeg); }
    int  GetCodecId         ( ) const { return subsession?codecId:(int)Codec::unknown; }
    size_t GetSocketBufferSize ( ) const { return socketBufferSize; }
    void SetSocketBufferSize( size_t s ) { socketBufferSize = s; }
    size_t GetFrameSize     ( ) const { return frameSize; }
    void SetFrameSize       ( size_t s ) { frameSize = s; }
    int  GetFramesProcessed ( ) const;
    int  GetFramesLost      ( ) const;
    int  GetBitrate         ( ) const;
    int  GetNumChannels     ( ) const { return subsession?subsession->numChannels():-1; }
    int  GetIntAttr         ( const char* name ) const { return subsession?subsession->attrVal_int(name):-1; }
    const char* GetStrAttr  ( const char* name ) const { return subsession?subsession->attrVal_str(name):NULL; }
    int64_t TimeSinceRead   ( ) const;
    const MediaSubsession* GetSubsession() const { return subsession; }
    void SetSubsession      ( MediaSubsession* s ) { subsession = s; }
    int64_t GetUptime       () const;

protected:    // callbacks
    static void OnData             ( void *clientData,
                                            unsigned int size,
                                            unsigned int truncated,
                                            struct timeval pts,
                                            unsigned int duration )
    {
        return ((SubstreamState*)clientData)->OnData(size, truncated, pts, duration);
    }

    static void OnClose            ( void* clientData )
    {
        return ((SubstreamState*)clientData)->OnClose();
    }

    void OnData                     ( unsigned int size,
                                            unsigned int truncated,
                                            struct timeval pts,
                                            unsigned int duration );
    void OnClose                    ( );

private: // TODO: This should be private ... visibility needs to be fixed
    SubstreamStateCb*               substreamCb;
    int                             mediaType;     // video vs audio
    const char*                     name;
    bool                            activeRead;    // whether we're actively reading this substream's frame now
    ILive555APIBridge*              clientCb;
    MediaSubsession*                subsession;
    int64_t                         dataPts;       // pts as notified by callback
    int64_t                         prevDataPts;   // previous value
    StatsSnapshot                   lifetimeStats;
    StatsSnapshot                   intervalStats;
    size_t                          frameSize;     // frame size we want to allocate
    size_t                          socketBufferSize;
    int64_t                         startPts;      // first pts we've seen
    int64_t                         startTime;     // time we've received first packet at
    int64_t                         prevFramePts;  // previous frame PTS
    int                             prevFrameSize; // previous frame size
    int64_t                         prevCaptureTime; // previous frame capture time
    bool                            rtcpSync;      // whether RTCP sync had occurred
    int64_t                         rtcpSyncTime;
    bool                            useLocalTimestamps; // use capture timestamps
    int64_t                         localTimestampAdjustment;
    bool                            streamHasTimingInfo;
    int                             codecId;
    int                             profile;       // only applies to video substream
    int                             level;       // only applies to video substream
    FrameBuffer*                    currentFrame;
    std::unique_ptr<ITimestampCreator>   tsCreator;
    int64_t                         readStartTime;  // time next frame was requested
};

//-----------------------------------------------------------------------------
class VideoSubstreamState : public SubstreamState
{
public:
    VideoSubstreamState ( SubstreamStateCb* cb, size_t defFrameSize,
                        ILive555APIBridge* clientCbParam )
        : SubstreamState( cb, (int)MediaType::video, defFrameSize, clientCbParam ) {}
};

//-----------------------------------------------------------------------------
class AudioSubstreamState : public SubstreamState
{
public:
    AudioSubstreamState ( SubstreamStateCb* cb, size_t defFrameSize,
                        ILive555APIBridge* clientCbParam )
        : SubstreamState( cb, (int)MediaType::audio, defFrameSize, clientCbParam ) {}
};

//-----------------------------------------------------------------------------
class SVLIVE555_EXPORT DemuxRTSPClientImpl
    : public RTSPClient
    , public SubstreamStateCb
{
    friend class L555DemuxSink;

    enum State { Idle, Setup, Streaming, Closing, Error };

    MediaSubsessionIterator*        iter;
    MediaSession*                   session;
    MediaSubsession*                setupInProgressSubsession;
    int                             sessionTimeout;
    bool                            serverSupportsGetParameter;
    bool                            enableGetParameter;
    int64_t                         lastSessionRefreshTime;
    double                          duration;
    Boolean                         forceTCP;
    int                             initTimeout;
    int                             packetTimeout;

    ILive555APIBridge*              clientCb;
    char                            eventLoopWatchVariable;
    State                           state;
    int                             taskRunning;
    std::string                     rtspURLCopy;
    int                             pendingOperations;
    TaskToken                       timeoutTask;
    int                             timeoutValue;
    int64_t                         eventLoopStart;
    int                             timeoutOccurred;
    int                             streamClosed;
    int                             statsIntervalMSec;
    int64_t                         statsLastReportTime;
    bool                            aggregateNALU;

    // describes state of input


    uint8_t*                        sps;
    int                             spsSize;
    int                             width;
    int                             height;
    uint8_t*                        pps;
    int                             ppsSize;


    typedef std::list<FrameBuffer*>   FrameContainer;
    FrameContainer                  savedFrames;
    int                             savedVideoFramesCount;

    AudioSubstreamState             audio;
    VideoSubstreamState             video;

    Authenticator*                  authenticator;
    bool                            removeUserNamePassword;

public:
    DemuxRTSPClientImpl(  BasicUsageEnvironment* myEnvir,
                ILive555APIBridge*      clientCbParam,
                char const*             rtspURL,
                int                     verbosityLevel,
                char const*             applicationName,
                int                     forceTCPParam,
                portNumBits             tunnelOverHTTPPortNum = 0,
                bool                    removeUsernamePasswordParam = false);
    virtual ~DemuxRTSPClientImpl();


public:         // operations
    int Init                            ( );
    int Run                             ( int timeout );
    int Close                           ( );
    FrameBuffer* ReadFrame              ( );


public:         // getters and setters
    char* GetSPS                        ( ) const { return (char*)sps; }
    char* GetPPS                        ( ) const { return (char*)pps; }
    size_t GetSPSSize                   ( ) const { return spsSize; }
    size_t GetPPSSize                   ( ) const { return ppsSize; }
    size_t GetWidth                     ( ) const { return width; }
    size_t GetHeight                    ( ) const { return height; }
    int GetH264Profile                  ( ) const { return video.GetProfile(); }
    int GetH264Level                    ( ) const { return video.GetLevel(); }
    int GetVideoCodecId                 ( ) const { return video.GetCodecId(); }
    int GetAudioCodecId                 ( ) const { return audio.GetCodecId(); }
    int GetVideoBitrate                 ( ) const { return video.GetBitrate(); }
    int GetAudioBitrate                 ( ) const { return audio.GetBitrate(); }
    int GetAudioFramesProcessed         ( ) const { return audio.GetFramesProcessed(); }
    int GetVideoFramesProcessed         ( ) const { return video.GetFramesProcessed(); }
    int GetAudioFramesDropped           ( ) const { return audio.GetFramesLost(); }
    int GetVideoFramesDropped           ( ) const { return video.GetFramesLost(); }
    int GetAudioProfileId               ( ) const { return audio.GetIntAttr("profile"); }
    const char* GetAudioConfig          ( ) const { return audio.GetStrAttr("config"); }
    int GetAudioSampleRate              ( ) const;
    int GetAudioChannels                ( ) const { return audio.GetNumChannels(); }
    int64_t GetUptime                   ( ) const { return video.GetUptime(); }

    void SetInitTimeout                 ( int param ) { initTimeout = param; }
    void SetPacketTimeout               ( int param ) { packetTimeout = param; }
    void SetStatsInterval               ( int _statsIntervalSec ) { statsIntervalMSec = _statsIntervalSec*1000; }
    void SetGetParameterEnabled         ( bool _enabled ) { enableGetParameter = _enabled; }
    void SetAggregateNALU               ( bool param ) { aggregateNALU = param; }
    void SetBufferSize                  ( int bufferSizeKb );
    int GetBufferSize                   ( ) const;
    void SetSocketBufferSize            ( int bufferSizeKb );


private:        // utility methods
    FrameBuffer* GetFrame               ( bool flush = false );
    bool CanReturnFrame                 ( );
    int SaveFrame                       ( SubstreamState& sub, int timeElapsed );
    int SaveAudioFrame                  ( int timeElapsed );
    int SaveVideoFrame                  ( int timeElapsed );
    void ApplySocketBufferSize          ( );
    void SetupNextSubsession            ( );
    int RemoveAuthInfoFromURL           ( );
    int SetSpropsParams                 ( MediaSubsession* subs );
    bool IsVideoCodecSuported           ( MediaSubsession* subsession, SubstreamState* param );
    bool IsAudioCodecSuported           ( MediaSubsession* subsession, SubstreamState* param );
    int InitCodecContext                ( );
    void NotifyOpStarted                ( );
    void NotifyOpFinished               ( );
    void OnCriticalError                ( );
    void UpdateSession                  ( );
    void LogStats                       ( );
    void OnSubsessionDone               ( MediaSubsession* subsession );
    void OnAfterGettingFrame            ( unsigned frameSize,
                                            unsigned numTruncatedBytes,
                                            struct timeval presentationTime,
                                            unsigned durationInMicroseconds );
    bool HaveTwoVideoFrames             ( );

private:        // substream callbacks
    virtual void OnLocalTimestampSwitch ( );
    virtual void OnSubstreamFrameReady  ( FrameBuffer* frame, bool& merged );
    virtual void OnSubstreamClosed      ( );
    virtual void OnSubstreamError       ( );

private:        // callbacks
    void _DescribeResponse             ( int resultCode, char* resultString );
    void _SetupResponse                ( int resultCode, char* resultString );
    void _PlayResponse                 ( int resultCode, char* resultString );
    void _PingResponse                 ( int resultCode,
                                            char* resultString,
                                            bool _serverSupportsGetParameter);
    void _SubsessionRTCPBye            ( MediaSubsession* subsession );
    void _SubsessionPlayDone           ( MediaSubsession* subsession );
    void _Timeout                      ( );

private:        // static callbacks
    static void OnDescribeResponse      ( RTSPClient* rtspClient,
                                            int resultCode,
                                            char* resultString);
    static void OnSetupResponse         ( RTSPClient* rtspClient,
                                            int resultCode,
                                            char* resultString);
    static void OnPlayResponse          ( RTSPClient* rtspClient,
                                            int resultCode,
                                            char* resultString);
    static void OnOptionsResponse       ( RTSPClient* rtspClient,
                                            int resultCode,
                                            char* resultString);
    static void OnGetParameterResponse  ( RTSPClient* rtspClient,
                                            int resultCode,
                                            char* resultString );
    static void OnSubsessionRTCPBye     ( void* clientData );
    static void OnSubsessionPlayDone    ( void* clientData );
    static void OnTimeout               ( void* clientData );
};

}; // namespace live555

}; // namespace sio

#endif
