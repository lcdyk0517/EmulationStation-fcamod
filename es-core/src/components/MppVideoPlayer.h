#pragma once
#ifndef ES_CORE_COMPONENTS_MPP_VIDEO_PLAYER_H
#define ES_CORE_COMPONENTS_MPP_VIDEO_PLAYER_H

#ifdef HAVE_MPP

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

struct VideoContext;
struct AVFormatContext;
struct AVCodecContext;
struct AVBSFContext;
struct AVPacket;
struct AVFrame;
struct SwsContext;
struct SwrContext;

// Hardware H.264 playback for VideoVlcComponent on Rockchip SoCs: libavformat
// demuxes, MPP decodes on the VPU, libswscale converts NV12 to RGBA into the
// same double-buffered VideoContext surfaces VLC writes to. Audio (if asked
// for) is decoded with libavcodec and played on its own SDL audio device.
class MppVideoPlayer
{
public:
	// Returns nullptr when the file can't be played this way (no VPU access,
	// not H.264, open error); the caller then falls back to VLC.
	static MppVideoPlayer* open(const std::string& path);
	static bool isAvailable();

	~MppVideoPlayer();

	int width() const { return mWidth; }
	int height() const { return mHeight; }
	bool hasAudio() const { return mAudioStream >= 0; }

	// Starts the decoder thread. Frames are converted to outWidth x outHeight
	// RGBA into ctx->surfaces. startTime is in seconds.
	void start(VideoContext* ctx, int outWidth, int outHeight, bool withAudio, double startTime);

	// True once the last frame has been shown; restart() plays it again.
	bool ended() const { return mEnded; }
	void restart();

private:
	MppVideoPlayer();

	bool openFile(const std::string& path);
	bool initDecoder();
	bool initAudio();
	void threadProc();
	bool playOnce();
	bool sendVideoPacket(AVPacket* pkt, bool eos);
	bool drainFrames(bool waitForEos);
	void presentFrame(void* frame);
	void decodeAudio(AVPacket* pkt);
	void rewind();

	AVFormatContext*	mFormat;
	AVBSFContext*		mBsf;
	AVCodecContext*		mAudioCodec;
	AVPacket*			mPacket;
	AVFrame*			mAudioFrame;
	SwsContext*			mSws;
	SwrContext*			mSwr;
	int					mVideoStream;
	int					mAudioStream;
	int					mWidth;
	int					mHeight;

	void*				mMpp;		// MppCtx
	void*				mMpi;		// MppApi*
	void*				mFrameGroup;	// MppBufferGroup
	bool				mFrameEos;

	unsigned int		mAudioDevice;	// SDL_AudioDeviceID
	bool				mWithAudio;

	VideoContext*		mContext;
	int					mOutWidth;
	int					mOutHeight;
	double				mStartTime;

	// wall clock (ms, SDL ticks) at which pts 0 is shown; < 0 until the first frame
	long long			mClockStart;
	long long			mFirstPts;

	std::thread			mThread;
	std::mutex			mLock;
	std::condition_variable mWake;
	std::atomic<bool>	mStop;
	std::atomic<bool>	mEnded;
	bool				mRestart;
};

#endif // HAVE_MPP
#endif // ES_CORE_COMPONENTS_MPP_VIDEO_PLAYER_H
