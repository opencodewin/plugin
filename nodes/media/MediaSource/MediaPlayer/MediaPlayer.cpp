#include <sstream>
#include <iostream>
#include <iomanip>
#include <thread>
#include <mutex>
#include <list>
#include <atomic>
#include <limits>
#include <vector>
#include <cmath>
#include <algorithm>
#include <imgui_fft.h>
#include "MediaPlayer.h"
#include "FFUtils.h"
extern "C"
{
    #include "libavutil/avutil.h"
    #include "libavutil/avstring.h"
    #include "libavutil/pixdesc.h"
    #include "libavformat/avformat.h"
    #include "libavcodec/avcodec.h"
    #include "libavdevice/avdevice.h"
    #include "libavfilter/avfilter.h"
    #include "libavfilter/buffersrc.h"
    #include "libavfilter/buffersink.h"
    #include "libswscale/swscale.h"
    #include "libswresample/swresample.h"
#ifdef EXTRA_CODECS
    #include "extra_codec.h"
#endif
}

using namespace std;

static AVPixelFormat get_hw_format(AVCodecContext *ctx, const AVPixelFormat *pix_fmts);

struct AudioData
{
    double audio_pts {0};
    float audio_decibel {0};
    ImGui::ImMat audio_wave;
};
struct AudioAttribute
{
    int audio_stack {0};
    int audio_count {0};
    std::vector<AudioData> audio_value;
};

class MediaPlayer_FFImpl : public MediaPlayer
{
    struct media_stream
    {
        int                 m_index {-1};
        enum AVMediaType    m_type {AVMEDIA_TYPE_UNKNOWN};
        AVCodecPtr          m_dec {nullptr};
        AVCodecContext *    m_dec_ctx {nullptr};
        AVStream *          m_stream {nullptr};
        SwrContext*         m_swrCtx{nullptr};
        SwrContext*         m_swrDBCtx{nullptr};
        int                 m_audpktQMaxSize{64};
        list<AVPacket*>     m_audpktQ;
        mutex               m_audpktQLock;
        thread              m_auddecThread;
        int                 m_audfrmQMaxSize{5};
        list<AVFrame*>      m_audfrmQ;
        mutex               m_audfrmQLock;
        bool                m_auddecEof{false};
        double              m_audfrmAvgDur{0.021};
        uint32_t            m_audfrmAvgDurCalcCnt{10};
        // pcm format conversion thread
        thread              m_audswrThread;
        float               m_audQDuration{0.5f};
        int                 m_swrfrmQMaxSize{24};
        list<AVFrame*>      m_swrfrmQ;
        mutex               m_swrfrmQLock;
        bool                m_swrPassThrough{false};
        bool                m_swrEof{false};
        AVSampleFormat      m_swrOutSmpfmt{AV_SAMPLE_FMT_S16};
        int                 m_swrOutSampleRate;
        int                 m_swrOutChannels;
        AVChannelLayout     m_swrOutChnLyt;
        bool                m_rendering {false};
        std::vector<AudioAttribute> m_channel_data;
        std::mutex audio_value_lock;
    };
public:
    using Clock = chrono::steady_clock;
    using Duration = chrono::duration<int64_t, milli>;
    using TimePoint = chrono::time_point<Clock>;
    static const TimePoint CLOCK_MIN;

    MediaPlayer_FFImpl() : m_audByteStream(this) {}

    bool SetAudioRender(AudioRender* audrnd) override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        if (IsPlaying())
        {
            m_errMessage = "Can NOT set audio render while the player is playing!";
            return false;
        }
        m_audrnd = audrnd;
        return true;
    }

    bool Open(const string& url) override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        if (!OpenMedia(url))
        {
            Close();
            return false;
        }
        return true;
    }

    bool Close() override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        WaitAllThreadsQuit();
        FlushAllQueues();

        if (m_audrnd)
            m_audrnd->CloseDevice();
        m_audByteStream.Flush();

        for (auto stream : m_audio_streams)
        {
            if (stream->m_swrCtx) { swr_free(&stream->m_swrCtx); stream->m_swrCtx = nullptr; }
            if (stream->m_swrDBCtx) { swr_free(&stream->m_swrDBCtx); stream->m_swrDBCtx = nullptr; }
            if (stream->m_dec_ctx) { avcodec_free_context(&stream->m_dec_ctx); stream->m_dec_ctx = nullptr; }
            delete stream;
        }
        m_audio_streams.clear();

        if (m_viddecCtx)
        {
            avcodec_free_context(&m_viddecCtx);
            m_viddecCtx = nullptr;
        }
        if (m_viddecHwDevCtx)
        {
            av_buffer_unref(&m_viddecHwDevCtx);
            m_viddecHwDevCtx = nullptr;
        }
        m_vidHwPixFmt = AV_PIX_FMT_NONE;
        m_viddecDevType = AV_HWDEVICE_TYPE_NONE;
        if (m_avfmtCtx)
        {
            avformat_close_input(&m_avfmtCtx);
            m_avfmtCtx = nullptr;
        }
        m_vidStmIdx = -1;
        m_vidStream = nullptr;
        m_viddec = nullptr;

        m_demuxEof = false;
        m_viddecEof = false;
        m_renderEof = false;

        m_vidMat.release();

        m_runStartTp = CLOCK_MIN;
        m_pauseStartTp = CLOCK_MIN;
        m_playPos = m_posOffset = 0;
        m_pausedDur = 0;
        m_audioMts = m_audioOffset = 0;
        m_audioStart = m_videoStart = -1;

        m_vidpktQMaxSize = 0;
        m_errMessage = "";
        return true;
    }

    bool Play() override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        if (m_isPlaying)
            return true;
        if (!IsOpened())
        {
            m_errMessage = "No media has been opened!";
            return false;
        }
        if (!HasVideo() && !HasAudio())
        {
            m_errMessage = "No video nor audio is to be played!";
            return false;
        }

        if (m_renderEof)
            if (!Reset())
                return false;

        if (!HasAudio())
        {
            if (m_runStartTp == CLOCK_MIN)
                m_runStartTp = Clock::now();
            if (m_pauseStartTp != CLOCK_MIN)
            {
                m_pausedDur += chrono::duration_cast<Duration>((Clock::now()-m_pauseStartTp)).count();
                m_pauseStartTp = CLOCK_MIN;
            }
        }

        if (!m_renderThread.joinable())
            StartAllThreads();
        if (m_audrnd)
            m_audrnd->Resume();
        m_isPlaying = true;
        return true;
    }

    bool Pause() override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        if (!IsOpened())
        {
            m_errMessage = "No media has been opened!";
            return false;
        }
        if (m_audrnd)
            m_audrnd->Pause();
        if (!HasAudio())
            m_pauseStartTp = Clock::now();
        m_isPlaying = false;
        return true;
    }

    bool Reset() override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        if (!IsOpened())
        {
            m_errMessage = "No media has been opened!";
            return false;
        }

        if (m_audrnd)
            m_audrnd->Pause();

        WaitAllThreadsQuit();
        FlushAllQueues();

        if (m_audrnd)
            m_audrnd->Flush();
        m_audByteStream.Flush();
        if (m_viddecCtx)
            avcodec_flush_buffers(m_viddecCtx);

        for (auto stream : m_audio_streams)
        {
            if (stream->m_dec_ctx) avcodec_flush_buffers(stream->m_dec_ctx);
            stream->m_auddecEof = false;
        }

        m_demuxEof = false;
        m_viddecEof = false;
        m_renderEof = false;

        m_runStartTp = CLOCK_MIN;
        m_pauseStartTp = CLOCK_MIN;
        m_playPos = m_posOffset = 0;
        m_pausedDur = 0;
        m_audioMts = m_audioOffset = 0;
        m_audioStart = m_videoStart = -1;

        int fferr = avformat_seek_file(m_avfmtCtx, -1, INT64_MIN, m_avfmtCtx->start_time, m_avfmtCtx->start_time, 0);
        if (fferr < 0)
        {
            SetFFError("avformat_seek_file(In Reset)", fferr);
            return false;
        }
        return true;
    }

    bool Seek(int64_t pos, bool seekToI) override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        if (!IsOpened())
        {
            m_errMessage = "No media has been opened!";
            return false;
        }

        bool isPlaying = m_isPlaying;
        m_isSeeking = true;
        if (m_audrnd)
            m_audrnd->Pause();

        WaitAllThreadsQuit();
        FlushAllQueues();

        if (m_audrnd)
            m_audrnd->Flush();
        m_audByteStream.Flush();
        if (m_viddecCtx)
            avcodec_flush_buffers(m_viddecCtx);

        for (auto stream : m_audio_streams)
        {
            if (stream->m_dec_ctx) avcodec_flush_buffers(stream->m_dec_ctx);
            stream->m_auddecEof = false;
        }

        m_demuxEof = false;
        m_viddecEof = false;
        m_renderEof = false;
        m_pauseStartTp = CLOCK_MIN;

        int64_t ffpos = av_rescale_q(pos, MILLISEC_TIMEBASE, FFAV_TIMEBASE);
        int fferr = avformat_seek_file(m_avfmtCtx, -1, INT64_MIN, ffpos, ffpos, 0);
        if (fferr < 0)
        {
            SetFFError("avformat_seek_file(In Seek)", fferr);
            return false;
        }

        cout << "Seek to " << MillisecToString(pos) << endl;
        m_isSeekToI = seekToI;
        m_isAfterSeek = true;
        m_seekToMts = pos;

        if (isPlaying)
        {
            StartAllThreads();
            if (m_audrnd)
                m_audrnd->Resume();
            m_isPlaying = true;
        }
        m_isSeeking = false;
        return true;
    }

    bool SeekAsync(int64_t pos) override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        if (!IsOpened())
        {
            m_errMessage = "No media has been opened!";
            return false;
        }

        if (!m_isSeeking)
        {
            m_isPlayingBeforeSeek = m_isPlaying;

            if (m_audrnd)
                m_audrnd->Pause();

            WaitAllThreadsQuit();
            FlushAllQueues();

            if (m_audrnd)
                m_audrnd->Flush();
            m_audByteStream.Flush();
            if (m_viddecCtx)
                avcodec_flush_buffers(m_viddecCtx);

            for (auto stream : m_audio_streams)
            {
                if (stream->m_dec_ctx) avcodec_flush_buffers(stream->m_dec_ctx);
                stream->m_auddecEof = false;
            }

            m_demuxEof = false;
            m_viddecEof = false;
            m_renderEof = false;
            m_pauseStartTp = CLOCK_MIN;

            m_asyncSeekPos = INT64_MIN;

            StartAllThreads_SeekAsync();
            m_isSeeking = true;
        }

        m_asyncSeekPos = pos;
        cout << "Seek(async) to " << MillisecToString(pos) << endl;
        return true;
    }

    bool QuitSeekAsync() override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        if (!IsOpened())
        {
            m_errMessage = "No media has been opened!";
            return false;
        }

        if (m_isSeeking)
        {
            WaitAllThreadsQuit();
            FlushAllQueues();
            if (m_viddecCtx)
                avcodec_flush_buffers(m_viddecCtx);

            for (auto stream : m_audio_streams)
            {
                //if (stream->m_dec_ctx) avcodec_flush_buffers(stream->m_dec_ctx);
                stream->m_auddecEof = false;
            }

            m_demuxEof = false;
            m_viddecEof = false;
            m_renderEof = false;
            m_pauseStartTp = CLOCK_MIN;

            int64_t currSeekPos = m_asyncSeekPos;
            int64_t ffpos;
            if (currSeekPos == INT64_MIN)
                ffpos = m_avfmtCtx->start_time;
            else
                ffpos = av_rescale_q(currSeekPos, MILLISEC_TIMEBASE, FFAV_TIMEBASE);
            int fferr = avformat_seek_file(m_avfmtCtx, -1, INT64_MIN, ffpos, ffpos, 0);
            if (fferr < 0)
            {
                SetFFError("avformat_seek_file(In QuitSeekAsync)", fferr);
                return false;
            }

            cout << "Seek to (In QuitSeekAsync) " << MillisecToString(currSeekPos) << endl;
            m_isSeekToI = false;
            m_isAfterSeek = true;
            m_seekToMts = currSeekPos;

            if (m_isPlayingBeforeSeek)
            {
                StartAllThreads();
                if (m_audrnd)
                    m_audrnd->Resume();
                m_isPlaying = true;
            }
            m_isSeeking = false;
        }

        return true;
    }

    bool IsOpened() const override
    {
        return (bool)m_avfmtCtx;
    }

    bool IsPlaying() const override
    {
        return m_isPlaying;
    }

    bool IsSeeking() const override
    {
        return m_isSeeking;
    }

    bool IsEof() const override
    {
        return m_demuxEof;
    }

    MediaInfo::InfoHolder GetMediaInfo() const override
    {
        return m_avfmtCtx ? GenerateMediaInfoByAVFormatContext(m_avfmtCtx) : nullptr;
    }

    bool HasVideo() const override
    {
        return m_vidStmIdx >= 0;
    }

    bool HasAudio() const override
    {
        return m_audio_streams.size() > 0; //m_audStmIdx >= 0;
    }

    float GetPlaySpeed() const override
    {
        return 1.0f;
    }

    bool SetPlaySpeed(float speed) override
    {
        return false;
    }

    bool SetPreferHwDecoder(bool prefer) override
    {
        m_vidPreferUseHw = prefer;
        return true;
    }

    uint64_t GetDuration() const override
    {
        if (!m_avfmtCtx)
            return 0;
        int64_t dur = av_rescale(m_avfmtCtx->duration, 1000L, AV_TIME_BASE);
        return dur < 0 ? 0 : (uint64_t)dur;
    }

    int64_t GetPlayPos() const override
    {
        return m_playPos;
    }

    void GetVideo(ImGui::ImMat& out) override
    {
        lock_guard<mutex> lk(m_vidMatLock);
        out = m_vidMat;
    }

    int GetAudioStreams() override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        return m_audio_streams.size();
    }

    int GetAudioChannels() override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        int channels = 0;
        for (auto stream : m_audio_streams)
        {
            channels += stream->m_channel_data.size();
        }
        return channels;
    }

    int GetAudioMeterStack(int channel) override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        if (channel < GetAudioChannels())
        {
            int c = 0;
            for (auto stream : m_audio_streams)
            {
                if (c < channel && c + stream->m_channel_data.size() <= channel)
                {
                    c += stream->m_channel_data.size();
                    continue;
                }
                if (c < channel) c = channel;
                else c = c - channel;
                return stream->m_channel_data[c].audio_stack;
            }
        }
        return 0;
    }

    int GetAudioMeterCount(int channel) override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        if (channel < GetAudioChannels())
        {
            int c = 0;
            for (auto stream : m_audio_streams)
            {
                if (c < channel && c + stream->m_channel_data.size() <= channel)
                {
                    c += stream->m_channel_data.size();
                    continue;
                }
                if (c < channel) c = channel;
                else c = c - channel;
                return stream->m_channel_data[c].audio_count;
            }
        }
        return 0;
    }

    bool GetAudioRendering(int channel) override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        if (channel < GetAudioChannels())
        {
            int c = 0;
            for (auto stream : m_audio_streams)
            {
                if (c < channel && c + stream->m_channel_data.size() <= channel)
                {
                    c += stream->m_channel_data.size();
                    continue;
                }
                return stream->m_rendering;
            }
        }
        return false;
    }

    bool GetAudioStreamRendering(int index) override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        auto iter = std::find_if(m_audio_streams.begin(), m_audio_streams.end(), [index](const media_stream* ss) {
            return ss->m_index == index;
        });
        if (iter != m_audio_streams.end())
            return (*iter)->m_rendering;
        return false;
    }

    int GetAudioStreamIndex(std::vector<int>& list) override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        list.clear();
        for (auto stream : m_audio_streams)
        {
            list.push_back(stream->m_index);
        }
        return list.size();
    }

    void SetAudioMeterStack(int channel, int stack) override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        if (channel < GetAudioChannels())
        {
            int c = 0;
            for (auto stream : m_audio_streams)
            {
                if (c < channel && c + stream->m_channel_data.size() <= channel)
                {
                    c += stream->m_channel_data.size();
                    continue;
                }
                if (c < channel) c = channel;
                else c = c - channel;
                if (c < stream->m_channel_data.size())
                {
                    stream->m_channel_data[c].audio_stack = stack;
                }
                break;
            }
        }
    }

    void SetAudioMeterCount(int channel, int count) override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        if (channel < GetAudioChannels())
        {
            int c = 0;
            for (auto stream : m_audio_streams)
            {
                if (c < channel && c + stream->m_channel_data.size() <= channel)
                {
                    c += stream->m_channel_data.size();
                    continue;
                }
                if (c < channel) c = channel;
                else c = c - channel;
                if (c < stream->m_channel_data.size())
                {
                    stream->m_channel_data[c].audio_count = count;
                }
                break;
            }
        }
    }

    float GetAudioMeterValue(int channel, double pts, ImGui::ImMat& mat) override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        if (channel < GetAudioChannels())
        {
            int c = 0;
            for (auto stream : m_audio_streams)
            {
                if (c < channel && c + stream->m_channel_data.size() <= channel)
                {
                    c += stream->m_channel_data.size();
                    continue;
                }
                if (c < channel) c = channel;
                else c = c - channel;
                if (c < stream->m_channel_data.size())
                {
                    lock_guard<mutex> lk(stream->audio_value_lock);
                    for (auto it = stream->m_channel_data[c].audio_value.begin(); it != stream->m_channel_data[c].audio_value.end();)
                    {
                        if (it->audio_pts < pts)
                        {
                            it = stream->m_channel_data[c].audio_value.erase(it);
                        }
                        else
                        {
                            mat = it->audio_wave;
                            return it->audio_decibel;
                        }
                    }
                }
            }
        }
        return 0;
    }

    int GetRenderingAudioIndex() const override
    {
        return m_render_audio_index;
    }

    void SetRenderingAudioIndex(int index) override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        m_render_audio_index = index;
        for (auto stream : m_audio_streams)
        {
            if (stream->m_index == index) stream->m_rendering = true;
            else stream->m_rendering = false;
        }
        m_audByteStream.Flush();
    }

    bool SetPlayMode(int mode) override
    {
        lock_guard<recursive_mutex> lk(m_ctlLock);
        if (IsOpened())
        {
            m_errMessage = "Can only change play mode when media is not opened!";
            return false;
        }
        m_playMode = mode;
        return true;
    }

    string GetError() const override
    {
        return m_errMessage;
    }

    bool CheckHwPixFmt(AVPixelFormat pixfmt)
    {
        return pixfmt == m_vidHwPixFmt;
    }

private:
    static const AVRational MILLISEC_TIMEBASE;
    static const AVRational FFAV_TIMEBASE;

    void SetFFError(const string& funcname, int fferr)
    {
        ostringstream oss;
        oss << "'" << funcname << "' returns " << fferr << ".";
        m_errMessage = oss.str();
    }

    bool OpenMedia(const string& url)
    {
        if (IsOpened())
            Close();

        int fferr = 0;

        const char * _url = nullptr;
        const AVInputFormat * ifmt = nullptr;
        AVDictionary *options = nullptr;
        if (url.empty() || url == "Camera")
        {
#ifdef _WIN32
            ifmt = av_find_input_format("dshow");
            _url = "video=Integrated Camera";
#elif defined(__APPLE__)
            ifmt = av_find_input_format("avfoundation");
            _url = "0";
            av_dict_set(&options, "framerate", "30", 0);
            //av_dict_set(&options, "video_size", "640x480", 0);
            av_dict_set(&options, "pixel_format", "nv12", 0);
#else
            ifmt = av_find_input_format("vfwcap");
            _url = "0";
#endif
        }
        else
        {
            _url = url.c_str();
        }

        if (!_url && !ifmt) return false;

        fferr = avformat_open_input(&m_avfmtCtx, _url, ifmt, &options);
        if (fferr < 0)
        {
            SetFFError("avformat_open_input", fferr);
            cout << "avformat_open_input failed:" << av_err2str(fferr) << endl;
            return false;
        }
        fferr = avformat_find_stream_info(m_avfmtCtx, nullptr);
        if (fferr < 0)
        {
            SetFFError("avformat_find_stream_info", fferr);
            cout << "avformat_find_stream_info failed:" << av_err2str(fferr) << endl;
            return false;
        }
        cout << "Open '" << url << "' successfully. " << m_avfmtCtx->nb_streams << " streams are found." << endl;

        for (int i = 0; i < m_avfmtCtx->nb_streams; i++)
        {
            AVStream * stream = m_avfmtCtx->streams[i];
            if (!stream)
                continue;
#ifdef EXTRA_CODECS
            if ((m_playMode & MP_DECODE_AUDIO || m_playMode & MP_DECODE_AUDIO_ALL) && stream->codecpar->codec_tag == AV_CODEC_ID_DRA)
            {
                stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
                stream->codecpar->codec_id = (enum AVCodecID)AV_CODEC_ID_DRA;
                if (stream->codecpar->extradata_size <= 0 && m_avfmtCtx->pb->buffer_size >=0)
                {
                    stream->codecpar->extradata = (uint8_t*)av_mallocz(m_avfmtCtx->pb->buffer_size + AV_INPUT_BUFFER_PADDING_SIZE);
                    stream->codecpar->extradata_size = m_avfmtCtx->pb->buffer_size;
                    memcpy(stream->codecpar->extradata, m_avfmtCtx->pb->buf_ptr, m_avfmtCtx->pb->buffer_size);
                }
            }
            else if ((m_playMode & MP_DECODE_VIDEO) && stream->codecpar->codec_id == AV_CODEC_ID_CAVS)
            {
                m_vidStmIdx = stream->index;
                m_vidStream = m_avfmtCtx->streams[m_vidStmIdx];
                m_viddec = ff_cavs_decoder;
            }
            else if ((m_playMode & MP_DECODE_VIDEO) && (stream->codecpar->codec_id == (enum AVCodecID)AV_CODEC_ID_AVS2 || stream->codecpar->codec_tag == AV_CODEC_TAG_AVS2 || stream->codecpar->codec_tag == 0xd2))
            {
                stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
                stream->codecpar->codec_id = (enum AVCodecID)AV_CODEC_ID_AVS2;
                m_vidStmIdx = stream->index;
                m_vidStream = m_avfmtCtx->streams[m_vidStmIdx];
                m_viddec = ff_avs2_decoder;
            }
            else if ((m_playMode & MP_DECODE_VIDEO) && (stream->codecpar->codec_id == (enum AVCodecID)AV_CODEC_ID_AVS3 || stream->codecpar->codec_tag == AV_CODEC_ID_AVS3))
            {
                stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
                stream->codecpar->codec_id = (enum AVCodecID)AV_CODEC_ID_AVS3;
                m_vidStmIdx = stream->index;
                m_vidStream = m_avfmtCtx->streams[m_vidStmIdx];
                m_viddec = ff_avs3_decoder;
                if (stream->codecpar->extradata_size <= 0 && m_avfmtCtx->pb->buffer_size >=0)
                {
                    stream->codecpar->extradata = (uint8_t*)av_mallocz(m_avfmtCtx->pb->buffer_size + AV_INPUT_BUFFER_PADDING_SIZE);
                    stream->codecpar->extradata_size = m_avfmtCtx->pb->buffer_size;
                    memcpy(stream->codecpar->extradata, m_avfmtCtx->pb->buf_ptr, m_avfmtCtx->pb->buffer_size);
                }
            }
#endif
#if LIBAVCODEC_VERSION_MAJOR < 58 || (LIBAVCODEC_VERSION_MAJOR == 58 && LIBAVCODEC_VERSION_MINOR <= 54)
            else if(stream->codecpar->codec_tag == MKTAG('A', 'V', 'S', 'V'))
            {
                stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
                stream->codecpar->codec_id = (enum AVCodecID)AV_CODEC_ID_AVS2;
                m_vidStmIdx = stream->index;
                m_vidStream = m_avfmtCtx->streams[m_vidStmIdx];
                m_viddec = ff_avs2_decoder;
            }
#endif
        }

        if ((m_playMode & MP_DECODE_VIDEO) && m_vidStmIdx == -1)
        {
#if LIBAVFORMAT_VERSION_MAJOR > 58
            m_vidStmIdx = av_find_best_stream(m_avfmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &m_viddec, 0);
#else
            m_vidStmIdx = av_find_best_stream(m_avfmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, (AVCodec **)&m_viddec, 0);
#endif
            m_vidStream = m_vidStmIdx >= 0 ? m_avfmtCtx->streams[m_vidStmIdx] : nullptr;

        }
        if (m_playMode & MP_DECODE_AUDIO)
        {
            AVCodecPtr auddec = nullptr;
#if LIBAVFORMAT_VERSION_MAJOR > 58
            int audStmIdx = av_find_best_stream(m_avfmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, &auddec, 0);
#else
            int audStmIdx = av_find_best_stream(m_avfmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, (AVCodec **)&auddec, 0);
#endif
            if (audStmIdx >= 0)
            {
                media_stream *stream = new media_stream;
                stream->m_index = audStmIdx;
                stream->m_type = AVMEDIA_TYPE_AUDIO;
                stream->m_dec = auddec;
                stream->m_stream = m_avfmtCtx->streams[audStmIdx];
                stream->m_rendering = true;
                m_audio_streams.push_back(stream);
                m_render_audio_index = audStmIdx;
            }
        }
        if (m_playMode & MP_DECODE_AUDIO_ALL)
        {
            for (int i = 0; i < m_avfmtCtx->nb_streams; i++)
            {
                auto audio_stream = m_avfmtCtx->streams[i];
                int audio_stream_index = audio_stream->index;
                if (audio_stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                {
                    media_stream *stream = new media_stream;
                    stream->m_index = audio_stream_index;
                    stream->m_type = AVMEDIA_TYPE_AUDIO;
                    stream->m_dec = m_avfmtCtx->audio_codec ? m_avfmtCtx->audio_codec : 
#ifdef EXTRA_CODECS
                                    audio_stream->codecpar->codec_id == (enum AVCodecID)AV_CODEC_ID_DRA ? ff_dra_decoder :
#endif
                                    avcodec_find_decoder(audio_stream->codecpar->codec_id);
                    stream->m_stream = audio_stream;
                    if (m_render_audio_index == -1)
                    {
                        stream->m_rendering = true;
                        m_render_audio_index = audio_stream_index;
                    }
                    m_audio_streams.push_back(stream);
                }
            }
        }
        if (m_vidStream)
        {
            if (m_vidPreferUseHw)
            {
                if (!OpenHwVideoDecoder())
                    if (!OpenVideoDecoder())
                        return false;
            }
            else
            {
                if (!OpenVideoDecoder())
                    return false;
            }
            int qMaxSize = 0;
            if (m_vidStream->avg_frame_rate.den > 0)
                qMaxSize = (int)(m_vidpktQDuration*m_vidStream->avg_frame_rate.num/m_vidStream->avg_frame_rate.den);
            if (qMaxSize < 20)
                qMaxSize = 20;
            m_vidpktQMaxSize = qMaxSize;
        }
        for (auto stream : m_audio_streams)
        {
            if (!OpenAudioDecoder(stream))
                continue;
            if (m_audrnd && m_render_audio_index != -1 && m_render_audio_index == stream->m_index)
            {
                if (!OpenAudioRender(stream))
                    continue;
            }
        }
        return true;
    }

    bool OpenVideoDecoder()
    {
        m_viddecCtx = avcodec_alloc_context3(m_viddec);
        if (!m_viddecCtx)
        {
            m_errMessage = "FAILED to allocate new AVCodecContext!";
            return false;
        }
        m_viddecCtx->opaque = this;

        int fferr;
        fferr = avcodec_parameters_to_context(m_viddecCtx, m_vidStream->codecpar);
        if (fferr < 0)
        {
            SetFFError("avcodec_parameters_to_context", fferr);
            return false;
        }

        m_viddecCtx->thread_count = 8;
        // m_viddecCtx->thread_type = FF_THREAD_FRAME;
        fferr = avcodec_open2(m_viddecCtx, m_viddec, nullptr);
        if (fferr < 0)
        {
            SetFFError("avcodec_open2", fferr);
            return false;
        }
        cout << "Video decoder '" << m_viddec->name << "' opened." << " thread_count=" << m_viddecCtx->thread_count
            << ", thread_type=" << m_viddecCtx->thread_type << endl;
        return true;
    }

    bool OpenHwVideoDecoder()
    {
        m_vidHwPixFmt = AV_PIX_FMT_NONE;
        for (int i = 0; ; i++)
        {
            const AVCodecHWConfig* config = avcodec_get_hw_config(m_viddec, i);
            if (!config)
            {
                ostringstream oss;
                oss << "Decoder '" << m_viddec->name << "' does NOT support hardware acceleration.";
                m_errMessage = oss.str();
                return false;
            }
            if ((config->methods&AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) != 0)
            {
                if (m_vidUseHwType == AV_HWDEVICE_TYPE_NONE || m_vidUseHwType == config->device_type)
                {
                    m_vidHwPixFmt = config->pix_fmt;
                    m_viddecDevType = config->device_type;
                    break;
                }
            }
        }
        cout << "Use hardware device type '" << av_hwdevice_get_type_name(m_viddecDevType) << "'." << endl;

        m_viddecCtx = avcodec_alloc_context3(m_viddec);
        if (!m_viddecCtx)
        {
            m_errMessage = "FAILED to allocate new AVCodecContext!";
            return false;
        }
        m_viddecCtx->opaque = this;

        int fferr;
        fferr = avcodec_parameters_to_context(m_viddecCtx, m_vidStream->codecpar);
        if (fferr < 0)
        {
            SetFFError("avcodec_parameters_to_context", fferr);
            return false;
        }
        m_viddecCtx->get_format = get_hw_format;

        fferr = av_hwdevice_ctx_create(&m_viddecHwDevCtx, m_viddecDevType, nullptr, nullptr, 0);
        if (fferr < 0)
        {
            SetFFError("av_hwdevice_ctx_create", fferr);
            return false;
        }
        m_viddecCtx->hw_device_ctx = av_buffer_ref(m_viddecHwDevCtx);

        fferr = avcodec_open2(m_viddecCtx, m_viddec, nullptr);
        if (fferr < 0)
        {
            SetFFError("avcodec_open2", fferr);
            return false;
        }
        cout << "Video decoder(HW) '" << m_viddecCtx->codec->name << "' opened." << endl;
        return true;
    }

    bool OpenAudioDecoder(media_stream * stream)
    {
        stream->m_dec_ctx = avcodec_alloc_context3(stream->m_dec);
        if (!stream->m_dec_ctx)
        {
            m_errMessage = "FAILED to allocate new AVCodecContext!";
            return false;
        }
        stream->m_dec_ctx->opaque = this;

        int fferr;
        fferr = avcodec_parameters_to_context(stream->m_dec_ctx, stream->m_stream->codecpar);
        if (fferr < 0)
        {
            SetFFError("avcodec_parameters_to_context", fferr);
            return false;
        }

        fferr = avcodec_open2(stream->m_dec_ctx, stream->m_dec, nullptr);
        if (fferr < 0)
        {
            SetFFError("avcodec_open2", fferr);
            return false;
        }
        
#ifdef EXTRA_CODECS
        if (stream->m_stream->codecpar->codec_id == (enum AVCodecID)AV_CODEC_ID_DRA)
        {
            avcodec_parameters_from_context(stream->m_stream->codecpar, stream->m_dec_ctx);
            stream->m_dec_ctx->time_base = stream->m_stream->time_base;
        }
#endif
        cout << "Audio decoder '" << stream->m_dec->name << "' opened." << endl;
        // setup autio meter
        for (int a = 0; a < stream->m_stream->codecpar->ch_layout.nb_channels; a++)
        {
            stream->m_channel_data.emplace(stream->m_channel_data.end());
        }
//#if LIBSWRESAMPLE_VERSION_MAJOR >= 4 && LIBSWRESAMPLE_VERSION_MINOR >= 5 && LIBSWRESAMPLE_VERSION_MICRO >= 100
        fferr = swr_alloc_set_opts2(&stream->m_swrDBCtx, &stream->m_stream->codecpar->ch_layout, AV_SAMPLE_FMT_FLTP, stream->m_stream->codecpar->sample_rate, 
                                    &stream->m_stream->codecpar->ch_layout, (AVSampleFormat)stream->m_stream->codecpar->format, stream->m_stream->codecpar->sample_rate, 0, nullptr);
//#else
//        int64_t channel_layout = av_get_default_channel_layout(stream->m_stream->codecpar->ch_layout.nb_channels);
//        stream->m_swrDBCtx = swr_alloc_set_opts(NULL, channel_layout, AV_SAMPLE_FMT_FLTP, stream->m_stream->codecpar->sample_rate, 
//                                            channel_layout, (AVSampleFormat)stream->m_stream->codecpar->format, stream->m_stream->codecpar->sample_rate, 0, nullptr);
//#endif
        if (!stream->m_swrDBCtx || fferr != 0)
        {
            m_errMessage = "FAILED to invoke 'swr_alloc_set_opts()' to create 'SwrDBContext'!";
            return false;
        }
        if (swr_init(stream->m_swrDBCtx) < 0)
        {
            SetFFError("swr_init", fferr);
            return false;
        }

        // setup sw resampler
        int inChannels = stream->m_stream->codecpar->ch_layout.nb_channels;
        AVChannelLayout inChnLyt = stream->m_stream->codecpar->ch_layout;
        int inSampleRate = stream->m_stream->codecpar->sample_rate;
        AVSampleFormat inSmpfmt = (AVSampleFormat)stream->m_stream->codecpar->format;
        stream->m_swrOutChannels = 2;
        stream->m_swrOutChnLyt.nb_channels = stream->m_swrOutChannels;
        stream->m_swrOutChnLyt.u.mask = (1 << AV_CHAN_FRONT_LEFT) | (1 << AV_CHAN_FRONT_RIGHT);
        stream->m_swrOutSmpfmt = AV_SAMPLE_FMT_S16;
        stream->m_swrOutSampleRate = inSampleRate;
        if (stream->m_swrOutChnLyt.nb_channels != inChnLyt.nb_channels || stream->m_swrOutSmpfmt != inSmpfmt || stream->m_swrOutSampleRate != inSampleRate)
        {
            fferr = swr_alloc_set_opts2(&stream->m_swrCtx, &stream->m_swrOutChnLyt, stream->m_swrOutSmpfmt, stream->m_swrOutSampleRate, &inChnLyt, inSmpfmt, inSampleRate, 0, nullptr);
            if (fferr < 0)
            {
                m_errMessage = "FAILED to invoke 'swr_alloc_set_opts()' to create 'SwrContext'!";
                return false;
            }
            int fferr = swr_init(stream->m_swrCtx);
            if (fferr < 0)
            {
                SetFFError("swr_init", fferr);
                return false;
            }
            stream->m_swrPassThrough = false;
        }
        else
        {
            stream->m_swrPassThrough = true;
        }

        m_audioMts = av_rescale_q(stream->m_stream->start_time, stream->m_stream->time_base, MILLISEC_TIMEBASE); 
        if (m_audioStart == -1)
        {
            m_audioStart = m_audioMts;
        }
        m_audioMts -= m_audioStart;
        return true;
    }

    bool OpenAudioRender(media_stream * stream)
    {
        auto bytesPerSample = av_get_bytes_per_sample(stream->m_swrOutSmpfmt);
        auto frameSize = bytesPerSample*stream->m_swrOutChannels;
        if (frameSize < 0) frameSize = 0;
        m_audByteStream.SetFrameSize(frameSize);
        if (!m_audrnd->OpenDevice(
                stream->m_swrOutSampleRate, stream->m_swrOutChannels,
                AudioRender::PcmFormat::SINT16, &m_audByteStream))
        {
            m_errMessage = m_audrnd->GetError();
            return false;
        }
        return true;
    }

    void DemuxThreadProc()
    {
        cout << "Enter DemuxThreadProc()..." << endl;
        AVPacket avpkt = {0};
        bool avpktLoaded = false;
        while (!m_quitPlay)
        {
            bool idleLoop = true;
            if (!avpktLoaded)
            {
                int fferr = av_read_frame(m_avfmtCtx, &avpkt);
                if (fferr == 0)
                {
                    avpktLoaded = true;
                    idleLoop = false;
                }
                else
                {
                    if (fferr == AVERROR_EOF)
                        cout << "Demuxer EOF." << endl;
                    else
                        cerr << "Demuxer ERROR! 'av_read_frame' returns " << fferr << "." << endl;
                    break;
                }
            }

            if (avpkt.stream_index == m_vidStmIdx)
            {
                lock_guard<mutex> lk(m_vidpktQLock);
                if (m_vidpktQ.size() < m_vidpktQMaxSize)
                {
                    AVPacket* enqpkt = av_packet_clone(&avpkt);
                    if (!enqpkt)
                    {
                        cerr << "FAILED to invoke 'av_packet_clone(DemuxThreadProc)'!" << endl;
                        break;
                    }
                    m_vidpktQ.push_back(enqpkt);
                    av_packet_unref(&avpkt);
                    avpktLoaded = false;
                    idleLoop = false;
                }
            }
            else
            {
                // find stream index
                auto iter = std::find_if(m_audio_streams.begin(), m_audio_streams.end(), [&](const media_stream* ss) {
                    return ss->m_index == avpkt.stream_index;
                });
                if (iter != m_audio_streams.end())
                {
                    media_stream * stream = *iter;
                    lock_guard<mutex> lk(stream->m_audpktQLock);
                    if (m_vidpktQMaxSize > 0 || stream->m_audpktQ.size() < stream->m_audpktQMaxSize)
                    {
                        AVPacket* enqpkt = av_packet_clone(&avpkt);
                        if (!enqpkt)
                        {
                            cerr << "FAILED to invoke 'av_packet_clone(DemuxThreadProc)'!" << endl;
                            break;
                        }
                        stream->m_audpktQ.push_back(enqpkt);
                        av_packet_unref(&avpkt);
                        avpktLoaded = false;
                        idleLoop = false;
                    }
                }
                else
                {
                    av_packet_unref(&avpkt);
                    avpktLoaded = false;
                }
            }

            if (idleLoop)
                this_thread::sleep_for(chrono::milliseconds(5));
        }
        m_demuxEof = true;
        if (avpktLoaded)
            av_packet_unref(&avpkt);
        cout << "Leave DemuxThreadProc()." << endl;
    }

    void DemuxThreadProc_SeekAsync()
    {
        cout << "Enter DemuxAsyncSeekThreadProc()..." << endl;
        bool fatalError = false;
        AVPacket avpkt = {0};
        bool avpktLoaded = false;
        bool seekingMode = false;
        int64_t seekPos0, seekPos1;
        seekPos0 = seekPos1 = INT64_MIN;
        while (!m_quitPlay)
        {
            bool idleLoop = true;

            if (HasVideo())
            {
                int64_t currSeekPos = m_asyncSeekPos;
                if (currSeekPos != INT64_MIN)
                {
                    int64_t vidSeekPos = av_rescale_q(currSeekPos, MILLISEC_TIMEBASE, m_vidStream->time_base);
                    if (vidSeekPos < seekPos0 || vidSeekPos >= seekPos1)
                    {
                        if (avpktLoaded)
                        {
                            av_packet_unref(&avpkt);
                            avpktLoaded = false;
                        }
                        int fferr = avformat_seek_file(m_avfmtCtx, m_vidStmIdx, vidSeekPos+1, vidSeekPos+1, INT64_MAX, 0);
                        if (fferr < 0)
                        {
                            cerr << "avformat_seek_file() FAILED for finding 'seekPos1'! fferr = " << fferr << "!" << endl;
                            fatalError = true;
                            break;
                        }
                        if (!ReadNextStreamPacket(m_vidStmIdx, &avpkt, &avpktLoaded, &seekPos1))
                        {
                            fatalError = true;
                            break;
                        }
                        if (avpktLoaded)
                            av_packet_unref(&avpkt);
                        fferr = avformat_seek_file(m_avfmtCtx, m_vidStmIdx, INT64_MIN, vidSeekPos, vidSeekPos, 0);
                        if (fferr < 0)
                        {
                            cerr << "avformat_seek_file() FAILED for finding 'seekPos0'! fferr = " << fferr << "!" << endl;
                            fatalError = true;
                            break;
                        }
                        if (!ReadNextStreamPacket(m_vidStmIdx, &avpkt, &avpktLoaded, &seekPos0))
                        {
                            fatalError = true;
                            break;
                        }

                        // for debug info
                        int64_t seekPos0Mts = av_rescale_q(seekPos0, m_vidStream->time_base, MILLISEC_TIMEBASE);
                        int64_t seekPos1Mts = av_rescale_q(seekPos1, m_vidStream->time_base, MILLISEC_TIMEBASE);
                        cout << "Seek range updated: seekPos0 = " << MillisecToString(seekPos0Mts) << ", seekPos1 = " << MillisecToString(seekPos1Mts) << endl;
                        ////////////////////////
                        // check the correctness of 'seekPos0' and 'seekPos1'
                        if (vidSeekPos >= seekPos0 && vidSeekPos < seekPos1)
                            cout << "\tRange is correct: " << seekPos0 << " <= " << vidSeekPos << " < " << seekPos1 << endl;
                        else
                        {
                            cout << "\tRange is not correct: " << seekPos0;
                            if (vidSeekPos >= seekPos0)
                                cout << " <= ";
                            else
                                cout << " NOT<= ";
                            cout << vidSeekPos;
                            if (vidSeekPos < seekPos1)
                                cout << " < ";
                            else
                                cout << " NOT< ";
                            cout << seekPos1 << endl;
                        }
                        ///////////////////////
                    }
                    // else
                    //     cout << "New seek pos " << vidSeekPos << " is with in current range [" << seekPos0 << ", " << seekPos1 << "]" << endl;
                }
            }
            else
            {
                seekPos0 = m_asyncSeekPos;
            }

            if (!avpktLoaded)
            {
                int fferr = av_read_frame(m_avfmtCtx, &avpkt);
                if (fferr == 0)
                {
                    avpktLoaded = true;
                    idleLoop = false;
                }
                else
                {
                    if (fferr == AVERROR_EOF)
                        cout << "Demuxer EOF." << endl;
                    else
                        cerr << "Demuxer ERROR! 'av_read_frame' returns " << fferr << "." << endl;
                    break;
                }
            }

            if (avpkt.stream_index == m_vidStmIdx)
            {
                if (m_vidpktQ.size() < m_vidpktQMaxSize && avpkt.pts < seekPos1)
                {
                    AVPacket* enqpkt = av_packet_clone(&avpkt);
                    if (!enqpkt)
                    {
                        cerr << "FAILED to invoke 'av_packet_clone(DemuxThreadProc)'!" << endl;
                        break;
                    }
                    lock_guard<mutex> lk(m_vidpktQLock);
                    m_vidpktQ.push_back(enqpkt);
                    av_packet_unref(&avpkt);
                    avpktLoaded = false;
                    idleLoop = false;
                }
            }
            else
            {
                // find stream index
                auto iter = std::find_if(m_audio_streams.begin(), m_audio_streams.end(), [&](const media_stream* ss) {
                    return ss->m_index == avpkt.stream_index;
                });
                if (iter != m_audio_streams.end() && !HasVideo())
                {
                    media_stream * stream = *iter;
                    if (m_vidpktQMaxSize > 0 || stream->m_audpktQ.size() < stream->m_audpktQMaxSize)
                    {
                        AVPacket* enqpkt = av_packet_clone(&avpkt);
                        if (!enqpkt)
                        {
                            cerr << "FAILED to invoke 'av_packet_clone(DemuxThreadProc)'!" << endl;
                            break;
                        }
                        lock_guard<mutex> lk(stream->m_audpktQLock);
                        stream->m_audpktQ.push_back(enqpkt);
                        av_packet_unref(&avpkt);
                        avpktLoaded = false;
                        idleLoop = false;
                    }
                }
                else
                {
                    av_packet_unref(&avpkt);
                    avpktLoaded = false;
                }
            }

            if (idleLoop)
                this_thread::sleep_for(chrono::milliseconds(5));
        }
        m_demuxEof = true;
        if (avpktLoaded)
            av_packet_unref(&avpkt);
        cout << "Leave DemuxAsyncSeekThreadProc()." << endl;
    }

    bool ReadNextStreamPacket(int stmIdx, AVPacket* avpkt, bool* avpktLoaded, int64_t* pts)
    {
        *avpktLoaded = false;
        int fferr;
        do {
            fferr = av_read_frame(m_avfmtCtx, avpkt);
            if (fferr == 0)
            {
                if (avpkt->stream_index == stmIdx)
                {
                    if (pts) *pts = avpkt->pts;
                    *avpktLoaded = true;
                    break;
                }
                av_packet_unref(avpkt);
            }
            else
            {
                if (fferr == AVERROR_EOF)
                {
                    if (pts) *pts = INT64_MAX;
                    break;
                }
                else
                {
                    cerr << "av_read_frame() FAILED! fferr = " << fferr << "." << endl;
                    return false;
                }
            }
        } while (fferr >= 0);
        return true;
    }

    void VideoDecodeThreadProc()
    {
        cout << "Enter VideoDecodeThreadProc()..." << endl;
        AVFrame avfrm = {0};
        bool avfrmLoaded = false;
        bool inputEof = false;
        while (!m_quitPlay)
        {
            bool idleLoop = true;
            bool quitLoop = false;

            // retrieve output frame
            bool hasOutput;
            do{
                if (!avfrmLoaded)
                {
                    int fferr = avcodec_receive_frame(m_viddecCtx, &avfrm);
                    if (fferr == 0)
                    {
                        avfrmLoaded = true;
                        idleLoop = false;
                        // handle seeking operation
                        if (m_isAfterSeek)
                        {
                            int64_t vidMts = av_rescale_q(avfrm.pts, m_vidStream->time_base, MILLISEC_TIMEBASE);
                            if (m_videoStart == -1)
                            {
                                m_videoStart = vidMts;
                            }
                            vidMts -= m_audioStart == -1 ?  m_videoStart : m_audioStart;
                            if (m_isSeekToI && !HasAudio())
                            {
                                m_seekToMts = vidMts;
                                m_isSeekToI = false;
                            }
                            if (vidMts < m_seekToMts)
                            {
                                // cout << "drop video frame after seek " << MillisecToString(vidMts) << endl;
                                av_frame_unref(&avfrm);
                                avfrmLoaded = false;
                            }
                        }
                        else if (m_isSeeking)
                        {
                            // int64_t vidMts = av_rescale_q(avfrm.pts, m_vidStream->time_base, MILLISEC_TIMEBASE);
                            // cout << "get video frame at " << MillisecToString(vidMts) << endl;
                        }
                    }
                    else if (fferr != AVERROR(EAGAIN))
                    {
                        if (fferr != AVERROR_EOF)
                            cerr << "FAILED to invoke 'avcodec_receive_frame'(VideoDecodeThreadProc)! return code is "
                                << fferr << "." << endl;
                        quitLoop = true;
                        break;
                    }
                }

                hasOutput = avfrmLoaded;
                if (avfrmLoaded)
                {
                    lock_guard<mutex> lk(m_vidfrmQLock);
                    if (m_vidfrmQ.size() < m_vidfrmQMaxSize)
                    {
                        AVFrame* enqfrm = av_frame_clone(&avfrm);
                        m_vidfrmQ.push_back(enqfrm);
                        av_frame_unref(&avfrm);
                        avfrmLoaded = false;
                        idleLoop = false;
                    }
                    else
                        break;
                }
            } while (hasOutput);
            if (quitLoop)
                break;

            // input packet to decoder
            if (!inputEof)
            {
                while (m_vidpktQ.size() > 0)
                {
                    AVPacket* avpkt = m_vidpktQ.front();
                    int fferr = avcodec_send_packet(m_viddecCtx, avpkt);
                    if (fferr == 0)
                    {
                        lock_guard<mutex> lk(m_vidpktQLock);
                        m_vidpktQ.pop_front();
                        av_packet_free(&avpkt);
                        idleLoop = false;
                    }
                    else
                    {
                        if (fferr != AVERROR(EAGAIN))
                        {
                            cerr << "FAILED to invoke 'avcodec_send_packet'(VideoDecodeThreadProc)! return code is "
                                << fferr << "." << endl;
                            if (fferr != AVERROR_UNKNOWN)
                                quitLoop = true;
                        }
                        break;
                    }
                }
                if (quitLoop)
                    break;

                if (m_vidpktQ.size() == 0 && m_demuxEof)
                {
                    // cout << "-------------> Video decoder send NULL packet to indicate EOF!" << endl;
                    avcodec_send_packet(m_viddecCtx, nullptr);
                    idleLoop = false;
                    inputEof = true;
                }
            }

            if (idleLoop)
                this_thread::sleep_for(chrono::milliseconds(5));
        }
        m_viddecEof = true;
        if (avfrmLoaded)
            av_frame_unref(&avfrm);
        cout << "Leave VideoDecodeThreadProc()." << endl;
    }

    void AudioDecodeThreadProc(media_stream * stream)
    {
        cout << "Enter AudioDecodeThreadProc()..." << endl;
        AVFrame avfrm = {0};
        bool avfrmLoaded = false;
        bool inputEof = false;
        while (!m_quitPlay)
        {
            bool idleLoop = true;
            bool quitLoop = false;

            // retrieve output frame
            bool hasOutput;
            do{
                if (!avfrmLoaded)
                {
                    int fferr = avcodec_receive_frame(stream->m_dec_ctx, &avfrm);
                    if (fferr == 0)
                    {
                        avfrmLoaded = true;
                        idleLoop = false;
                        // update average audio frame duration, for calculating audio queue size
                        double frmDur = (double)avfrm.nb_samples/stream->m_stream->codecpar->sample_rate;
                        stream->m_audfrmAvgDur = (stream->m_audfrmAvgDur*(stream->m_audfrmAvgDurCalcCnt-1)+frmDur)/stream->m_audfrmAvgDurCalcCnt;
                        stream->m_swrfrmQMaxSize = (int)ceil(stream->m_audQDuration/stream->m_audfrmAvgDur);
                        stream->m_audfrmQMaxSize = (int)ceil((double)stream->m_swrfrmQMaxSize/5);
                        // handle seeking operation
                        if (m_isAfterSeek)
                        {
                            int64_t audMts = av_rescale_q(avfrm.pts, stream->m_stream->time_base, MILLISEC_TIMEBASE);
                            if (m_audioStart == -1)
                            {
                                m_audioStart = audMts;
                            }
                            audMts -= m_audioStart;
                            if (m_isSeekToI)
                            {
                                m_seekToMts = audMts;
                                m_isSeekToI = false;
                            }
                            if (audMts < m_seekToMts)
                            {
                                m_audioMts = audMts;
                                av_frame_unref(&avfrm);
                                avfrmLoaded = false;
                            }
                            if (!HasVideo())
                                m_isAfterSeek = false;
                        }
                    }
                    else if (fferr != AVERROR(EAGAIN))
                    {
                        if (fferr != AVERROR_EOF)
                            cerr << "FAILED to invoke 'avcodec_receive_frame'(AudioDecodeThreadProc)! return code is "
                                << fferr << "." << endl;
                        else
                        {
                            quitLoop = true;
                            break;
                        }
                    }
                }

                hasOutput = avfrmLoaded;
                if (avfrmLoaded)
                {
                    lock_guard<mutex> lk(stream->m_audfrmQLock);
                    if (stream->m_audfrmQ.size() < stream->m_audfrmQMaxSize)
                    {
                        AVFrame* enqfrm = av_frame_clone(&avfrm);
                        stream->m_audfrmQ.push_back(enqfrm);
                        av_frame_unref(&avfrm);
                        avfrmLoaded = false;
                        idleLoop = false;
                    }
                    else
                        break;
                }
            } while (hasOutput);
            if (quitLoop)
                break;

            // input packet to decoder
            if (!inputEof)
            {
                while (stream->m_audpktQ.size() > 0)
                {
                    AVPacket* avpkt = stream->m_audpktQ.front();
                    int fferr = avcodec_send_packet(stream->m_dec_ctx, avpkt);
                    if (fferr == 0)
                    {
                        lock_guard<mutex> lk(stream->m_audpktQLock);
                        stream->m_audpktQ.pop_front();
                        av_packet_free(&avpkt);
                        idleLoop = false;
                    }
                    else
                    {
                        if (fferr != AVERROR(EAGAIN))
                        {
                            cerr << "FAILED to invoke 'avcodec_send_packet'(AudioDecodeThreadProc)! return code is "
                                << fferr << "." << endl;
                            if (fferr != AVERROR_INVALIDDATA)
                                quitLoop = true;
                            else
                            {
                                lock_guard<mutex> lk(stream->m_audpktQLock);
                                stream->m_audpktQ.pop_front();
                                av_packet_free(&avpkt);
                                idleLoop = false;
                            }
                        }
                        break;
                    }
                }
                if (quitLoop)
                    break;

                if (stream->m_audpktQ.size() == 0 && m_demuxEof)
                {
                    avcodec_send_packet(stream->m_dec_ctx, nullptr);
                    idleLoop = false;
                    inputEof = true;
                }
            }

            if (idleLoop)
                this_thread::sleep_for(chrono::milliseconds(5));
        }
        stream->m_auddecEof = true;
        if (avfrmLoaded)
            av_frame_unref(&avfrm);
        cout << "Leave AudioDecodeThreadProc()." << endl;
    }

    void SwrThreadProc(media_stream * stream)
    {
        while (!m_quitPlay)
        {
            bool idleLoop = true;
            if (!stream->m_audfrmQ.empty())
            {
                lock_guard<mutex> lk(stream->m_swrfrmQLock);
                if (stream->m_swrfrmQ.size() < stream->m_swrfrmQMaxSize)
                {
                    AVFrame* srcfrm = stream->m_audfrmQ.front();;
                    AVFrame* dstfrm = nullptr;
                    // do audio data scope here
                    if (stream->m_swrDBCtx)
                    {
                        AVFrame* dstDBfrm = nullptr;
                        dstDBfrm = av_frame_alloc();
                        if (dstDBfrm)
                        {
                            dstDBfrm->format = (int)AV_SAMPLE_FMT_FLTP;
                            dstDBfrm->sample_rate = stream->m_stream->codecpar->sample_rate;
                            dstDBfrm->ch_layout = stream->m_stream->codecpar->ch_layout;
                            dstDBfrm->nb_samples = swr_get_out_samples(stream->m_swrDBCtx, srcfrm->nb_samples);
                            int fferr = av_frame_get_buffer(dstDBfrm, 0);
                            if (fferr >= 0)
                            {
                                av_frame_copy_props(dstDBfrm, srcfrm);
                                dstDBfrm->pts = swr_next_pts(stream->m_swrDBCtx, srcfrm->pts);
                                int64_t mts = av_rescale_q(dstDBfrm->pts, stream->m_stream->time_base, MILLISEC_TIMEBASE);
                                mts -= m_audioStart == -1 ? 0 : m_audioStart;
                                fferr = swr_convert(stream->m_swrDBCtx, dstDBfrm->data, dstDBfrm->nb_samples, (const uint8_t **)srcfrm->data, srcfrm->nb_samples);
                                if (fferr >= 0 && dstDBfrm->nb_samples >= 64)
                                {
                                    for (int c = 0; c < dstDBfrm->ch_layout.nb_channels; c++)
                                    {
                                        if (c < stream->m_channel_data.size())
                                        {
                                            AudioData value;
                                            int sample_size = /*dstDBfrm->nb_samples > 1024 ? 1024 : dstDBfrm->nb_samples > 512 ? 512 : */dstDBfrm->nb_samples > 256 ? 256 : dstDBfrm->nb_samples > 128 ? 128 : 64;
                                            ImGui::ImMat mat;
                                            mat.create_type(sample_size, 1, 1, dstDBfrm->data[c], IM_DT_FLOAT32);
                                            value.audio_wave.clone_from(mat);
                                            value.audio_wave.flags |= IM_MAT_FLAGS_AUDIO_FRAME;
                                            ImGui::ImMat mat_fft;
                                            mat_fft.clone_from(mat);
                                            ImGui::ImRFFT((float *)mat_fft.data, mat_fft.w, true);
                                            value.audio_pts = (double)mts / 1000.0;
                                            value.audio_decibel = ImGui::ImDoDecibel((float*)mat_fft.data, mat_fft.w);
                                            {
                                                lock_guard<mutex> lk(stream->audio_value_lock);
                                                if (stream->m_channel_data[c].audio_value.size() > 1024)
                                                    stream->m_channel_data[c].audio_value.erase(stream->m_channel_data[c].audio_value.begin());
                                                stream->m_channel_data[c].audio_value.push_back(value);
                                            }
                                        }
                                    }
                                    av_frame_free(&dstDBfrm);
                                }
                            }
                        }
                    }

                    if (stream->m_swrPassThrough)
                    {
                        dstfrm = srcfrm;
                    }
                    else
                    {
                        dstfrm = av_frame_alloc();
                        if (!dstfrm)
                        {
                            m_errMessage = "FAILED to allocate new AVFrame for 'swr_convert()'!";
                            break;
                        }
                        dstfrm->format = (int)stream->m_swrOutSmpfmt;
                        dstfrm->sample_rate = stream->m_swrOutSampleRate;
                        dstfrm->ch_layout = stream->m_swrOutChnLyt;
                        dstfrm->nb_samples = swr_get_out_samples(stream->m_swrCtx, srcfrm->nb_samples);
                        int fferr = av_frame_get_buffer(dstfrm, 0);
                        if (fferr < 0)
                        {
                            SetFFError("av_frame_get_buffer(SwrThreadProc)", fferr);
                            break;
                        }
                        av_frame_copy_props(dstfrm, srcfrm);
                        dstfrm->pts = swr_next_pts(stream->m_swrCtx, srcfrm->pts);
                        fferr = swr_convert(stream->m_swrCtx, dstfrm->data, dstfrm->nb_samples, (const uint8_t **)srcfrm->data, srcfrm->nb_samples);
                        if (fferr < 0)
                        {
                            SetFFError("swr_convert(SwrThreadProc)", fferr);
                            break;
                        }
                    }
                    {
                        lock_guard<mutex> lk(stream->m_audfrmQLock);
                        stream->m_audfrmQ.pop_front();
                    }
                    {
                        //lock_guard<mutex> lk(stream->m_swrfrmQLock);
                        stream->m_swrfrmQ.push_back(dstfrm);
                    }
                    if (srcfrm != dstfrm)
                        av_frame_free(&srcfrm);
                    idleLoop = false;
                }
            }
            else if (stream->m_auddecEof)
                break;

            if (idleLoop)
                this_thread::sleep_for(chrono::milliseconds(5));
        }
        stream->m_swrEof = true;
    }

    void RenderThreadProc()
    {
        while (!m_quitPlay)
        {
            if (!m_isPlaying)
            {
                this_thread::sleep_for(chrono::milliseconds(5));
                continue;
            }

            bool vidIdleRun = true;
            // Audio
            if (HasAudio())
            {
                m_playPos = m_audioMts - m_audioOffset;
                // cout << "--> [1] playPos=" << m_playPos << " =audioMts(" << m_audioMts << ")-audioOffset(" << m_audioOffset << ")" << endl;
            }
            else
            {
                if (m_isAfterSeek)
                {
                    m_playPos = m_seekToMts;
                    // cout << "--> [3] playPos=" << m_playPos << "(=seekToMts)" << endl;
                }
                else
                {
                    auto nowTp = Clock::now();
                    auto playedDur = chrono::duration_cast<Duration>((Clock::now()-m_runStartTp)).count();
                    m_playPos = playedDur+m_posOffset-m_pausedDur;
                    // cout << "--> [2] playPos=" << m_playPos << " =playedDur(" << playedDur << ")+posOffset(" << m_posOffset
                    //         << ")-pausedDur(" << m_pausedDur << ")" << endl;
                }
            }

            // Video
            if (HasVideo() && !m_vidfrmQ.empty())
            {
                if (m_isAfterSeek)
                {
                    if (!HasAudio())
                    {
                        m_runStartTp = Clock::now();
                        m_posOffset = m_seekToMts;
                    }
                    m_isAfterSeek = false;
                }
                AVFrame* vidfrm = m_vidfrmQ.front();
                int64_t mts = av_rescale_q(vidfrm->pts, m_vidStream->time_base, MILLISEC_TIMEBASE);
                if (m_videoStart == -1)
                {
                    m_videoStart = mts;
                }
                mts -= m_audioStart == -1 ? m_videoStart : m_audioStart;
                if (m_playPos >= mts)
                {
                    {
                        lock_guard<mutex> lk(m_vidfrmQLock);
                        m_vidfrmQ.pop_front();
                    }
                    {
                        lock_guard<mutex> lk(m_vidMatLock);
                        m_frmCvt.ConvertImage(vidfrm, m_vidMat, (double)mts/1000);
                    }
                    av_frame_free(&vidfrm);
                    vidIdleRun = false;
                }
            }

            if (vidIdleRun)
                this_thread::sleep_for(chrono::milliseconds(1));
        }
        m_renderEof = true;
    }

    void RenderThreadProc_SeekAsync()
    {
        cout << "Enter RenderThreadProc_SeekAsync()." << endl;
        const int MAX_CACHE_SIZE = 64;
        const int CACHE_SHRINK_SIZE = 48;
        const double MIN_CACHE_FRAME_INTERVAL = 0.5;
        list<ImGui::ImMat> vidMatCache;
        list<ImGui::ImMat>::iterator cacheIter = vidMatCache.begin();
        int64_t prevSeekPos = INT64_MIN;
        while (!m_quitPlay)
        {
            bool idleLoop = true;
            int64_t currSeekPos = m_asyncSeekPos;

            bool cacheUpdated = false;
            double prevCachedTimestamp = numeric_limits<double>::min();
            while (!m_vidfrmQ.empty())
            {
                AVFrame* vidfrm = m_vidfrmQ.front();
                {
                    lock_guard<mutex> lk(m_vidfrmQLock);
                    m_vidfrmQ.pop_front();
                }
                double timestamp = vidfrm->pts*av_q2d(m_vidStream->time_base);

                double checkedTimestamp = prevCachedTimestamp;
                bool skipThisFrame = abs(timestamp-prevCachedTimestamp) < MIN_CACHE_FRAME_INTERVAL;
                if (!skipThisFrame)
                {
                    auto findIter = find_if(vidMatCache.begin(), vidMatCache.end(),
                        [timestamp, MIN_CACHE_FRAME_INTERVAL](const ImGui::ImMat& m) { return abs(m.time_stamp-timestamp) < MIN_CACHE_FRAME_INTERVAL; });
                    if (findIter != vidMatCache.end())
                    {
                        checkedTimestamp = findIter->time_stamp;
                        skipThisFrame = true;
                    }
                }

                if (!skipThisFrame)
                {
                    ImGui::ImMat vidMat;
                    m_frmCvt.ConvertImage(vidfrm, vidMat, timestamp);
                    vidMatCache.push_back(vidMat);
                    prevCachedTimestamp = timestamp;
                    cacheUpdated = true;
                    // cout << "------> Add one cache frame, timestamp = " << MillisecToString((int64_t)(timestamp*1000)) << endl;

                    // shrink the cache if the cache size exceeds the limit
                    if (vidMatCache.size() > MAX_CACHE_SIZE)
                    {
                        double vidSeekTimestamp = (double)currSeekPos/1000;
                        auto iter0 = vidMatCache.begin();
                        auto iter1 = vidMatCache.end();
                        iter1--;
                        while (vidMatCache.size() > CACHE_SHRINK_SIZE)
                        {
                            if (abs(iter0->time_stamp-vidSeekTimestamp) > abs(iter1->time_stamp-vidSeekTimestamp))
                            {
                                iter0++;
                                vidMatCache.pop_front();
                            }
                            else
                            {
                                iter1--;
                                vidMatCache.pop_back();
                            }
                        }
                    }
                }
                // else
                //     cout << "skipThisFrame, timestamp = " << MillisecToString((int64_t)(timestamp*1000))
                //         << ", checkedTimestamp = " << MillisecToString((int64_t)(checkedTimestamp*1000))
                //         << ", diff = " << abs(checkedTimestamp-timestamp) << endl;
                av_frame_free(&vidfrm);
            }
            if (cacheUpdated)
            {
                // cout << "vidMatCache updated." << endl;
                vidMatCache.sort([](const ImGui::ImMat& a, const ImGui::ImMat& b) { return a.time_stamp < b.time_stamp; });
                cacheIter = vidMatCache.begin();
            }
            // else
            //     cout << "No video frame updated." << " currSeekPos=" << MillisecToString(currSeekPos)
            //         << ", prevSeekPos=" << MillisecToString(prevSeekPos) << endl;

            if (currSeekPos != INT64_MIN && (currSeekPos != prevSeekPos || cacheUpdated))
            {
                // cout << "To find frame update m_vidMat" << endl;
                double vidSeekTimestamp = (double)currSeekPos/1000;
                bool farword = true;
                if (cacheIter != vidMatCache.begin())
                {
                    auto temp = cacheIter--;
                    if (abs(cacheIter->time_stamp-vidSeekTimestamp) < abs(temp->time_stamp-vidSeekTimestamp))
                        farword = false;
                    else
                        cacheIter = temp;
                }
                if (farword)
                {
                    while (cacheIter != vidMatCache.end())
                    {
                        auto temp = cacheIter++;
                        if (cacheIter == vidMatCache.end() ||
                            abs(temp->time_stamp-vidSeekTimestamp) < abs(cacheIter->time_stamp-vidSeekTimestamp))
                        {
                            cacheIter = temp;
                            break;
                        }
                    }
                }
                else
                {
                    while (cacheIter != vidMatCache.begin())
                    {
                        auto temp = cacheIter--;
                        if (abs(temp->time_stamp-vidSeekTimestamp) < abs(cacheIter->time_stamp-vidSeekTimestamp))
                        {
                            cacheIter = temp;
                            break;
                        }
                    }
                }
                if (cacheIter != vidMatCache.end())
                {
                    // int64_t vidfrmMts = (int64_t)(cacheIter->time_stamp*1000);
                    // cout << "Found closest frame to seek point " << MillisecToString(currSeekPos) << ", is " << MillisecToString(vidfrmMts)
                    //     << ", the difference is " << (vidfrmMts-currSeekPos) << "ms." << endl;
                    lock_guard<mutex> lk(m_vidMatLock);
                    m_vidMat = *cacheIter;
                }
                // else
                //     cout << "cacheIter is at list.end()" << endl;
                prevSeekPos = currSeekPos;
                idleLoop = false;
            }

            if (idleLoop)
                this_thread::sleep_for(chrono::milliseconds(1));
        }
        cout << "Leave RenderThreadProc_SeekAsync()." << endl;
    }

    void StartAllThreads()
    {
        m_quitPlay = false;
        m_demuxThread = thread(&MediaPlayer_FFImpl::DemuxThreadProc, this);
        if (HasVideo())
            m_viddecThread = thread(&MediaPlayer_FFImpl::VideoDecodeThreadProc, this);
        if (HasAudio())
        {
            for (auto stream : m_audio_streams)
            {
                stream->m_auddecThread = thread(&MediaPlayer_FFImpl::AudioDecodeThreadProc, this, stream);
                stream->m_audswrThread = thread(&MediaPlayer_FFImpl::SwrThreadProc, this, stream);
            }
        }
        m_renderThread = thread(&MediaPlayer_FFImpl::RenderThreadProc, this);
    }

    void StartAllThreads_SeekAsync()
    {
        m_quitPlay = false;
        m_demuxThread = thread(&MediaPlayer_FFImpl::DemuxThreadProc_SeekAsync, this);
        if (HasVideo())
            m_viddecThread = thread(&MediaPlayer_FFImpl::VideoDecodeThreadProc, this);
        else
        {
            for (auto stream : m_audio_streams)
            {
                stream->m_auddecThread = thread(&MediaPlayer_FFImpl::AudioDecodeThreadProc, this, stream);
                stream->m_audswrThread = thread(&MediaPlayer_FFImpl::SwrThreadProc, this, stream);
            }
        }
        m_renderThread = thread(&MediaPlayer_FFImpl::RenderThreadProc_SeekAsync, this);
    }

    void WaitAllThreadsQuit()
    {
        m_quitPlay = true;
        if (m_demuxThread.joinable())
        {
            m_demuxThread.join();
            m_demuxThread = thread();
        }
        if (m_viddecThread.joinable())
        {
            m_viddecThread.join();
            m_viddecThread = thread();
        }
        for (auto stream : m_audio_streams)
        {
            if (stream->m_auddecThread.joinable())
            {
                stream->m_auddecThread.join();
                stream->m_auddecThread = thread();
            }
            if (stream->m_audswrThread.joinable())
            {
                stream->m_audswrThread.join();
                stream->m_audswrThread = thread();
            }
        }
        if (m_renderThread.joinable())
        {
            m_renderThread.join();
            m_renderThread = thread();
        }
        m_isPlaying = false;
    }

    void FlushAllQueues()
    {
        for (AVPacket* avpkt : m_vidpktQ)
            av_packet_free(&avpkt);
        m_vidpktQ.clear();
        for (AVFrame* avfrm : m_vidfrmQ)
            av_frame_free(&avfrm);
        m_vidfrmQ.clear();
        for (auto stream : m_audio_streams)
        {
            for (auto pkt : stream->m_audpktQ) av_packet_free(&pkt);
            stream->m_audpktQ.clear();
            for (auto frame : stream->m_audfrmQ) av_frame_free(&frame);
            stream->m_audfrmQ.clear();
            for (auto frame : stream-> m_swrfrmQ) av_frame_free(&frame);
            stream-> m_swrfrmQ.clear();
        }
    }

    string MillisecToString(int64_t millisec)
    {
        ostringstream oss;
        if (millisec < 0)
        {
            oss << "-";
            millisec = -millisec;
        }
        uint64_t t = (uint64_t) millisec;
        uint32_t milli = (uint32_t)(t%1000); t /= 1000;
        uint32_t sec = (uint32_t)(t%60); t /= 60;
        uint32_t min = (uint32_t)(t%60); t /= 60;
        uint32_t hour = (uint32_t)t;
        oss << setfill('0') << setw(2) << hour << ':'
            << setw(2) << min << ':'
            << setw(2) << sec << '.'
            << setw(3) << milli;
        return oss.str();
    }

    class AudioByteStream : public AudioRender::ByteStream
    {
    public:
        AudioByteStream(MediaPlayer_FFImpl* outterThis) : m_outterThis(outterThis) {}

        uint32_t Read(uint8_t* buff, uint32_t buffSize, bool blocking) override
        {
            uint32_t loadSize = 0;
            if (m_unconsumedAudfrm)
            {
                uint32_t copySize = m_frmPcmDataSize-m_consumedPcmDataSize;
                if (copySize > buffSize)
                    copySize = buffSize;
                memcpy(buff, m_unconsumedAudfrm->data[0]+m_consumedPcmDataSize, copySize);
                loadSize += copySize;
                m_consumedPcmDataSize += copySize;
                if (m_consumedPcmDataSize >= m_frmPcmDataSize)
                {
                    av_frame_free(&m_unconsumedAudfrm);
                    m_unconsumedAudfrm = nullptr;
                    m_frmPcmDataSize = 0;
                    m_consumedPcmDataSize = 0;
                }
            }
            bool tsUpdate = false;
            int64_t audMts;
            bool need_break = false;
            int render_stream_index = m_outterThis->m_render_audio_index != -1 ? m_outterThis->m_render_audio_index : m_outterThis->m_audio_streams[0]->m_index;
            bool mute = m_outterThis->m_render_audio_index == -1;
            while (loadSize < buffSize)
            {
                bool idleLoop = true;
                for (auto stream : m_outterThis->m_audio_streams)
                {
                    AVFrame* audfrm = nullptr;
                    if (stream->m_swrfrmQ.empty())
                    {
                        if (stream->m_auddecEof)
                        {
                            need_break = true;
                            continue;
                        }
                    }
                    else
                    {
                        audfrm = stream->m_swrfrmQ.front();
                        lock_guard<mutex> lk(stream->m_swrfrmQLock);
                        stream->m_swrfrmQ.pop_front();
                    }

                    if (audfrm != nullptr)
                    {
                        if (render_stream_index == stream->m_index)
                        {
                            if (m_frameSize == 0)
                            {
                                uint32_t bytesPerSample = av_get_bytes_per_sample((AVSampleFormat)audfrm->format);
                                m_frameSize = bytesPerSample * stream->m_swrOutChannels;
                            }
                            uint32_t frmPcmDataSize = m_frameSize * audfrm->nb_samples;
                            tsUpdate = true;
                            audMts = av_rescale_q(audfrm->pts, stream->m_stream->time_base, MILLISEC_TIMEBASE);
                            uint32_t copySize = buffSize-loadSize;
                            if (copySize > frmPcmDataSize)
                                copySize = frmPcmDataSize;
                            if (!mute)
                                memcpy(buff+loadSize, audfrm->data[0], copySize);
                            else
                                memset(buff+loadSize, 0, copySize);
                            loadSize += copySize;
                            if (copySize < frmPcmDataSize)
                            {
                                m_unconsumedAudfrm = audfrm;
                                m_frmPcmDataSize = frmPcmDataSize;
                                m_consumedPcmDataSize = copySize;
                            }
                            else
                            {
                                av_frame_free(&audfrm);
                            }
                        }
                        else
                        {
                            av_frame_free(&audfrm);
                        }
                        idleLoop = false;
                    }
                    else if (!blocking)
                    {
                        need_break = true;
                        continue;
                    }
                }
                if (need_break)
                    break;
                if (idleLoop)
                    this_thread::sleep_for(chrono::milliseconds(5));
            }
            if (tsUpdate)
                m_outterThis->m_audioMts = audMts - (m_outterThis->m_audioStart == -1 ? 0 : m_outterThis->m_audioStart);
            return loadSize;
        }

        void Flush() override
        {
            if (m_unconsumedAudfrm)
            {
                av_frame_free(&m_unconsumedAudfrm);
                m_unconsumedAudfrm = nullptr;
            }
            m_frmPcmDataSize = 0;
            m_consumedPcmDataSize = 0;
            m_frameSize = 0;
        }

        bool GetTimestampMs(int64_t& ts) override
        {
            return false;
        }

        void SetFrameSize(uint32_t size)
        {
            m_frameSize = size;
        }

    private:
        MediaPlayer_FFImpl* m_outterThis;
        AVFrame* m_unconsumedAudfrm{nullptr};
        uint32_t m_frmPcmDataSize{0};
        uint32_t m_consumedPcmDataSize{0};
        uint32_t m_frameSize{0};
    };

private:
    string m_errMessage;
    bool m_vidPreferUseHw{true};
    AVHWDeviceType m_vidUseHwType{AV_HWDEVICE_TYPE_NONE};
    int m_playMode{MP_DECODE_VIDEO | MP_DECODE_AUDIO};

    AVFormatContext* m_avfmtCtx{nullptr};
    int m_vidStmIdx{-1};
    //int m_audStmIdx{-1};
    AVStream* m_vidStream{nullptr};
    //AVStream* m_audStream{nullptr};
    AVCodecPtr m_viddec{nullptr};
    //AVCodecPtr m_auddec{nullptr};
    AVCodecContext* m_viddecCtx{nullptr};
    //AVCodecContext* m_auddecCtx{nullptr};
    AVPixelFormat m_vidHwPixFmt{AV_PIX_FMT_NONE};
    AVHWDeviceType m_viddecDevType{AV_HWDEVICE_TYPE_NONE};
    AVBufferRef* m_viddecHwDevCtx{nullptr};

    // demuxing thread
    thread m_demuxThread;
    float m_vidpktQDuration{2.0f};
    int m_vidpktQMaxSize{0};
    list<AVPacket*> m_vidpktQ;
    mutex m_vidpktQLock;
    
    bool m_demuxEof{false};
    // video decoding thread
    thread m_viddecThread;
    int m_vidfrmQMaxSize{4};
    list<AVFrame*> m_vidfrmQ;
    mutex m_vidfrmQLock;
    bool m_viddecEof{false};

    // rendering thread
    thread m_renderThread;
    bool m_renderEof{false};

    recursive_mutex m_ctlLock;
    bool m_quitPlay{false};
    bool m_isPlaying{false};

    bool m_isAfterSeek{false};
    bool m_isSeekToI{false};
    int64_t m_seekToMts{0};

    bool m_isSeeking{false};
    atomic_int64_t m_asyncSeekPos{INT64_MIN};
    bool m_isPlayingBeforeSeek{false};

    int64_t m_playPos{0}, m_posOffset{0};
    int64_t m_pausedDur{0};
    int64_t m_audioMts{0}, m_audioOffset{0};
    int64_t m_audioStart{-1}, m_videoStart{-1};
    TimePoint m_runStartTp{CLOCK_MIN}, m_pauseStartTp{CLOCK_MIN};

    ImGui::ImMat m_vidMat;
    mutex m_vidMatLock;
    AudioRender* m_audrnd{nullptr};
    AudioByteStream m_audByteStream;

    std::vector<media_stream*> m_audio_streams;
    int m_render_audio_index {-1};
    AVFrameToImMatConverter m_frmCvt;
};

constexpr MediaPlayer_FFImpl::TimePoint MediaPlayer_FFImpl::CLOCK_MIN = MediaPlayer_FFImpl::Clock::time_point::min();
const AVRational MediaPlayer_FFImpl::MILLISEC_TIMEBASE = { 1, 1000 };
const AVRational MediaPlayer_FFImpl::FFAV_TIMEBASE = { 1, AV_TIME_BASE };

static AVPixelFormat get_hw_format(AVCodecContext *ctx, const AVPixelFormat *pix_fmts)
{
    MediaPlayer_FFImpl* mp = reinterpret_cast<MediaPlayer_FFImpl*>(ctx->opaque);
    const AVPixelFormat *p;
    for (p = pix_fmts; *p != -1; p++) {
        if (mp->CheckHwPixFmt(*p))
            return *p;
    }
    return ctx->pix_fmt; // if not found HW pix fmt, using software fmt
}

MediaPlayer* CreateMediaPlayer()
{
    return static_cast<MediaPlayer*>(new MediaPlayer_FFImpl());
}

void ReleaseMediaPlayer(MediaPlayer** player)
{
    if (!player || !*player)
        return;
    avdevice_register_all();
    MediaPlayer_FFImpl* ffPlayer = dynamic_cast<MediaPlayer_FFImpl*>(*player);
    ffPlayer->Close();
    delete ffPlayer;
    *player = nullptr;
}
