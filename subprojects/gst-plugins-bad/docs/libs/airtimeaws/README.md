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
It supports both **single S3 objects** and **S3 directories (prefixes)**, where all files under a prefix are concatenated into a single virtual byte stream.

---

## Features at a Glance

| Feature                        | Description                                                                                                                   |
| ------------------------------ | ----------------------------------------------------------------------------------------------------------------------------- |
| **URI Scheme**                 | Handles `s3://` and registers as a GStreamer URI handler                                                                      |
| **Single & Directory Sources** | Supports both single S3 objects and S3 directory prefixes (multiple files concatenated into one stream)                       |
| **Asynchronous Fetching**      | Downloads from S3 in parallel using a thread pool                                                                             |
| **Early Start**                | Pipeline can process data before the file is fully downloaded                                                                 |
| **Seeking**                    | Prioritizes requested byte ranges for instant random access                                                                   |
| **Local Caching**              | Stores chunks on disk for reuse between runs or pipelines                                                                     |
| **Partial Caching**            | Useful for GStreamer discovery; resumes incomplete downloads                                                                  |
| **Cache Policies**             | Supports eviction (e.g., LRU) to manage cache size                                                                            |
| **Metrics**                    | Posts `airtimes3src::metrics` to the bus with location, size, and progress                                                    |
| **S3 Authentication**          | Uses standard AWS credential resolution, including environment variables (`AWS_ACCESS_KEY_ID`, `AWS_SECRET_ACCESS_KEY`, etc.) |

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

## S3 Directory (Prefix) Source

When the S3 URI points to a **directory prefix** rather than a single object, `airtimes3src` lists all files under that prefix and presents them as a **single concatenated byte stream** to downstream elements. This enables use cases where content is stored as multiple files in an S3 "directory" (e.g., chunked recordings, segmented media) but needs to be consumed as one continuous source.

### How it works

1. **Listing** — `ListObjectsV2` is called with the prefix and a `/` delimiter (non-recursive, single level only). Files are sorted lexicographically by key.
2. **Virtual byte stream** — Each file is assigned a cumulative virtual offset. A 5 MB + 3 MB + 4 MB directory becomes a single 12 MB stream.
3. **Range mapping** — When a byte range is requested, `resolveVirtualRange()` maps virtual offsets to physical S3 object ranges using binary search. Most requests hit a single object; boundary-crossing ranges (spanning two files) are handled by concatenating synchronous downloads.
4. **Cache consistency** — A composite entity tag is computed as a SHA-256 hash of all individual file ETags, enabling cache invalidation when any file in the directory changes.

### Source detection

The element determines whether a URI points to a single key or a directory using the `source-hint` property:

| `source-hint`    | Behavior                                                                                                                                                         |
| ---------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `none` (default) | Auto-detect: a trailing `/` in the URI triggers directory mode. Otherwise, `HeadObject` is tried first; if it fails, `ListObjectsV2` is attempted as a fallback. |
| `key`            | Forces single-object mode.                                                                                                                                       |
| `prefix`         | Forces directory/prefix mode.                                                                                                                                    |

**Performance tip:** When you know in advance whether the URI points to a single object or a directory, setting `source-hint` to `key` or `prefix` avoids the extra S3 API calls that `none` mode performs for auto-detection (a `HeadObject` probe followed by a potential `ListObjectsV2` fallback). This can noticeably reduce startup latency, especially on high-latency connections.

### File filtering

The `file-pattern` property accepts a glob pattern (e.g., `*.ts`, `segment_*`) to filter files under the prefix. An empty string (the default) includes all files.

### Example

```bash
# Stream all .ts files under an S3 prefix as a single source
gst-launch-1.0 airtimes3src location="s3://my-bucket/recordings/session-1/" source-hint=prefix file-pattern="*.ts" ! decodebin ! autovideosink

# Auto-detect directory mode via trailing slash
gst-launch-1.0 airtimes3src location="s3://my-bucket/recordings/session-1/" ! decodebin ! autovideosink
```

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

## Examples

**Single S3 object:**

```bash
gst-launch-1.0 airtimes3src location="s3://my-bucket/path/to/my/video.mp4" ! decodebin ! autovideosink
```

**S3 directory (prefix):**

```bash
gst-launch-1.0 airtimes3src location="s3://my-bucket/chunks/" source-hint=prefix file-pattern="*.part" ! decodebin ! autovideosink
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

  cache-max-size-bytes: The maximum total cache size in bytes. When this limit is reached, the LRU eviction policy removes cache directory of the least recently used S3 file. Setting this value to 0 disables eviction making the cache unbounded.
                        flags: readable, writable, changeable only in NULL or READY state
                        Unsigned Integer64. Range: 0 - 18446744073709551615 Default: 10737418240

  do-timestamp        : Apply current stream time to buffers
                        flags: readable, writable
                        Boolean. Default: false

  download-chunk-size-bytes: The size in bytes of the chunks of the S3 object to download is divided into. Must be multiple of the file-chunk-size property value.
                        flags: readable, writable, changeable only in NULL or READY state
                        Unsigned Integer64. Range: 512 - 18446744073709551615 Default: 2097152

  ensure-correct-region: Whether to ensure that the S3 bucket is accessed in the correct region. If enabled, the element will attempt to determine the correct region for the specified S3 bucket and configure the S3 client accordingly.
                        flags: readable, writable, changeable only in NULL or READY state
                        Boolean. Default: false

  fetch-max-retry-count: The maximum number of retries for S3 fetch operations that fail due to transient errors (e.g., network issues).
                        flags: readable, writable, changeable only in NULL or READY state
                        Unsigned Integer. Range: 0 - 4294967295 Default: 2

  file-chunk-size-bytes: The size in bytes of the cached file chunk. Each downloaded S3 chunk is split into these smaller chunks for storage. The download-chunk-size property value must be multiple of this value.
                        flags: readable, writable, changeable only in NULL or READY state
                        Unsigned Integer64. Range: 512 - 18446744073709551615 Default: 1048576

  http-request-timeout: Corresponds to the AWS client configuration httpRequestTimeoutMs property.
                        flags: readable, writable, changeable only in NULL or READY state
                        Long. Range: 0 - 9223372036854775807 Default: 0

  file-pattern        : Glob pattern to filter files when operating in directory/prefix mode (e.g. '*.ts', 'segment_*'). An empty string (default) means include all files under the prefix.
                        flags: readable, writable, changeable only in NULL or READY state
                        String. Default: ""

  location            : The location to use for the source. This should be a valid AWS S3 URI as follows: 's3://<s3_bucket>/<s3_key>' for a single object or 's3://<s3_bucket>/<s3_prefix>/' for a directory. AWS authentication is assumed to have been handled by the environment.
                        flags: readable, writable, changeable only in NULL or READY state
                        String. Default: ""

  max-concurrent-downloads: The maximum number of concurrent S3 chunk downloads to cache the S3 object locally. If set to 0, the default value will be used as defined in the AWS SDK.
                        flags: readable, writable, changeable only in NULL or READY state
                        Unsigned Integer64. Range: 0 - 18446744073709551615 Default: 25

  name                : The name of the object
                        flags: readable, writable
                        String. Default: "airtimes3src0"

  num-buffers         : Number of buffers to output before sending EOS (-1 = unlimited)
                        flags: readable, writable
                        Integer. Range: -1 - 2147483647 Default: -1

  parent              : The parent of the object
                        flags: readable, writable
                        Object of type "GstObject"

  request-timeout     : Corresponds to the AWS client configuration requestTimeoutMs property.
                        flags: readable, writable, changeable only in NULL or READY state
                        Long. Range: 0 - 9223372036854775807 Default: 0

  source-hint         : Hint for whether the S3 URI points to a single object (key) or a directory (prefix). When set to 'none' (default), the element auto-detects by trying HeadObject first, then falling back to ListObjectsV2. Use 'key' to force single-object mode or 'prefix' to force directory mode.
                        flags: readable, writable, changeable only in NULL or READY state
                        Enum "GstAirtimeS3SrcSourceHintType" Default: 0, "none"
                           (0): none             - Auto-detect (default)
                           (1): key              - Single S3 object key
                           (2): prefix           - S3 directory prefix

  trust-cached-data   : Whether to trust the integrity of cached data without revalidating it with S3 metadata object. It may avoid unnecessary S3 requests if the metadata is already cached and allows for working with the cached object without having an active internet connection.
                        flags: readable, writable, changeable only in NULL or READY state
                        Boolean. Default: false

  typefind            : Run typefind before negotiating (deprecated, non-functional)
                        flags: readable, writable, deprecated
                        Boolean. Default: false

  validate-credentials: Whether to validate AWS credentials before using them. If enabled, the element will attempt to validate the provided AWS credentials by making a simple request to AWS STS service. If the credentials are invalid, the element will fail to start.
                        flags: readable, writable, changeable only in NULL or READY state
                        Boolean. Default: false
```

It should be noted that this element inherits from `GstBaseSrc` and looks very similar to `filesrc`.

The most important **`airtimes3src`-specific** properties are:

- **`location`** – The S3 URI pointing to the media file or directory to stream. Use `s3://bucket/key` for a single object or `s3://bucket/prefix/` (trailing slash) for a directory.

- **`source-hint`** – Hint for whether the URI points to a single object (`key`) or a directory prefix (`prefix`). Default is `none` (auto-detect). See [S3 Directory (Prefix) Source](#s3-directory-prefix-source) for details.

- **`file-pattern`** – Glob pattern to filter files when in directory/prefix mode (e.g., `*.ts`, `segment_*`). Empty string (default) includes all files.

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

- **`trust-cached-data`** - Specifies whether to trust the integrity of cached data without revalidating it with S3 metadata object. It may avoid unnecessary S3 requests if the metadata is already cached. Setting this property to true allows for working with the cached S3 object without an active internet connection.

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

In the example below, the 189M file is first retrieved from S3 using the `aws s3 cp` CLI command, then processed from the local filesystem through a simple pipeline that decodes the entire media content.

```
aws s3 cp s3://my-bucket/path/to/media.webm media.webm
gst-launch-1.0 filesrc location=./media.webm ! decodebin name=d d. ! videoconvert ! fakesink d. ! audioconvert ! fakesink
```

The next example runs the same decoding pipeline but uses the **`airtimes3src`** element instead of `filesrc`:

```
gst-launch-1.0 airtimes3src location=s3://my-bucket/path/to/media.webm ! decodebin name=d d. ! videoconvert ! fakesink d. ! audioconvert ! fakesink
```

The tests were run multiple times, and the table below presents average durations for two files: a relatively small 189 MB file and a larger 1.6 GB file. Performance varies significantly depending on network speed. The smaller file was tested over a mobile LTE connection, while the larger file was tested on a fiber network. Both sets of tests were conducted on a MacBook M3 Pro with 36 GB of RAM.

| Operation                      | 189MB file          | Speedup | 1.6GB file                  | Speedup |
| ------------------------------ | ------------------- | ------- | --------------------------- | ------- |
| `aws s3 cp` + `filesrc`        | 21s + 12s = **33s** | 1×      | 54s + 2m 43s = **3min 37s** | 1×      |
| `airtimes3src` with cold cache | **15s**             | 2.2×    | **2min 46s**                | 1.3×    |
| `airtimes3src` with warm cache | **12s**             | 2.8×    | **2min 46s**                | 1.3×    |

With a warm cache, the overhead of processing chunks directory, checking the cache consistency and potential gaps analysis is negligible compared to processing a regular file with `filesrc`. However, please note that the pipeline starts running immediately, processing the requested byte range as soon as those bytes are available — it does not wait for the entire file to be fetched. With that in mind, comparing aws s3 cp + filesrc to airtimes3src is not entirely fair. The above example pipelines perform very little work, so they run quickly. In more realistic scenarios, where pipeline processing takes longer, the time spent fetching data — even with a cold cache — may be completely masked by the processing time, especially for large files.

### Comparison with the existing `awss3src` element

The airtimes3src element offers a range of advanced features and significant performance improvements over the existing Rust-based awss3src element. Below is a summary of the key differences in functionality and performance.

#### Functionality

Although at first glance `airtimes3src` may seem similar to the existing `awss3src` element implemented in Rust, it offers significantly more functionality. In particular:

- **True seeking support — even during ongoing downloads**
  - `airtimes3src` can seek accurately while S3 downloads are still in progress.
  - `awss3src` does not support seeking ([source code](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/blob/main/net/aws/src/s3src/imp.rs?ref_type=heads#L571)), where a `FIXME` remains.
  - This limitation causes errors in GES and other components.

- **Parallel byte-range downloads with prioritization**
  - Downloads up to N byte ranges at once using AWS’s `ThreadPoolExecutor` (by default N-25).
  - Prioritizes byte ranges requested by `GstBaseSrc`, enabling pipelines to start processing early.
  - `awss3src` lacks this logic and downloads only one range at a time ([code reference](https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/blob/main/net/aws/src/s3src/imp.rs?ref_type=heads#L221)).

- **Advanced local caching**
  - Reuses downloaded chunks to avoid redundant transfers.
  - Cache persists between pipeline runs and discovery attempts — no need to re-download objects unnecessarily.
  - `awss3src` has no caching mechanism and must re-download each time.

- **On-demand downloading**
  - Only fetches the exact byte ranges required, rather than downloading the full object when not needed.

- **Metrics and download feedback**
  - Provides real-time download metrics and status reporting.
  - `awss3src` has no equivalent functionality.

- **Robust URI handling**
  - Correctly processes all valid URIs.
  - `awss3src` can fail on certain URIs, producing errors such as:
    - `"Could not parse URI"`
    - `"Failed to get HEAD object: dispatch failure: io error: ... dns error: failed to lookup address information: nodename nor servname provided, or not known"`

#### Performance

The following pipelines were used to compare the processing times of the existing **awss3src** (baseline, gstreamer 1.26.7) and **airtimes3src**:

```bash
gst-launch-1.0 awss3src uri=s3://REGION/BUCKET/KEY ! decodebin name=d d. ! videoconvert ! fakesink d. ! audioconvert ! fakesink
gst-launch-1.0 airtimes3src location=s3://BUCKET/KEY ! decodebin name=d d. ! videoconvert ! fakesink d. ! audioconvert ! fakesink
```

Results:

- _189MB file, Webm/VP9/Opus, length: 10min 18s_:

| Element      | Duration           | Speedup |
| ------------ | ------------------ | ------- |
| awss3src     | 53.3s              | 1×      |
| airtimes3src | 17.7s (cold cache) | 3.0×    |
| airtimes3src | 12.5s (warm cache) | 4.3×    |

- _2GB file, Webm/VP8/Opus, length: 7min 11s_:

| Element      | Duration               | Speedup |
| ------------ | ---------------------- | ------- |
| awss3src     | 21m 58.62s             | 1×      |
| airtimes3src | 2m 51.75s (cold cache) | 7.7×    |
| airtimes3src | 2m 45.02s (warm cache) | 8.0×    |

The substantial performance improvement of **airtimes3src** is primarily due to its parallel byte-range downloading with prioritization and robust, true seeking capabilities.

## More on the implementation details

Although **Rust** is the preferred language, the plugin implementation is primarily written in **C++** for the authors’ convenience. It is structured into multiple well-defined layers designed with **SOLID** principles in mind. Each component is testable, and a comprehensive suite of unit and integration tests has been developed using the [Catch2](https://github.com/catchorg/Catch2) testing framework. The implementation optionally depends on the [ASIO](https://github.com/chriskohlhoff/asio) library, distributed with [Boost](https://www.boost.org/), to simulate S3 fetching, enabling isolated testing of other layers. These tests are not currently included in the distribution due to limited time available to rewrite them using the GStreamer-preferred testing framework.
