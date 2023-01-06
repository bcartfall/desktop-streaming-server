# Desktop Streaming Server

This application was developed to solve the problem of not being able to fully focus on a video while completing all workout activities. This is the Desktop application that serves the video to a network connect mobile application. See https://github.com/bcartfall/desktop-streaming-stopwatch for the android mobile application.

The problem: When doing workout stretches and activities such as planks the head is facing the floor and it is not possible to watch the computer screen.

The solution: The mobile device can be placed on the floor and will duplicate the computer screen so that the video content can be enjoyed.

# Instructions

Compile this code in Visual Studio 2022 with x86 configuration.

# Usage

```bash
streamingserver.exe --codec h264_nvenc --framerate 25 --bitrate 8000000 --host 0.0.0.0 --port 8554 --suffix live
```

# Requirements

- Windows 8 or later.
- (optional) NVENC codec - NVIDIA GPU https://developer.nvidia.com/nvidia-video-codec-sdk.
- (optional) QSV codec - Intel QSV compatible processor.

# Licence

MIT. 

This desktop server code was based on https://github.com/PHZ76/DesktopSharing. 

This software uses code from FFmpeg licensed under the LGPLv2.1 and its source can be downloaded at https://ffmpeg.org/