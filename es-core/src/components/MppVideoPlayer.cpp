#ifdef HAVE_MPP

#include "components/MppVideoPlayer.h"
#include "components/VideoVlcComponent.h"
#include "Log.h"

#include <chrono>
#include <vector>
#include <unistd.h>
#include <dlfcn.h>
#include <SDL.h>

extern "C" {
#include <libavutil/macros.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#define MODULE_TAG "es_video"	// tags MPP buffer allocations
#include <rockchip/rk_mpi.h>


// ffmpeg and MPP are loaded with dlopen on the first video instead of being
// linked: linking pulled ~90 extra shared libraries (all of libavcodec's codec
// dependencies) into every ES start, and ES must still run where they are missing.
#define MPP_VIDEO_SYMBOLS(X) \
	X(AVFORMAT, avformat_open_input) \
	X(AVFORMAT, avformat_find_stream_info) \
	X(AVFORMAT, av_find_best_stream) \
	X(AVFORMAT, av_seek_frame) \
	X(AVFORMAT, av_read_frame) \
	X(AVFORMAT, avformat_close_input) \
	X(AVCODEC, av_bsf_get_by_name) \
	X(AVCODEC, av_bsf_alloc) \
	X(AVCODEC, av_bsf_init) \
	X(AVCODEC, av_bsf_send_packet) \
	X(AVCODEC, av_bsf_receive_packet) \
	X(AVCODEC, av_bsf_flush) \
	X(AVCODEC, av_bsf_free) \
	X(AVCODEC, avcodec_parameters_copy) \
	X(AVCODEC, avcodec_find_decoder) \
	X(AVCODEC, avcodec_alloc_context3) \
	X(AVCODEC, avcodec_parameters_to_context) \
	X(AVCODEC, avcodec_open2) \
	X(AVCODEC, avcodec_send_packet) \
	X(AVCODEC, avcodec_receive_frame) \
	X(AVCODEC, avcodec_flush_buffers) \
	X(AVCODEC, avcodec_free_context) \
	X(AVCODEC, av_packet_alloc) \
	X(AVCODEC, av_packet_unref) \
	X(AVCODEC, av_packet_free) \
	X(AVUTIL, av_frame_alloc) \
	X(AVUTIL, av_frame_unref) \
	X(AVUTIL, av_frame_free) \
	X(AVUTIL, av_rescale_q) \
	X(SWSCALE, sws_getCachedContext) \
	X(SWSCALE, sws_scale) \
	X(SWSCALE, sws_freeContext) \
	X(SWRESAMPLE, swr_alloc_set_opts2) \
	X(SWRESAMPLE, swr_init) \
	X(SWRESAMPLE, swr_get_out_samples) \
	X(SWRESAMPLE, swr_convert) \
	X(SWRESAMPLE, swr_free) \
	X(MPP, mpp_create) \
	X(MPP, mpp_init) \
	X(MPP, mpp_destroy) \
	X(MPP, mpp_packet_init) \
	X(MPP, mpp_packet_set_pts) \
	X(MPP, mpp_packet_set_eos) \
	X(MPP, mpp_packet_deinit) \
	X(MPP, mpp_frame_get_info_change) \
	X(MPP, mpp_frame_get_buf_size) \
	X(MPP, mpp_frame_get_errinfo) \
	X(MPP, mpp_frame_get_discard) \
	X(MPP, mpp_frame_get_buffer) \
	X(MPP, mpp_frame_get_eos) \
	X(MPP, mpp_frame_deinit) \
	X(MPP, mpp_frame_get_pts) \
	X(MPP, mpp_frame_get_width) \
	X(MPP, mpp_frame_get_height) \
	X(MPP, mpp_frame_get_hor_stride) \
	X(MPP, mpp_frame_get_ver_stride) \
	X(MPP, mpp_buffer_group_get) \
	X(MPP, mpp_buffer_group_put) \
	X(MPP, mpp_buffer_group_limit_config) \
	X(MPP, mpp_buffer_get_ptr_with_caller) \
	X(MPP, mpp_buffer_sync_begin_f) \
	X(MPP, mpp_buffer_sync_end_f)

enum VideoLib { AVFORMAT, AVCODEC, AVUTIL, SWSCALE, SWRESAMPLE, MPP, VIDEO_LIB_COUNT };

static const char* const sLibNames[VIDEO_LIB_COUNT] = {
	"libavformat.so." AV_STRINGIFY(LIBAVFORMAT_VERSION_MAJOR),
	"libavcodec.so." AV_STRINGIFY(LIBAVCODEC_VERSION_MAJOR),
	"libavutil.so." AV_STRINGIFY(LIBAVUTIL_VERSION_MAJOR),
	"libswscale.so." AV_STRINGIFY(LIBSWSCALE_VERSION_MAJOR),
	"libswresample.so." AV_STRINGIFY(LIBSWRESAMPLE_VERSION_MAJOR),
	"librockchip_mpp.so.1",
};

static struct
{
#define X(lib, name) decltype(&::name) name;
	MPP_VIDEO_SYMBOLS(X)
#undef X
} sLib;

#define LIB(name) sLib.name

// Loads everything once; false (and VLC playback) if anything is missing.
static bool loadVideoLibs()
{
	static int state = -1; // -1 not tried, 0 failed, 1 loaded
	if (state >= 0)
		return state == 1;

	state = 0;
	void* handles[VIDEO_LIB_COUNT];
	for (int i = 0; i < VIDEO_LIB_COUNT; i++)
	{
		handles[i] = dlopen(sLibNames[i], RTLD_NOW | RTLD_LOCAL);
		if (!handles[i] && i == MPP)
			handles[i] = dlopen("librockchip_mpp.so.0", RTLD_NOW | RTLD_LOCAL);
		if (!handles[i])
		{
			LOG(LogInfo) << "MppVideoPlayer: " << sLibNames[i] << " not available, using VLC";
			return false;
		}
	}

#define X(lib, name) \
	sLib.name = (decltype(sLib.name))dlsym(handles[lib], #name); \
	if (!sLib.name) { LOG(LogWarning) << "MppVideoPlayer: missing symbol " #name ", using VLC"; return false; }
	MPP_VIDEO_SYMBOLS(X)
#undef X

	state = 1;
	return true;
}

#define MPP_DEVICE		"/dev/mpp_service"
#define FRAME_BUFFERS	20		// H.264 DPB (16) + frames in flight
#define LATE_DROP_MS	100		// frames later than this are skipped instead of shown

static long long nowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool MppVideoPlayer::isAvailable()
{
	return access(MPP_DEVICE, R_OK | W_OK) == 0 && loadVideoLibs();
}

MppVideoPlayer* MppVideoPlayer::open(const std::string& path)
{
	if (!isAvailable())
		return nullptr;

	MppVideoPlayer* player = new MppVideoPlayer();
	if (!player->openFile(path) || !player->initDecoder())
	{
		delete player;
		return nullptr;
	}

	return player;
}

MppVideoPlayer::MppVideoPlayer() :
	mFormat(nullptr), mBsf(nullptr), mAudioCodec(nullptr), mPacket(nullptr), mAudioFrame(nullptr),
	mSws(nullptr), mSwr(nullptr), mVideoStream(-1), mAudioStream(-1), mWidth(0), mHeight(0),
	mMpp(nullptr), mMpi(nullptr), mFrameGroup(nullptr), mFrameEos(false),
	mAudioDevice(0), mWithAudio(false), mContext(nullptr), mOutWidth(0), mOutHeight(0), mStartTime(0),
	mClockStart(-1), mFirstPts(0), mStop(false), mEnded(false), mRestart(false)
{
}

MppVideoPlayer::~MppVideoPlayer()
{
	{
		std::unique_lock<std::mutex> lock(mLock);
		mStop = true;
	}
	mWake.notify_all();

	if (mThread.joinable())
		mThread.join();

	if (mAudioDevice)
		SDL_CloseAudioDevice(mAudioDevice);

	if (mMpp)
		LIB(mpp_destroy)((MppCtx)mMpp);

	if (mFrameGroup)
		LIB(mpp_buffer_group_put)((MppBufferGroup)mFrameGroup);

	LIB(sws_freeContext)(mSws);
	LIB(swr_free)(&mSwr);
	LIB(av_frame_free)(&mAudioFrame);
	LIB(av_packet_free)(&mPacket);
	LIB(avcodec_free_context)(&mAudioCodec);
	LIB(av_bsf_free)(&mBsf);
	LIB(avformat_close_input)(&mFormat);
}

bool MppVideoPlayer::openFile(const std::string& path)
{
	if (LIB(avformat_open_input)(&mFormat, path.c_str(), nullptr, nullptr) < 0)
		return false;

	if (LIB(avformat_find_stream_info)(mFormat, nullptr) < 0)
		return false;

	mVideoStream = LIB(av_find_best_stream)(mFormat, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	if (mVideoStream < 0)
		return false;

	AVCodecParameters* par = mFormat->streams[mVideoStream]->codecpar;
	if (par->codec_id != AV_CODEC_ID_H264 || par->width <= 0 || par->height <= 0)
		return false;

	mWidth = par->width;
	mHeight = par->height;
	mAudioStream = LIB(av_find_best_stream)(mFormat, AVMEDIA_TYPE_AUDIO, -1, mVideoStream, nullptr, 0);

	// MPP wants Annex-B start codes; MP4 stores length-prefixed NAL units
	const AVBitStreamFilter* filter = LIB(av_bsf_get_by_name)("h264_mp4toannexb");
	if (!filter || LIB(av_bsf_alloc)(filter, &mBsf) < 0)
		return false;

	LIB(avcodec_parameters_copy)(mBsf->par_in, par);
	mBsf->time_base_in = mFormat->streams[mVideoStream]->time_base;
	if (LIB(av_bsf_init)(mBsf) < 0)
		return false;

	mPacket = LIB(av_packet_alloc)();
	return mPacket != nullptr;
}

bool MppVideoPlayer::initDecoder()
{
	MppCtx ctx = nullptr;
	MppApi* mpi = nullptr;

	if (LIB(mpp_create)(&ctx, &mpi) != MPP_OK)
		return false;

	mMpp = ctx;
	mMpi = mpi;

	// packets are whole access units from the demuxer, no parsing needed
	RK_U32 split = 0;
	mpi->control(ctx, MPP_DEC_SET_PARSER_SPLIT_MODE, &split);

	if (LIB(mpp_init)(ctx, MPP_CTX_DEC, MPP_VIDEO_CodingAVC) != MPP_OK)
	{
		LOG(LogWarning) << "MppVideoPlayer: mpp_init failed, falling back to VLC";
		return false;
	}

	return true;
}

bool MppVideoPlayer::initAudio()
{
	AVCodecParameters* par = mFormat->streams[mAudioStream]->codecpar;
	const AVCodec* codec = LIB(avcodec_find_decoder)(par->codec_id);
	if (!codec)
		return false;

	mAudioCodec = LIB(avcodec_alloc_context3)(codec);
	if (!mAudioCodec || LIB(avcodec_parameters_to_context)(mAudioCodec, par) < 0 || LIB(avcodec_open2)(mAudioCodec, codec, nullptr) < 0)
		return false;

	SDL_AudioSpec want, have;
	SDL_zero(want);
	want.freq = mAudioCodec->sample_rate > 0 ? mAudioCodec->sample_rate : 44100;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 1024;

	mAudioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
	if (!mAudioDevice)
	{
		LOG(LogWarning) << "MppVideoPlayer: no audio device: " << SDL_GetError();
		return false;
	}

	AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
	if (LIB(swr_alloc_set_opts2)(&mSwr, &stereo, AV_SAMPLE_FMT_S16, have.freq,
		&mAudioCodec->ch_layout, mAudioCodec->sample_fmt, mAudioCodec->sample_rate, 0, nullptr) < 0 || LIB(swr_init)(mSwr) < 0)
		return false;

	mAudioFrame = LIB(av_frame_alloc)();
	return mAudioFrame != nullptr;
}

void MppVideoPlayer::start(VideoContext* ctx, int outWidth, int outHeight, bool withAudio, double startTime)
{
	mContext = ctx;
	mOutWidth = outWidth;
	mOutHeight = outHeight;
	mStartTime = startTime;
	mWithAudio = withAudio && hasAudio() && initAudio();

	mThread = std::thread(&MppVideoPlayer::threadProc, this);
}

void MppVideoPlayer::restart()
{
	{
		std::unique_lock<std::mutex> lock(mLock);
		mRestart = true;
	}
	mWake.notify_all();
}

void MppVideoPlayer::threadProc()
{
	while (!mStop)
	{
		playOnce();

		std::unique_lock<std::mutex> lock(mLock);
		if (mStop)
			break;

		mEnded = true;
		mWake.wait(lock, [this] { return mStop || mRestart; });
		if (mStop)
			break;

		mRestart = false;
		mEnded = false;
		mStartTime = 0;
		lock.unlock();

		rewind();
	}
}

bool MppVideoPlayer::playOnce()
{
	if (mStartTime > 0)
		LIB(av_seek_frame)(mFormat, -1, (int64_t)(mStartTime * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);

	AVPacket* out = LIB(av_packet_alloc)();

	while (!mStop && LIB(av_read_frame)(mFormat, mPacket) >= 0)
	{
		if (mPacket->stream_index == mVideoStream)
		{
			if (LIB(av_bsf_send_packet)(mBsf, mPacket) == 0)
			{
				while (!mStop && LIB(av_bsf_receive_packet)(mBsf, out) == 0)
				{
					sendVideoPacket(out, false);
					LIB(av_packet_unref)(out);
				}
			}
		}
		else if (mWithAudio && mPacket->stream_index == mAudioStream)
			decodeAudio(mPacket);

		LIB(av_packet_unref)(mPacket);
	}

	LIB(av_packet_free)(&out);

	if (mStop)
		return false;

	// flush: everything still queued in the decoder is shown before the end
	sendVideoPacket(nullptr, true);
	drainFrames(true);
	return true;
}

bool MppVideoPlayer::sendVideoPacket(AVPacket* pkt, bool eos)
{
	MppCtx ctx = (MppCtx)mMpp;
	MppApi* mpi = (MppApi*)mMpi;

	MppPacket packet = nullptr;
	if (pkt)
	{
		LIB(mpp_packet_init)(&packet, pkt->data, pkt->size);
		AVRational tb = mFormat->streams[mVideoStream]->time_base;
		int64_t pts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
		LIB(mpp_packet_set_pts)(packet, pts != AV_NOPTS_VALUE ? LIB(av_rescale_q)(pts, tb, AVRational{ 1, 1000000 }) : 0);
	}
	else
		LIB(mpp_packet_init)(&packet, nullptr, 0);

	if (eos)
		LIB(mpp_packet_set_eos)(packet);

	// plain memory packets are copied by MPP, so this buffer can be freed afterwards
	bool sent = false;
	while (!mStop)
	{
		if (mpi->decode_put_packet(ctx, packet) == MPP_OK)
		{
			sent = true;
			break;
		}

		// input queue full: take frames out, then retry
		drainFrames(false);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	LIB(mpp_packet_deinit)(&packet);
	drainFrames(false);
	return sent;
}

bool MppVideoPlayer::drainFrames(bool waitForEos)
{
	MppCtx ctx = (MppCtx)mMpp;
	MppApi* mpi = (MppApi*)mMpi;
	long long deadline = nowMs() + 2000;

	while (!mStop)
	{
		MppFrame frame = nullptr;
		MPP_RET ret = mpi->decode_get_frame(ctx, &frame);

		if (ret != MPP_OK || !frame)
		{
			if (waitForEos && !mFrameEos && nowMs() < deadline)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
				continue;
			}
			return mFrameEos;
		}

		if (LIB(mpp_frame_get_info_change)(frame))
		{
			// first frame (or a resolution change): give the decoder its frame buffers
			if (!mFrameGroup)
			{
				MppBufferGroup group = nullptr;
				// cachable: the NV12 -> RGBA conversion reads every pixel on the CPU
				if (LIB(mpp_buffer_group_get)(&group, (MppBufferType)(MPP_BUFFER_TYPE_ION | MPP_BUFFER_FLAGS_CACHABLE), MPP_BUFFER_INTERNAL, MODULE_TAG, __FUNCTION__) != MPP_OK &&
					LIB(mpp_buffer_group_get)(&group, MPP_BUFFER_TYPE_ION, MPP_BUFFER_INTERNAL, MODULE_TAG, __FUNCTION__) != MPP_OK)
				{
					LOG(LogError) << "MppVideoPlayer: no frame buffer group";
					LIB(mpp_frame_deinit)(&frame);
					mStop = true;
					return false;
				}
				mFrameGroup = group;
			}

			LIB(mpp_buffer_group_limit_config)((MppBufferGroup)mFrameGroup, LIB(mpp_frame_get_buf_size)(frame), FRAME_BUFFERS);
			mpi->control(ctx, MPP_DEC_SET_EXT_BUF_GROUP, mFrameGroup);
			mpi->control(ctx, MPP_DEC_SET_INFO_CHANGE_READY, nullptr);
		}
		else if (!LIB(mpp_frame_get_errinfo)(frame) && !LIB(mpp_frame_get_discard)(frame) && LIB(mpp_frame_get_buffer)(frame))
			presentFrame(frame);

		if (LIB(mpp_frame_get_eos)(frame))
			mFrameEos = true;

		LIB(mpp_frame_deinit)(&frame);
	}

	return mFrameEos;
}

void MppVideoPlayer::presentFrame(void* f)
{
	MppFrame frame = (MppFrame)f;
	long long pts = LIB(mpp_frame_get_pts)(frame) / 1000; // ms

	if (mClockStart < 0)
	{
		mClockStart = nowMs();
		mFirstPts = pts;
		if (mAudioDevice)
			SDL_PauseAudioDevice(mAudioDevice, 0);
	}

	long long target = mClockStart + (pts - mFirstPts);
	long long now = nowMs();

	if (now > target + LATE_DROP_MS)
		return;

	if (target > now)
	{
		std::unique_lock<std::mutex> lock(mLock);
		mWake.wait_for(lock, std::chrono::milliseconds(target - now), [this] { return mStop.load(); });
		if (mStop)
			return;
	}

	int width = LIB(mpp_frame_get_width)(frame);
	int height = LIB(mpp_frame_get_height)(frame);
	int hstride = LIB(mpp_frame_get_hor_stride)(frame);
	int vstride = LIB(mpp_frame_get_ver_stride)(frame);
	MppBuffer buffer = LIB(mpp_frame_get_buffer)(frame);
	unsigned char* base = (unsigned char*)LIB(mpp_buffer_get_ptr_with_caller)(buffer, __FUNCTION__);
	if (!base)
		return;

	mSws = LIB(sws_getCachedContext)(mSws, width, height, AV_PIX_FMT_NV12, mOutWidth, mOutHeight, AV_PIX_FMT_RGBA,
		(width == mOutWidth && height == mOutHeight) ? SWS_POINT : SWS_BILINEAR, nullptr, nullptr, nullptr);
	if (!mSws)
		return;

	const uint8_t* src[2] = { base, base + (size_t)hstride * vstride };
	const int srcStride[2] = { hstride, hstride };

	// same double-buffer protocol as the VLC lock/unlock callbacks
	VideoContext* c = mContext;
	int target_surface = c->surfaceId ^ 1;
	c->mutexes[target_surface].lock();
	c->hasFrame[target_surface] = false;

	uint8_t* dst[1] = { c->surfaces[target_surface] };
	const int dstStride[1] = { mOutWidth * 4 };

	LIB(mpp_buffer_sync_begin_f)(buffer, 0, __FUNCTION__);
	LIB(sws_scale)(mSws, src, srcStride, 0, height, dst, dstStride);
	LIB(mpp_buffer_sync_end_f)(buffer, 0, __FUNCTION__);

	c->surfaceId = target_surface;
	c->hasFrame[target_surface] = true;
	c->mutexes[target_surface].unlock();

	if (c->valid && c->component != nullptr && !c->component->isPlaying() && c->component->isWaitingForVideoToStart())
		c->component->onVideoStarted();
}

void MppVideoPlayer::decodeAudio(AVPacket* pkt)
{
	if (!mAudioCodec || LIB(avcodec_send_packet)(mAudioCodec, pkt) < 0)
		return;

	while (LIB(avcodec_receive_frame)(mAudioCodec, mAudioFrame) == 0)
	{
		int outSamples = LIB(swr_get_out_samples)(mSwr, mAudioFrame->nb_samples);
		if (outSamples > 0)
		{
			std::vector<uint8_t> buf((size_t)outSamples * 4);
			uint8_t* out[1] = { buf.data() };
			int n = LIB(swr_convert)(mSwr, out, outSamples, (const uint8_t**)mAudioFrame->extended_data, mAudioFrame->nb_samples);
			if (n > 0)
				SDL_QueueAudio(mAudioDevice, buf.data(), (Uint32)n * 4);
		}
		LIB(av_frame_unref)(mAudioFrame);
	}
}

void MppVideoPlayer::rewind()
{
	LIB(av_seek_frame)(mFormat, mVideoStream, 0, AVSEEK_FLAG_BACKWARD);
	LIB(av_bsf_flush)(mBsf);
	((MppApi*)mMpi)->reset((MppCtx)mMpp);

	mFrameEos = false;
	mClockStart = -1;

	if (mAudioDevice)
	{
		SDL_PauseAudioDevice(mAudioDevice, 1);
		SDL_ClearQueuedAudio(mAudioDevice);
	}
	if (mAudioCodec)
		LIB(avcodec_flush_buffers)(mAudioCodec);
	if (mSwr)
		LIB(swr_init)(mSwr);
}

#endif // HAVE_MPP
