#pragma once
#include <cstdint>
#include <string>
#include <immat.h>
#include <imgui_helper.h>
#include "AudioRender.hpp"
#include "MediaInfo.h"

#define MP_DECODE_VIDEO     0x00000001
#define MP_DECODE_AUDIO     0x00000002
#define MP_DECODE_AUDIO_ALL 0x00000004

struct MediaPlayer
{
    virtual bool SetAudioRender(AudioRender* audrnd) = 0;
    virtual bool Open(const std::string& url) = 0;
    virtual bool Close() = 0;
    virtual bool Play() = 0;
    virtual bool Pause() = 0;
    virtual bool Reset() = 0;
    virtual bool Seek(int64_t pos, bool seekToI = false) = 0;
    virtual bool SeekAsync(int64_t pos) = 0;
    virtual bool QuitSeekAsync() = 0;

    virtual bool IsOpened() const = 0;
    virtual bool IsPlaying() const = 0;
    virtual bool IsSeeking() const = 0;
    virtual bool IsEof() const = 0;

    virtual MediaInfo::InfoHolder GetMediaInfo() const = 0;
    virtual bool HasVideo() const = 0;
    virtual bool HasAudio() const = 0;
    virtual float GetPlaySpeed() const = 0;
    virtual bool SetPlaySpeed(float speed) = 0;
    virtual bool SetPreferHwDecoder(bool prefer) = 0;
    virtual uint64_t GetDuration() const = 0;
    virtual int64_t GetPlayPos() const = 0; 
    virtual void GetVideo(ImGui::ImMat& out) = 0;

    virtual int GetAudioChannels() = 0;
    virtual int GetAudioStreams() = 0;
    virtual int GetAudioStreamIndex(std::vector<int>& list) = 0;
    virtual int GetAudioMeterStack(int channel) = 0;
    virtual int GetAudioMeterCount(int channel) = 0;
    virtual void SetAudioMeterStack(int channel, int stack) = 0;
    virtual void SetAudioMeterCount(int channel, int count) = 0;
    virtual float GetAudioMeterValue(int channel, double pts, ImGui::ImMat& mat) = 0;
    virtual bool GetAudioRendering(int channel) = 0;
    virtual bool GetAudioStreamRendering(int index) = 0;
    virtual int GetRenderingAudioIndex() const = 0;
    virtual void SetRenderingAudioIndex(int index) = 0;
    virtual bool SetPlayMode(int mode) = 0;

    virtual std::string GetError() const = 0;
};

MediaPlayer* CreateMediaPlayer();
void ReleaseMediaPlayer(MediaPlayer** player);
