<!-- SPDX-FileCopyrightText: 2025 Contributors to the Media eXchange Layer project. -->
<!-- SPDX-License-Identifier: CC-BY-4.0 -->

# Tools

## mxl-info

Simple tool that uses the MXL sdk to open a flow by ID and prints the flow details.

```bash
Usage: ./mxl-info [OPTIONS]

Options:
  -h,--help                   Print this help message and exit
  --version                   Display program version information and exit
  -d,--domain TEXT:DIR REQUIRED
                              The MXL domain directory
  -f,--flow TEXT              The flow id to analyse
  -l,--list                   List all flows in the MXL domain
```

Example 1 : listing all flows in a domain

```bash
./mxl-info -d ~/mxl_domain/ -l
--- MXL Flows ---
        5fbec3b1-1b0f-417d-9059-8b94a47197ed

```

Example 2 : Printing details about a specific flow

```bash
./mxl-info -d ~/mxl_domain/ -f 5fbec3b1-1b0f-417d-9059-8b94a47197ed

- Flow [5fbec3b1-1b0f-417d-9059-8b94a47197ed]
        version           : 1
        struct size       : 4192
        flags             : 0x00000000000000000000000000000000
        head index        : 52146817788
        grain rate        : 60000/1001
        grain count       : 3
        last write time   : 2025-02-19 11:44:12 UTC +062314246ns
        last read time    : 1970-01-01 00:00:00 UTC +000000000ns
```

Hint : Live monitoring of a flow details (updates every second)

```bash
watch -n 1 -p ./mxl-info -d ~/mxl_domain/ -f 5fbec3b1-1b0f-417d-9059-8b94a47197ed
```

## mxl-viewer

TODO. A generic GUI application based on gstreamer or ffmpeg to display flow(s).

## mxl-gst-videotestsrc

A binary that uses the gstreamer element 'videotestsrc' to produce video grains and/or audio samples which will be pushed to a MXL Flow. The flow is configured from a NMOS Flow json file. Here's an example of such file :

```json
{
  "description": "MXL Test File",
  "id": "5fbec3b1-1b0f-417d-9059-8b94a47197ef",
  "tags": {},
  "format": "urn:x-nmos:format:video",
  "label": "MXL Test File",
  "parents": [],
  "media_type": "video/v210",
  "grain_rate": {
    "numerator": 50,
    "denominator": 1
  },
  "frame_width": 1920,
  "frame_height": 1080,
  "colorspace": "BT709",
  "components": [
    {
      "name": "Y",
      "width": 1920,
      "height": 1080,
      "bit_depth": 10
    },
    {
      "name": "Cb",
      "width": 960,
      "height": 1080,
      "bit_depth": 10
    },
    {
      "name": "Cr",
      "width": 960,
      "height": 1080,
      "bit_depth": 10
    }
  ]
}
```

```bash
mxl-gst-videotestsrc


./build/Linux-Clang-Debug/tools/mxl-gst/mxl-gst-videotestsrc [OPTIONS]


OPTIONS:
  -h,     --help              Print this help message and exit
  -v,     --video-config-file TEXT
                              The json file which contains the Video NMOS Flow configuration
  -a,     --audio-config-file TEXT
                              The json file which contains the Audio NMOS Flow configuration
  -s,     --samples-per-batch UINT [512]
                              Number of audio samples per batch
          --audio-offset INT [0]
                              Audio sample offset in number of samples. Positive value means
                              you are adding a delay (commit in the past).
          --video-offset INT [0]
                              Video frame offset in number of frames. Positive value means you
                              are adding a delay (commit in the past).
  -d,     --domain TEXT:DIR REQUIRED
                              The MXL domain directory
  -p,     --pattern TEXT [smpte]
                              Test pattern to use. For available options see
                              https://gstreamer.freedesktop.org/documentation/videotestsrc/index.html?gi-language=c#GstVideoTestSrcPattern
  -t,     --overlay-text TEXT [EBU DMF MXL]
                              Change the text overlay of the test source
```

Example to run with video only:

```bash
./build/Linux-Clang-Debug/tools/mxl-gst/mxl-gst-videotestsrc \
  -d /dev/shm \
  -v lib/tests/data/v210_flow.json
```

Example to run with audio only:

```bash
./build/Linux-Clang-Debug/tools/mxl-gst/mxl-gst-videotestsrc \
  -d /dev/shm \
  -a lib/tests/data/audio_flow.json
```

Example to run with both video and audio:

```bash
./build/Linux-Clang-Debug/tools/mxl-gst/mxl-gst-videotestsrc \
  -d /dev/shm \
  -v lib/tests/data/v210_flow.json \
  -a lib/tests/data/audio_flow.json

```

## mxl-gst-videosink

A binary that reads from a MXL Flow and display the flow using the gstreamer element 'autovideosink'.

```bash
./build/Linux-GCC-Release/tools/mxl-gst/mxl-gst-videosink [OPTIONS]


OPTIONS:
  -h,     --help              Print this help message and exit
  -v,     --video-flow-id TEXT
                              The video flow ID
  -a,     --audio-flow-id TEXT
                              The audio flow ID
  -d,     --domain TEXT:DIR REQUIRED
                              The MXL domain directory
  -t,     --timeout UINT      The read timeout in ns, frame interval + 1 ms used if not
                              specified
  -l,     --listen-channels UINT [[0,1]]  ...
                              Audio channels to listen
          --audio-offset INT [150000000]
                              Audio playback offset in nanoseconds. Positive value means you
                              are delaying the playback
          --video-offset INT [150000000]
                              Video plaback offset in nanoseconds. Positive value means you are
                              delaying the playback
          --audio-read-delay INT [768]
                              How far in the past/future to read (in audio samples). Positive
                              value means you are delaying the read
          --video-read-delay INT [1]
                              How far in the past/future to read (in video frames). Positive
                              value means you are delaying the read
  -s,     --samples-per-batch UINT [512]
                              Number of audio samples per batch when reading. Should be the
                              same or lower than the videotestsrc setting.
```

Example to run with video only:

```bash
./build/Linux-Clang-Debug/tools/mxl-gst/mxl-gst-videosink \
  -d /dev/shm \
  -v 5fbec3b1-1b0f-417d-9059-8b94a47197ed
```

Example to run with audio only:

```bash
./build/Linux-Clang-Debug/tools/mxl-gst/mxl-gst-videosink \
  -d /dev/shm \
  -a b3bb5be7-9fe9-4324-a5bb-4c70e1084449
```

Example to run with both video and audio:

```bash
./build/Linux-Clang-Debug/tools/mxl-gst/mxl-gst-videosink \
  -d /dev/shm \
  -v 5fbec3b1-1b0f-417d-9059-8b94a47197ed \
  -a b3bb5be7-9fe9-4324-a5bb-4c70e1084449
```

If you experience audio artifacts, try increasing the --audio-offset value. On some systems running a type-2 hypervisor with a host VM, audio glitches have been observed with default settings. Increasing the buffer size can resolve this issue.

```bash
./build/Linux-Clang-Debug/tools/mxl-gst/mxl-gst-videosink \
  -d /dev/shm \
  -v 5fbec3b1-1b0f-417d-9059-8b94a47197ed \
  --video-offset 1000000000 \
  -a b3bb5be7-9fe9-4324-a5bb-4c70e1084449 \
  --audio-offset 1000000000

```
