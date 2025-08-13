# The airtime aws plugins

The primary purpose of **`airtimeaws`** is to provide a simplified, cached, and streaming experience of working with media files stored on AWS S3, seamlessly integrated with GStreamer.
At present, it contains a single element.

```bash
gst-inspect-1.0 --gst-plugin-path=./build/gst_airtime_plugin/aws/Debug airtimeaws
Plugin Details:
  Name                     airtimeaws
  Description              airtime aws plugin
  Filename                 /Users/me/gstreamer/builddir/subprojects/gst-plugins-bad/gst/airtimeaws/libgstairtimeaws.dylib
  Version                  1.27.1.1
  License                  LGPL
  Source module            gst-plugins-bad
  Documentation            https://gstreamer.freedesktop.org/documentation/airtimeaws/
  Binary package           GStreamer Bad Plug-ins git
  Origin URL               http://www.airtimetools.com/

  airtimes3src: airtime S3 (file) src element

  1 features:
  +-- 1 elements

```

## airtimes3src – AWS S3 Source Element for GStreamer

**`airtimes3src`** is a GStreamer source element that provides **URI-based**, **seekable**, **random** and **cached** access to AWS S3 objects.  
It acts like a file source, streaming data directly from an `s3://` location into your pipeline.

---

## Features at a Glance

| Feature | Description |
| ------- | ----------- |
| **URI Scheme** | Handles `s3://` and registers as a GStreamer URI handler |
| **Asynchronous Fetching** | Downloads from S3 in parallel using a thread pool |
| **Early Start** | Pipeline can process data before the file is fully downloaded |
| **Seeking** | Prioritizes requested byte ranges for instant random access |
| **Local Caching** | Stores chunks on disk for reuse between runs or pipelines |
| **Partial Caching** | Useful for GStreamer discovery; resumes incomplete downloads |
| **Cache Policies** | Supports eviction (e.g., LRU) to manage cache size |
| **Metrics** | Posts `airtimes3src::metrics` to the bus with location, size, and progress |
| **S3 Authentication** | Uses standard AWS credential resolution, including environment variables (`AWS_ACCESS_KEY_ID`, `AWS_SECRET_ACCESS_KEY`, etc.) |

For more details on the performance, please read [the performance section](#performance).

---

## How It Works

- **Asynchronous fetching** – Data is downloaded from S3 by a pool of worker threads in the background.  
- **Start before complete** – The pipeline begins processing as soon as enough data arrives; no need to wait for the entire download.  

#### Seeking and Prioritization
When you seek within the file:
- The requested byte ranges are fetched first for instant access.
- Remaining data is downloaded in the background.
- Ideal for large files requiring quick random access.

### Local Caching

`airtimes3src` caches downloaded data in **chunked files** on the local filesystem.

- **Persistence** – Cache survives between pipeline runs and can be reused by multiple pipelines in the same process.  
- **Partial caching** – Only stores the portions you’ve used (great for discovery).  
- **Resumable downloads** – Can continue an incomplete download where it left off.  
- **Cache management** – Supports eviction policies such as LRU.  
- **Consistency checks** – Corrupted or mismatched chunks are purged and refetched.

---

## Integration

- Works in **GStreamer pipelines**, **GES projects**, and **discovery tools**.
- Automatically chosen if it has the highest element rank.
- Registers as a URI handler for `s3://`.

## AWS authentication and configuration

The element assumes that authentication and configuration is set by the environment. This includes AWS authentication and
properties such as the AWS Region to use. 

---

## Metrics

**`airtimes3src`** posts a **`airtimes3src::metrics`** message to the pipeline bus with:

- S3 object location (`s3://…`)  
- Content length  
- Download progress and completion state  

---

## Example

```bash
gst-launch-1.0 airtimes3src location="s3://my-bucket/path/to/my/video.mp4" ! decodebin ! autovideosink
```
While running it for the second time instead of fetching the file again from S3, the locally cached chunks are read.

## Inspecting the `airtimes3src` element

```bash
gst-inspect-1.0 --gst-plugin-path=./build/gst_airtime_plugin/aws/Debug airtimes3src
Factory Details:
  Rank                     primary (256)
  Long-name                airtime S3 (file) src element
  Klass                    Source
  Description              Serves as an S3 (file) source element
  Author                   Teus Groenewoud <teus@hotmail.com>, Tomasz Mikolajczyk <tmmikolajczyk@gmail.com>

Plugin Details:
  Name                     airtimeaws
  Description              airtime aws plugin
  Filename                 /Users/me/gstreamer/builddir/subprojects/gst-plugins-bad/gst/airtimeaws/libgstairtimeaws.dylib
  Version                  1.27.1.1
  License                  LGPL
  Source module            gst-plugins-bad
  Documentation            https://gstreamer.freedesktop.org/documentation/airtimeaws/
  Binary package           GStreamer Bad Plug-ins git
  Origin URL               http://www.airtimetools.com/

GObject
 +----GInitiallyUnowned
       +----GstObject
             +----GstElement
                   +----GstBaseSrc
                         +----GstAirtimeS3Src

Implemented Interfaces:
  GstURIHandler

Element Flags:
  - SOURCE

Pad Templates:
  SRC template: 'src'
    Availability: Always
    Capabilities:
      ANY

Element has no clocking capabilities.

URI handling capabilities:
  Element can act as source.
  Supported URI protocols:
    s3

Pads:
  SRC: 'src'
    Pad Template: 'src'

Element Properties:

  automatic-eos       : Automatically EOS when the segment is done
                        flags: readable, writable
                        Boolean. Default: true
  
  blocksize           : Size in bytes to read per buffer (-1 = default)
                        flags: readable, writable
                        Unsigned Integer. Range: 0 - 4294967295 Default: 4096 
  
  cache-directory     : The base directory to use for local S3 file caching. It points to a local directory where the S3 file is downloaded and stored in chunks. If not set, an OS-specific temporary directory is used as the base cache directory. Each S3 URI is stored in a dedicated bucket/key-specific subdirectory.
                        flags: readable, writable, changeable only in NULL or READY state
                        String. Default: "/var/folders/mk/r99zwhtj1lb6ylpbl3gnk80h0000gn/T/airtime_s3_cache"
  
  cache-max-size-bytes: The maximum total cache size in bytes. When this limit is reached, the LRU eviction policy removes cache directory of the least recently used S3 file.
                        flags: readable, writable, changeable only in NULL or READY state
                        Unsigned Integer64. Range: 10485760 - 18446744073709551615 Default: 10737418240 
  
  do-timestamp        : Apply current stream time to buffers
                        flags: readable, writable
                        Boolean. Default: false
  
  download-chunk-size-bytes: The size in bytes of the chunks of the S3 object to download is divided into. Must be multiple of the file-chunk-size property value.
                        flags: readable, writable, changeable only in NULL or READY state
                        Unsigned Integer64. Range: 512 - 18446744073709551615 Default: 2097152 
  
  fetch-max-retry-count: The maximum number of retries for S3 fetch operations that fail due to transient errors (e.g., network issues).
                        flags: readable, writable, changeable only in NULL or READY state
                        Unsigned Integer. Range: 2 - 4294967295 Default: 2 
  
  file-chunk-size-bytes: The size in bytes of the cached file chunk. Each downloaded S3 chunk is split into these smaller chunks for storage. The download-chunk-size property value must be multiple of this value.
                        flags: readable, writable, changeable only in NULL or READY state
                        Unsigned Integer64. Range: 512 - 18446744073709551615 Default: 1048576 
  
  location            : The location to use for the source. This should be a valid AWS S3 uri as follows: 's3://<s3_bucket>/<s3_key>'. AWS authentication is assumed to have been handled by the environment.
                        flags: readable, writable, changeable only in NULL or READY state
                        String. Default: ""
  
  max-concurrent-downloads: The maximum number of concurrent S3 chunk downloads to cache the S3 object locally.
                        flags: readable, writable, changeable only in NULL or READY state
                        Unsigned Integer64. Range: 1 - 18446744073709551615 Default: 25 
  
  name                : The name of the object
                        flags: readable, writable
                        String. Default: "airtimes3src0"
  
  num-buffers         : Number of buffers to output before sending EOS (-1 = unlimited)
                        flags: readable, writable
                        Integer. Range: -1 - 2147483647 Default: -1 
  
  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"
  
  typefind            : Run typefind before negotiating (deprecated, non-functional)
                        flags: readable, writable, deprecated
                        Boolean. Default: false
```

It should be noted that this element inherits from `GstBaseSrc` and looks very similar to `filesrc`.

The most important **`airtimes3src`-specific** properties are:

- **`location`** – The S3 URI pointing to the media file to stream.

- **`cache-directory`** – Path to the base directory to use for local S3 file caching. 
   - It points to a local directory where the S3 file is downloaded and stored in chunks.
   - If not set, an OS-specific temporary directory is used as the base cache directory.
   - Each S3 URI is stored in a dedicated bucket/key-specific subdirectory.

- **`cache-max-size-bytes`** – The maximum total cache size in bytes. When this limit is reached, the LRU eviction policy removes cache directory of the least recently used S3 file.

- **`download-chunk-size-bytes`** – Size (in bytes) of each chunk requested from S3.  
  - Must be a multiple of the `file-chunk-size-bytes` value.

- **`fetch-max-retry-count`** – The maximum number of retries for S3 fetch operations that fail due to transient errors (e.g., network issues).

- **`file-chunk-size-bytes`** – Size (in bytes) of each cached file chunk.  
  - Each downloaded S3 chunk is split into these smaller chunks for storage.

- **`max-concurrent-downloads`** – The maximum number of S3 chunks fetched in parallel.


## Seeking and cache-reusing example

When performing discovery from an S3 URI, the requested byte range is given download priority.
The resulting cache may be incomplete — only the requested range is guaranteed to be fetched.
However, because fetching occurs asynchronously, additional data beyond the requested range may also be downloaded.

```bash
gst-discoverer-1.0 s3://my-bucket/path/to/media.webm
Analyzing s3://my-bucket/path/to/media.webm
Done discovering s3://my-bucket/path/to/media.webm

Properties:
  Duration: 0:10:18.979000000
  Seekable: yes
  Live: no
  container #0: WebM
    video #1: VP9
      Stream ID: a783b1a0eb0889d97efabe3bea4cd5b9de66c897fc2bf53e281067f7e8f73fe8/001:7300705198089032394
      Width: 1920
      Height: 1080
      Depth: 24
      Frame rate: 30/1
      Pixel aspect ratio: 1/1
      Interlaced: false
      Bitrate: 0
      Max bitrate: 0
    audio #2: Opus
      Stream ID: a783b1a0eb0889d97efabe3bea4cd5b9de66c897fc2bf53e281067f7e8f73fe8/002:9390430879972339175
      Language: und
      Channels: 2 (front-left, front-right)
      Sample rate: 48000
      Depth: 16
      Bitrate: 0
      Max bitrate: 0


ls -1 /var/folders/mk/r99zwhtj1lb6ylpbl3gnk80h0000gn/T/airtime_s3_cache/my-bucket/path/to/media.webm/
chunk_00000000.part
chunk_00000001.part
chunk_00000002.part
chunk_00000003.part
chunk_00000004.part
chunk_00000005.part
chunk_00000188.part
chunk_00000189.part
file_chunk_size.txt
last_access_time.txt
metadata.json
```

In the example above, only the beginning and end of the file—required for discovery—are fetched. With a 1MB file chunk size, this results in less than 8MB downloaded in total, avoiding the need to download the entire file, which can be very large (189MB in this case).

When a partial cache is available and the pipeline requires the full file, existing cached chunks are reused, and only the missing chunks are downloaded.

```bash
gst-launch-1.0 uridecodebin uri=s3://my-bucket/path/to/media.webm name=d d. ! audioconvert ! fakesink d. ! videoconvert ! fakesink
Setting pipeline to PAUSED ...
Pipeline is PREROLLING ...
Got context from element 'd': airtime.s3.src.context=context, airtime.s3.src.context=(GstAirtimeS3SrcContext)"\(GstAirtimeS3SrcContext\)\ airtimes3srccontext0";
Redistribute latency...
Pipeline is PREROLLED ...
Setting pipeline to PLAYING ...
Redistribute latency...
New clock: GstSystemClock
Got EOS from element "pipeline0".
Execution ended after 0:00:30.717874166
Setting pipeline to NULL ...
Freeing pipeline ...


ls -1 /var/folders/mk/r99zwhtj1lb6ylpbl3gnk80h0000gn/T/airtime_s3_cache/my-bucket/path/to/media.webm/chunk_*.part | wc -l
     190
```

## Cache consistency

The S3 file metadata is stored in the cache as a simple metadata.json file.
Example:

```bash
cat /var/folders/mk/r99zwhtj1lb6ylpbl3gnk80h0000gn/T/airtime_s3_cache/my-bucket/path/to/media.webm/metadata.json 
{
  "bucket" : "my-bucket",
  "key" : "/path/to/media.webm",
  "content_length" : 198426907,
  "entity_tag" : "c66350c1be11c0e4d25dcb9ef022fd95-12",
  "version_id" : "",
  "expiration" : "",
  "last_modified" : "Fri, 15 Dec 2023 01:38:02 GMT"
}
```

Before reading a file from the existing cache, airtimes3src fetches the object’s metadata from S3 and compares it to the cached values.
It also verifies the availability of all cached chunks.
If any mismatch is detected, the affected cache is purged and the file is re-downloaded from scratch.

 Apart from the `metadata.json` file and the file chunks stored as a collection of `*.parts` files, the cache directory also contains the following files:

- **`file_chunk_size.txt`** – Stores the regular size (in bytes) of a single file chunk.  
  This is used when reading an existing cache to verify consistency with the current `airtimes3src` configuration.

- **`last_access_time.txt`** – Contains a timestamp indicating the last time the cached S3 file was accessed.  
  This timestamp is used by the LRU cache eviction policy to determine which files to remove.

## Performance

The **`airtimes3src`** element delivers excellent performance. Its main advantage is that it processes data in parallel with retrieval and fetches multiple chunks simultaneously (25 by default).

In the example below, the file is first retrieved from S3 using the `aws s3 cp` CLI command, then processed from the local filesystem through a simple pipeline that decodes the entire media content.

```
aws s3 cp s3://my-bucket/path/to/media.webm media.webm
gst-launch-1.0 filesrc location=./media.webm ! decodebin name=d d. ! videoconvert ! fakesink d. ! audioconvert ! fakesink
```

The next example runs the same decoding pipeline but uses the **`airtimes3src`**  element instead of `filesrc`:
```
gst-launch-1.0 airtimes3src location=s3://my-bucket/path/to/media.webm media.webm ! decodebin name=d d. ! videoconvert ! fakesink d. ! audioconvert ! fakesink
```

The tests have been performed multiple times and the table below shows an average durations. The performance depends a lot on the speed of the network. The tests have been performed on a mobile network (LTE), MacBook M3 Pro, 36GB of RAM.

| Test | Duration [seconds] |
| ------- | ----------- |
| `aws s3 cp` + `filesrc` | 21 + 12 = 33 |
| `airtimes3src` with cold cache | **21** |
| `airtimes3src` with warm cache | **12** |

With a warm cache, the overhead of processing chunks directory, checking the cache consistency and potential gaps analysis is negligible compared to processing a regular file with `filesrc`.

## More on the implementation details

Although **Rust** is the preferred language, the plugin implementation is primarily written in **C++** for the authors’ convenience. It is structured into multiple well-defined layers designed with **SOLID** principles in mind. Each component is testable, and a comprehensive suite of unit and integration tests has been developed using the [Catch2](https://github.com/catchorg/Catch2) testing framework. The implementation optionally depends on the [ASIO](https://github.com/chriskohlhoff/asio) library, distributed with [Boost](https://www.boost.org/), to simulate S3 fetching, enabling isolated testing of other layers. These tests are not currently included in the distribution due to limited time available to rewrite them using the GStreamer-preferred testing framework.
