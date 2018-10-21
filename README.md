This is a library allowing remote control an app through network.
Particularly useful when you want to use your PC to remote control a mobile app.

The mobile app's display will be streamed to PC, and mouse clicks on PC will be
treated as touch controls in mobile app for example.

I created this library to support [Multiness](https://lehoangquyenblog.wordpress.com/published-games/74-2/)
Online Multiplayer feature.

* There are two endpoints: Client and Server.
* Server will capture rendered frame and send to client.
* Client can then receive the frame sent from Server by calling `getFrameEvent`.
* Note that how Server capture current frame depends on the component `IFrameCapturer` supplied when you construct Server object.
* See `FrameCapturer.h` and `FrameCapturerGL.h` for example of `IFrameCapturer` implementation that captures current OpenGL's display buffer.
* Before the captured frame is sent to Client. It needs to be compressed. Hence the component `IImgCompressor` needs to be supplied to Server's constructor also.
* Finally, `IAudioCapturer` can be supplied if you want Server to stream audio data also. It is optional.

## License
This library is licensed under Apache License, Version 2.0. See the file `NOTICE`
for more details.
