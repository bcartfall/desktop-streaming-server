#include "ScreenLive.h"
#include "net/NetInterface.h"
#include "net/Timestamp.h"
#include "xop/RtspServer.h"
#include "xop/H264Parser.h"
#include "ScreenCapture/DXGIScreenCapture.h"
//#include "ScreenCapture/GDIScreenCapture.h"
#include <versionhelpers.h>

ScreenLive::ScreenLive()
	: event_loop_(new xop::EventLoop)
{
	encoding_fps_ = 0;
	rtsp_clients_.clear();
}

ScreenLive::~ScreenLive()
{
	Destroy();
}

ScreenLive& ScreenLive::Instance()
{
	static ScreenLive s_screen_live;
	return s_screen_live;
}

bool ScreenLive::GetScreenImage(std::vector<uint8_t>& bgra_image, uint32_t& width, uint32_t& height)
{
	if (screen_capture_) {
		if (screen_capture_->CaptureFrame(bgra_image, width, height)) {
			return true;
		}
	}

	return false;
}

std::string ScreenLive::GetStatusInfo()
{
	std::string info;

	if (is_encoder_started_) {
		info += "Encoder: " + av_config_.codec + " \n\n";
		info += "Encoding framerate: " + std::to_string(encoding_fps_) + " \n\n";
	}

	if (rtsp_server_ != nullptr) {
		info += "RTSP Server (connections): " + std::to_string(rtsp_clients_.size()) + " \n\n";
	}

	if (rtsp_pusher_ != nullptr) {
		std::string status = rtsp_pusher_->IsConnected() ? "connected" : "disconnected";
		info += "RTSP Pusher: " + status + " \n\n";
	}

	if (rtmp_pusher_ != nullptr) {
		std::string status = rtmp_pusher_->IsConnected() ? "connected" : "disconnected";
		info += "RTMP Pusher: " + status + " \n\n";
	}

	return info;
}

bool ScreenLive::Init(AVConfig& config)
{
	if (is_initialized_) {
		Destroy();
	}

	if (StartCapture() < 0) {
		return false;
	}

	if (StartEncoder(config) < 0) {
		return false;
	}

	is_initialized_ = true;
	return true;
}

void ScreenLive::Destroy()
{
	{
		std::lock_guard<std::mutex> locker(mutex_);

		if (rtsp_pusher_ != nullptr && rtsp_pusher_->IsConnected()) {
			rtsp_pusher_->Close();
			rtsp_pusher_ = nullptr;
		}

		if (rtmp_pusher_ != nullptr && rtmp_pusher_->IsConnected()) {
			rtmp_pusher_->Close();
			rtmp_pusher_ = nullptr;
		}

		if (rtsp_server_ != nullptr) {
			rtsp_server_->RemoveSession(media_session_id_);
			rtsp_server_ = nullptr;
		}
	}

	StopEncoder();
	StopCapture();
	is_initialized_ = false;
}

bool ScreenLive::StartLive(int type, LiveConfig& config)
{
	if (!is_encoder_started_) {
		return false;
	}

	// audio disabled
	//uint32_t samplerate = audio_capture_.GetSamplerate();
	//uint32_t channels = audio_capture_.GetChannels();

	if (type == SCREEN_LIVE_RTSP_SERVER) {
		auto rtsp_server = xop::RtspServer::Create(event_loop_.get());
		xop::MediaSessionId session_id = 0;

		if (config.ip == "127.0.0.1") {
			config.ip = "0.0.0.0";
		}

		if (!rtsp_server->Start(config.ip, config.port)) {
			return false;
		}

		xop::MediaSession* session = xop::MediaSession::CreateNew(config.suffix);
		session->AddSource(xop::channel_0, xop::H264Source::CreateNew());
		//session->AddSource(xop::channel_1, xop::AACSource::CreateNew(samplerate, channels, false)); // audio disabled
		session->AddNotifyConnectedCallback([this](xop::MediaSessionId sessionId, std::string peer_ip, uint16_t peer_port) {
			this->rtsp_clients_.emplace(peer_ip + ":" + std::to_string(peer_port));
			printf("RTSP client: %u\n", this->rtsp_clients_.size());
			});
		session->AddNotifyDisconnectedCallback([this](xop::MediaSessionId sessionId, std::string peer_ip, uint16_t peer_port) {
			this->rtsp_clients_.erase(peer_ip + ":" + std::to_string(peer_port));
			printf("RTSP client: %u\n", this->rtsp_clients_.size());
			});


		session_id = rtsp_server->AddSession(session);
		//printf("RTSP Server: rtsp://%s:%hu/%s \n", xop::NetInterface::GetLocalIPAddress().c_str(), config.port, config.suffix.c_str());
		printf("RTSP Server start: rtsp://%s:%hu/%s \n", config.ip.c_str(), config.port, config.suffix.c_str());

		std::lock_guard<std::mutex> locker(mutex_);
		rtsp_server_ = rtsp_server;
		media_session_id_ = session_id;
	}
	else {
		return false;
	}

	return true;
}

void ScreenLive::StopLive(int type)
{
	std::lock_guard<std::mutex> locker(mutex_);

	switch (type)
	{
	case SCREEN_LIVE_RTSP_SERVER:
		if (rtsp_server_ != nullptr) {
			rtsp_server_->Stop();
			rtsp_server_ = nullptr;
			rtsp_clients_.clear();
			printf("RTSP Server stop. \n");
		}

		break;

	default:
		break;
	}
}

bool ScreenLive::IsConnected(int type)
{
	std::lock_guard<std::mutex> locker(mutex_);

	bool is_connected = false;
	switch (type)
	{
	case SCREEN_LIVE_RTSP_SERVER:
		if (rtsp_server_ != nullptr) {
			is_connected = rtsp_clients_.size() > 0;
		}
		break;

	case SCREEN_LIVE_RTSP_PUSHER:
		if (rtsp_pusher_ != nullptr) {
			is_connected = rtsp_pusher_->IsConnected();
		}
		break;

	case SCREEN_LIVE_RTMP_PUSHER:
		if (rtmp_pusher_ != nullptr) {
			is_connected = rtmp_pusher_->IsConnected();
		}
		break;

	default:
		break;
	}

	return is_connected;
}

int ScreenLive::StartCapture()
{
	std::vector<DX::Monitor> monitors = DX::GetMonitors();
	if (monitors.empty()) {
		printf("Monitor not found. \n");
		return -1;
	}

	for (size_t index = 0; index < monitors.size(); index++) {
		printf("Monitor(%u) info: %dx%d \n", index,
			monitors[index].right - monitors[index].left,
			monitors[index].bottom - monitors[index].top);
	}

	int display_index = 0; // monitor index

	if (!screen_capture_) {
		if (IsWindows8OrGreater()) {
			printf("DXGI Screen capture start, monitor index: %d \n", display_index);
			screen_capture_ = new DXGIScreenCapture();
			if (!screen_capture_->Init(display_index)) {
				printf("DXGI Screen capture start failed, monitor index: %d \n", display_index);
				delete screen_capture_;

				return -1;
			}
		}
		else {
			printf("GDI disabled");
			//printf("GDI Screen capture start, monitor index: %d \n", display_index);
			//screen_capture_ = new GDIScreenCapture();
		}

		if (!screen_capture_->Init(display_index)) {
			printf("Screen capture start failed, monitor index: %d \n", display_index);
			delete screen_capture_;
			screen_capture_ = nullptr;
			return -1;
		}
	}

	//if (!audio_capture_.Init()) {
	//	return -1;
	//}

	is_capture_started_ = true;
	return 0;
}

int ScreenLive::StopCapture()
{
	if (is_capture_started_) {
		if (screen_capture_) {
			screen_capture_->Destroy();
			delete screen_capture_;
			screen_capture_ = nullptr;
		}
		//audio_capture_.Destroy();
		is_capture_started_ = false;
	}

	return 0;
}

int ScreenLive::StartEncoder(AVConfig& config)
{
	if (!is_capture_started_) {
		return -1;
	}

	av_config_ = config;

	ffmpeg::AVConfig encoder_config;
	encoder_config.video.framerate = av_config_.framerate;
	encoder_config.video.bitrate = av_config_.bitrate_bps;
	encoder_config.video.gop = av_config_.framerate;
	encoder_config.video.format = AV_PIX_FMT_BGRA;
	encoder_config.video.width = screen_capture_->GetWidth();
	encoder_config.video.height = screen_capture_->GetHeight();

	h264_encoder_.SetCodec(config.codec);

	if (!h264_encoder_.Init(av_config_.framerate, av_config_.bitrate_bps / 1000,
		AV_PIX_FMT_BGRA, screen_capture_->GetWidth(),
		screen_capture_->GetHeight())) {
		return -1;
	}

	/*
	int samplerate = audio_capture_.GetSamplerate();
	int channels = audio_capture_.GetChannels();
	if (!aac_encoder_.Init(samplerate, channels, AV_SAMPLE_FMT_S16, 64)) {
		return -1;
	}
	*/

	is_encoder_started_ = true;
	encode_video_thread_.reset(new std::thread(&ScreenLive::EncodeVideo, this));
	//encode_audio_thread_.reset(new std::thread(&ScreenLive::EncodeAudio, this));
	return 0;
}

int ScreenLive::StopEncoder()
{
	if (is_encoder_started_) {
		is_encoder_started_ = false;

		if (encode_video_thread_) {
			encode_video_thread_->join();
			encode_video_thread_ = nullptr;
		}

		if (encode_audio_thread_) {
			encode_audio_thread_->join();
			encode_audio_thread_ = nullptr;
		}

		h264_encoder_.Destroy();
		//aac_encoder_.Destroy();
	}

	return 0;
}

bool ScreenLive::IsKeyFrame(const uint8_t* data, uint32_t size)
{
	if (size > 4) {
		//0x67:sps ,0x65:IDR, 0x6: SEI
		if (data[4] == 0x67 || data[4] == 0x65 ||
			data[4] == 0x6 || data[4] == 0x27) {
			return true;
		}
	}

	return false;
}

void ScreenLive::EncodeVideo()
{
	static xop::Timestamp encoding_ts, update_ts;
	uint32_t encoding_fps = 0;
	uint32_t msec = 1000 / av_config_.framerate;


	while (is_encoder_started_ && is_capture_started_) {
		if (update_ts.Elapsed() >= 1000) {
			update_ts.Reset();
			encoding_fps_ = encoding_fps;
			encoding_fps = 0;
		}

		uint32_t delay = msec;
		uint32_t elapsed = (uint32_t)encoding_ts.Elapsed();
		if (elapsed > delay) {
			delay = 0;
		}
		else {
			delay -= elapsed;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(delay));
		encoding_ts.Reset();

		std::vector<uint8_t> bgra_image;
		uint32_t timestamp = xop::H264Source::GetTimestamp();
		int frame_size = 0;
		uint32_t width = 0, height = 0;

		if (screen_capture_->CaptureFrame(bgra_image, width, height)) {
			std::vector<uint8_t> out_frame;
			if (h264_encoder_.Encode(&bgra_image[0], width, height, bgra_image.size(), out_frame) > 0) {
				if (out_frame.size() > 0) {
					encoding_fps += 1;
					PushVideo(&out_frame[0], out_frame.size(), timestamp);
				}
			}
		}
	}

	encoding_fps_ = 0;
}

void ScreenLive::EncodeAudio()
{
	// audio disabled
}

void ScreenLive::PushVideo(const uint8_t* data, uint32_t size, uint32_t timestamp)
{
	xop::AVFrame video_frame(size);
	video_frame.size = size - 4;
	video_frame.type = IsKeyFrame(data, size) ? xop::VIDEO_FRAME_I : xop::VIDEO_FRAME_P;
	video_frame.timestamp = timestamp;
	memcpy(video_frame.buffer.get(), data + 4, size - 4);

	if (size > 0) {
		std::lock_guard<std::mutex> locker(mutex_);

		/* RTSP服务器 */
		if (rtsp_server_ != nullptr && this->rtsp_clients_.size() > 0) {
			rtsp_server_->PushFrame(media_session_id_, xop::channel_0, video_frame);
		}

		/* RTSP推流 */
		if (rtsp_pusher_ != nullptr && rtsp_pusher_->IsConnected()) {
			rtsp_pusher_->PushFrame(xop::channel_0, video_frame);
		}

		/* RTMP推流 */
		if (rtmp_pusher_ != nullptr && rtmp_pusher_->IsConnected()) {
			rtmp_pusher_->PushVideoFrame(video_frame.buffer.get(), video_frame.size);
		}
	}
}

void ScreenLive::PushAudio(const uint8_t* data, uint32_t size, uint32_t timestamp)
{
	// audio disabled
}