/*
 *   _   _       _         __  __          _ _         _    _      _
 *  | | | |_   _| |_ ____ |  \/  | ___  __| (_) __ _  | |  | |_ __| |
 *  | |_| | | | | __|_  / | |\/| |/ _ \/ _` | |/ _` | | |  | __/ _` |
 *  |  _  | |_| | |_ / /  | |  | |  __/ (_| | | (_| | | |__| || (_| |
 *  |_| |_|\__,_|\__/___| |_|  |_|\___|\__,_|_|\__,_| |_____\__\__,_|
 *
 * Copyright 2022 Hutz Media Ltd.
 * Copyright 2018 PHZ
 *
 * See LICENSE
 */

#include "ScreenLive.h"
#include <iostream>


int main(int argc, char* argv[])
{
	// prepare video encoder with default values
	AVConfig avconfig;
	avconfig.bitrate_bps = 8000000; // video bitrate
	avconfig.framerate = 25;        // video framerate
	avconfig.codec = "h264_nvenc";  // hardware encoder: "h264_nvenc"; // h264, h264_nvenc, or h264_qsv  

	// rtsp server
	LiveConfig live_config;
	live_config.ip = "0.0.0.0";
	live_config.port = 8554;
	live_config.suffix = "live";

	// handle command lines arguments
	// e.g. streamingserver.exe --codec h264_qsv --framerate 25 --bitrate 8000000 --host 0.0.0.0 --port 8554 --suffix live
	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "--codec") == 0) {
			avconfig.codec = std::string(argv[i + 1]);
			i++;
		}
		if (std::strcmp(argv[i], "--framerate") == 0) {
			avconfig.framerate = atoi(argv[i + 1]);
			i++;
		}
		if (std::strcmp(argv[i], "--bitrate") == 0) {
			avconfig.bitrate_bps = atoi(argv[i + 1]);
			i++;
		}
		if (std::strcmp(argv[i], "--host") == 0) {
			live_config.ip = std::string(argv[i + 1]);
			i++;
		}
		if (std::strcmp(argv[i], "--port") == 0) {
			live_config.port = atoi(argv[i + 1]);
			i++;
		}
		if (std::strcmp(argv[i], "--suffix") == 0) {
			live_config.suffix = std::string(argv[i + 1]);
			i++;
		}
	}

	if (!ScreenLive::Instance().Init(avconfig)) {
		return 0;
	}

	ScreenLive::Instance().StartLive(SCREEN_LIVE_RTSP_SERVER, live_config);
	while (1) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}

	ScreenLive::Instance().StopLive(SCREEN_LIVE_RTSP_SERVER);
	ScreenLive::Instance().Destroy();
	return 0;
}