# CastDemo

Plays a video on an XGIMI Horizon S projector, or any other Google Cast receiver, from a
small window or from the command line. The Horizon S runs Google TV with Chromecast
built-in, so a PC reaches its screen over Wi-Fi through the Cast protocol rather than
Miracast or Bluetooth. The demo is a complete Cast sender in C99 and Win32, with no
third-party libraries:

1. **Discovery.** An SSDP `M-SEARCH` for the DIAL service goes out on every IPv4
   interface. Chromecast built-in devices answer with the URL of their DIAL device
   description on port 8008, which names the device (`XGIMI HORIZON S Max`) and its
   model.
2. **Channel.** A TCP connection to port 8009 is wrapped in TLS 1.2 by SChannel. Cast
   receivers present self-signed certificates, so the chain is not validated. Each
   CASTV2 frame is a 4-byte big-endian length and a `CastMessage` protocol buffer,
   encoded and decoded by the counted `utl_castv2` model over `utl_protobuf`.
3. **Session.** `CONNECT` to `receiver-0`, `LAUNCH` the default media receiver
   (`CC1AD845`), read the `RECEIVER_STATUS` for its `transportId`, `CONNECT` to that
   transport, wait for the application's first `MEDIA_STATUS`, then `LOAD` the media
   URL. Heartbeat `PING`s are answered with `PONG` and sent every five seconds; a media
   `GET_STATUS` every second drives the position display. `PAUSE`, `PLAY`, `SEEK` and
   the receiver `STOP` implement the toolbar.
4. **Media.** The receiver downloads and decodes the video itself. `media_server.c`
   serves the local file over HTTP/1.1 with `Range` support (`206 Partial Content`)
   on one thread per request, because the receiver probes the file with several
   concurrent range requests before it plays.

Receiver replies are JSON. The demo parses them with the cvk JSON reader
(`cvkParseJsonDocument`, `cvkGetJsonMember`, `cvkGetJsonElement`), the same reader
that loads pipeline descriptions in the library. No Vulkan device is created: the
projector does the decoding, so nothing from the ImmersiveWindowDemo renderer runs on
the PC. To show CVK-rendered frames on the projector, attach it as a Windows display
(HDMI, or a Miracast receiver app on the projector) and run ImmersiveWindowDemo on that
monitor instead.

## The window

`CastDemo.exe` lists the Cast receivers that answered, with address and model, and
preselects the first whose name contains `XGIMI`. **Refresh** searches again.
**Windows Cast…** opens Windows' own flyout for wireless (Miracast) displays, the route
for a projector that acts as a second monitor rather than a Cast receiver; the Horizon S
does not appear there unless a Miracast receiver app is running on it.

Choose a file with **Browse…**, or type an `http(s)` URL into the field, and press
**Cast** (or double-click the receiver). The toolbar has **Play**/**Pause**, **Stop**,
a seek slider and the receiver's state and position. The status line shows the session
log: TLS connection, application launch, the URL being loaded, and any error the
receiver reports. Closing the window stops playback and returns the projector to its
home screen.

### Subtitles

The **Subtitles** selector discovers SRT/WebVTT files beside the video or in its
`Subs` directory, MP4 `tx3g` text tracks, and embedded MKV UTF-8/SubRip text tracks.
MKV entries show their track number, language (including regional language tags),
and title when present. **Automatic** uses the first supported track; choose another
entry for a different language, **Browse...** for a separate subtitle file, or **Off**.
Choose subtitles before pressing **Cast**.

The sender converts the selected track to WebVTT and serves it to the receiver on
the media server's port. Cue times remain on the original movie timeline through
seeks, proxy switches and audio-gain switches. MKV extraction reads the container
directly and requires no external tools. Image subtitles such as PGS/VobSub and
compressed or encrypted subtitle tracks are not listed.

### Adaptive quality: the full stream, then a GPU proxy

The projector is a Chromecast built-in receiver, and its post-seek rebuffer is the real
latency floor: on the full-quality stream a seek takes one to three seconds to show smooth
motion, because the weak decoder must fetch, buffer and decode a heavy bitstream from the
new keyframe. Measurements (`--bounce N`, which times seek-to-actual-motion) show a light
bitstream clears that rebuffer far faster and never underruns.

So CastDemo serves two streams of the same content on one timeline:

- **Full quality** is the original file, served as-is with its original audio. It plays
  immediately, shows the full movie duration, and seeks natively (the receiver rebuffers).
- **A scrub proxy** is an in-process GPU transcode — no external tools. cvk's Vulkan Video
  wrappers decode the source on the AMD GPU, downscale, and re-encode H.264 into a fragmented
  MP4, with the source AAC passed straight through. A short GOP keeps seeks cheap, so once the
  session is on the proxy every seek is a cheap in-stream `SEEK` with no reload or audio reset.

The proxy's transcode settings are `GpuTranscode_DefaultConfig` (overridable live from the
GUI sliders): **1280 wide, GOP 12** (~0.5 s between keyframes at 24 fps), **QP 20 intra / 23
inter**, constant-QP (rate control disabled), with **motion-adaptive QP** on at strength 30 —
a gentle bit-saver on still frames driven by block-SAD optical-flow estimation, which also
forces a keyframe on a scene cut. `bitrateKbps` (4000) is informational only under constant QP.

The session plays the full stream immediately and switches to the proxy **once the proxy has
finished transcoding**, at the current position, and stays there. The switch waits for
completion on purpose: a Cast receiver only treats an HTTP source as a fixed-duration,
seekable VOD — honoring the container's full duration and not chasing a live edge — when the
response carries a real `Content-Length`, and under constant-QP the final size is known only
once the encode is done. The proxy is loaded as `BUFFERED` (not `LIVE`) so the receiver honors
that duration. Serving the still-growing file instead made the receiver show only the few
seconds transcoded so far and bounce around the timeline. A long movie therefore plays and
seeks on the full stream until its proxy is ready, then gains instant in-stream seeks; the
transcode runs well above real time on the GPU, so the wait is a fraction of the runtime.

Scrubbing is live. While the slider is held, every movement posts a *scrub*: a `SEEK`
with `resumeState PLAYBACK_PAUSE`, snapped to the keyframe before the target. A paused
seek to a keyframe is a one-frame decode, so the projector paints the preview at once
instead of decoding a whole group of pictures and refilling its playback buffer for every
intermediate position. Releasing the slider posts the exact position with
`PLAYBACK_START` when it was playing before. The keyframe times come from the file's own
`stss`/`stts` tables (`mp4_index.c`); a file without them scrubs to exact positions.

The session coalesces queued scrubs and sends the newest every 60 ms, polls for commands
every 5 ms, polls status every 100 ms while a seek settles, and sets `TCP_NODELAY` on the
Cast channel so a seek never queues behind an unacknowledged heartbeat. The HTTP server
sets `TCP_NODELAY`, uses a 1 MB send buffer and keeps connections alive, so the range the
receiver fetches after a seek needs no new TCP handshake. The session reports the seek
target as the current position until the receiver confirms it, which stops stale reports
from snapping the slider back, and the window advances the position at playback rate
between reports.

Measured against the Horizon S on Wi-Fi with a 60 s 720p clip (`CastDemoCli --scrub 40`
and `--seek 40` print these):

| Step | 250-frame GOP (x264 default) | 15-frame GOP |
|---|---|---|
| Scrub preview painted (paused, keyframe-snapped) | 63 ms | 63 ms |
| Exact seek confirmed by the receiver | 47 to 109 ms | 47 ms |
| Playing again after an exact seek | about 2.6 s | under 100 ms |

The last row is the receiver decoding from the previous keyframe; only the media's
keyframe interval changes it (`ffmpeg -g 15` when you encode the file yourself).

## The console tool

```
CastDemoCli [file.mp4] [options]     default: demo.mp4 beside the executable
  --device NAME    receiver whose friendly name contains NAME (default "XGIMI")
  --host ADDRESS   skip discovery and connect to this receiver
  --url URL        cast this URL instead of serving a local file
  --port N         HTTP port for the local file (default 8642)
  --bind ADDRESS   local IPv4 address to serve from (default: the one facing the receiver)
  --seconds N      stop after N seconds of playback
  --seek T         seek to T seconds three seconds into playback and report the round trip
  --scrub T        preview T (paused, keyframe-snapped) at three seconds, release to it at five
  --bounce N       simulate N drags across the timeline, timing seek-to-actual-motion each
  --loop           restart the file when it finishes
  --list           discover receivers and exit
  --verbose        print every Cast message sent and received
```

Space pauses and resumes, Q or Escape stops playback and closes the receiver
application. Ctrl+C does the same. The tool exits with 0 when playback finished or was
stopped, 2 when no receiver matched, 1 on any other failure. `--verbose` prints the
JSON of every message in both directions, which is the quickest way to see what the
receiver objects to.

## Build and run

Requires Windows and a Windows SDK/C compiler. Enable `CVK_BUILD_EXAMPLES` (on by
default on Windows). The scrub proxy transcodes in-process on the GPU through cvk's Vulkan
Video wrappers — no external tools are needed at runtime. A GPU without H.264 encode+decode
in Vulkan simply plays on the full-quality stream without a proxy.

```powershell
cmake -S . -B build/windows-msvc-2026 -G "Visual Studio 18 2026" -A x64 -DCVK_BUILD_EXAMPLES=ON
cmake --build build/windows-msvc-2026 --config Release --target CastDemo CastDemoCli
& .\build\windows-msvc-2026\examples\CastDemo\Release\CastDemo.exe "C:\path\to\video.mp4"
& .\build\windows-msvc-2026\examples\CastDemo\Release\CastDemoCli.exe --list
```

Turn the projector on first; a Google TV device in network standby also answers.

The receiver must be able to reach the PC. The first run makes Windows Defender
Firewall ask whether to allow the program; on a network Windows classifies as public
the connection is blocked unless the rule is added, the receiver never fetches the file,
and after about thirty seconds it reports `LOAD_FAILED`. Either allow the program when
prompted or add the rule once (both executables use port 8642 by default):

```powershell
netsh advfirewall firewall add rule name="cvk CastDemo" dir=in action=allow protocol=TCP localport=8642
```

The receiver plays what its media stack supports: MP4 with H.264 or HEVC video and
AAC audio, WebM, and MKV within those codecs. An `http(s)` URL is sent to the receiver
as is, without the local server, which is also the quickest check that the Cast channel
itself works when the firewall is in the way.

## Validation

`cvk-cast-message-test` checks the protocol buffer framing against known bytes,
round-trips a long payload, skips unknown fields, rejects truncated bodies and foreign
protocol versions, exercises the HTTP range grammar, and looks up the media receiver's
`transportId` in a recorded `RECEIVER_STATUS` with the cvk JSON reader.

```powershell
cmake --build build/windows-msvc-2026 --config Release --target cvk-cast-message-test
ctest --test-dir build/windows-msvc-2026 -C Release -R cvk-cast-messages --output-on-failure
```

Protocol references:

- [CastMessage protocol buffer](https://chromium.googlesource.com/chromium/src/+/main/components/cast_channel/proto/cast_channel.proto)
- [Cast media messages (LOAD, MEDIA_STATUS, PAUSE, PLAY, SEEK, STOP)](https://developers.google.com/cast/docs/media/messages)
- [DIAL discovery](http://www.dial-multiscreen.org/dial-protocol-specification)
- [SChannel stream contexts](https://learn.microsoft.com/en-us/windows/win32/secauthn/stream-contexts)
