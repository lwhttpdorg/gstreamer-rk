# Combined streams

## Use cases

There exists multiple formats where the production of the final output requires
combining multiple streams into one.

Those are the cases we consider.

* Layered scalable codecs such as H.265 Scalable extensions,
  AV1 and AV2. In all of those, a base layer can be combined with one
  or more derived layers to produce a combined bitstream that can be
  decoded at a higher quality than the base layer alone. For example,
  at a higher resolution, framerate or visual quality.
* Some DolbyVision files have an enhancement layer encoded as a separate
  bitstream. This enhancement layer could even be a separate codec
  (H.265 EL over a H.264 base layer)
* Enhancement codecs such as MPEG-5 LCEVC. This codec is layered on top of regular
  video codec as the base. It must be decoded separately and combined scaling+blending.
* WebM/MP4 Alpha for VP8, VP9, AV1, etc. As the VP8, VP9, AV1 codecs
  don't have a concept of an alpha channel, there is a specification
  to create a second stream for the alpha channel, where it gets
  encoded as the Y channel (where UV are discarded after decoding).
* Director's commentary audio tracks can be delivered as a voice track that must be
  overlaid on top of the main audio track for playback.
* Depth, texture or occupancy can be separated in some systems such as
  MPEG-I (MIV / V-PCC), those would be carried as separate streams and
  encoded with a regular video codec. The recomposition required
  multiple video decoder instances.
* HEVC alpha layers are actually separate streams inside the
  bitstream, most decoders don't support them as-is, but they can be
  extracted and decoded as a normal stream.

Although subtitles and closed captions streams could be considered
when combining with the base video stream, we considered them out of
scope for this design. This simplifies by not having to worry about
demultiplexing different media types.

## Transmission and multiplexing

The streams to be combined can be transmitted in different ways:

* Multiplexed in the same track as SEI or as part of the same bitstream
* As a BlockAdditional in a WebM container for WebM alpha and LCEVC
* As separate tracks in the same container file
* As separate container files, associated through DASH/HLS metadata
* As separate RTP streams

## Separation

If the streams arrive multiplexed, a "Demuxer" type element will be
created to split them into their elementary streams.

This should apply to:
* BlockAdditional if we chose to go that way
* HEVC layers (alpha, depth, etc)
 
## Recombination

* Before decoding: some streams need to be muxed into a single stream before feeding to a decoder
  * H.265 / AV1 scalable extensions
* After decoding: Most other formats need to be separately decoded, then merged
  * LCEVC hardware, DolbyVision, WebM Alpha, director's commentary
    tracks, Depth tracks
* After decoding only one stream: Some decoders, such as the software
  LCEVCdec take in the encoded enhancement stream along with the
  decoded base stream

WebM BlockAdditional presents a different workflow, they are similar
to GstMeta in that they are attached to a video frame. We can expose
those as a GstMeta, as we did for WebM Alpha. If desired, we can
create a secondary demuxer for those.

## API

Based on the streams variants API:

  * Add a "parent-stream-id" field to the GstStream
    * The presence of this field indicates that the stream requires
      the parent to also be activated, it can be recursive
  * Only one stream is selected (the child), and the demuxer/parsebin
    will also expose pads for the parents streams
  * The stream-start event will have a flag saying that this pad is activated as a parent
  *  Add a  "passthrough" field  to the  stream-select event  to avoid
    plugging a combining decoder  for transcoding use-cases. When this
    is selected, the GstStream in the stream-start event will have a
    passthrough field set on it. To also get the parent stream, it has to also
	be selected separately.

## Caps for the different types

### Secondary stream in meta

When the secondary stream is present in a meta, because it has been
parsed out of a SEI or arrived in a BlockAdditional, we can represent
it with an added "secondary-streams-in-meta" field containing a
UniqueList of caps.

For example:

 * LCEVC: `video/x-h264, secondary-streams-in-meta=(/uniquelist}{ (GstCaps)[video/x-lcevc] }`
 * WebM Alpha: `video/x-x-8, secondary-streams-in-meta=(/uniquelist}{ (GstCaps)[video/x-vp8, is-alpha=true] }`

### Secondary stream in another pad

When the secondary stream is separate, the recombination is very
specific to the media format, so the caps will also be specific:

 * WebM Alpha: `video/x-vp8, is-alpha=true`
 * HEVC Depth `video/x-h265, is-depth=true`
 * LCEVC: `video/x-lcevc` (it's always a secondary stream)
 * Audio commentary: `audio/x-raw, mixable-matrix=<...>` or an encoded audio like `audio/mpeg, mixable-matrix=<...>`
 * DolbyVision secondary track: `video/x-h265, dolby-vision-secondary-track=true`


## Decoding pipelines

We need to accommodate the following pipelines where the muxing
happens:

* Before decoding:
  * `h265parse ! video/x-h265 ! h265combiner name=c ! h265decoder ! sink src ! video/x-h265-partial ! c.`
  * Combiner is: video/x-h265 + video/x-h265-partial -> video/x-h265
* After decoding (LCEVC and DolbyVision)):
  * `h264parse ! video/x-h264 ! h264decoder ! lcevccombiner name=c ! sink src ! video/x-lcevc !  lcevcdecoder ! c.`
  * Combiner is: video/x-raw + video/x-raw -> video/x-raw
* After decoding (audio commentary track):
  * `aacparse ! audio/mpeg ! aacdecoder ! audiomixer name=m ! sink aacparse ! audio/mpeg ! aacdecoder ! m.`
  * The mixer is a normal audio mixer: audio/x-raw + audio/x-raw -> audio/x-raw
* After decoding (WebM Alpha)
  * `video/x-vp8 ! vp8dec ! alphacombine name=c ! sink video/x-vp8 ! vp8dec ! c. ! sink`
  * alphacombine is: video/x-raw + video/x-raw -> video/x-raw with alpha
* After decoding only one stream (software LCEVCdec)
  * `h264parse ! video/x-h264 ! h264decoder ! lcevcdec name=d ! sink src ! video/x-lcevc !  d.`
  * Combiner is: video/x-raw + video/x-lcevc -> video/x-raw

In particular, some of the combiners have very generic caps, so they
can only be selected by the caps before decoding, or even the metadata
from the demuxer for the WebM alpha and director's commentary use-cases.

## Combiners

For combiners, we can explore creating a base class over GstAggregator that generally expects 2 input pads
and has some logic to ensure a 1-to-1 match between them and handles any losses appropriately.

Those standalone combiners would have a rank of 0, so they're not auto-plugged by themselves, only as part of
a bin containing the decoders as well. Unless the combination and decoding is a single step for this
particular format.



## Decodebin3 suggestion

It would be impossible to guess the right combination solely by
looking at the caps of each input stream, as for many combinations the
caps are "raw + raw -> raw", we need to treat the whole decoding
pipeline as one unit.

It would be impossible to guess the order of the combiner and decoder
as well as the right combiner solely based on the caps. This is
especially complicated if there are elements wrapping APIs that do
combination and decoding in a single step.

Instead, we create a decoder bin for each format that has multiple input pads.

These bins could have an internal "mini-decodebin" that selects a
decoder from the registry following a logic similar to decodebin3,
ideally creating a helper that we can re-use across the different
use-cases.

For example:

* lcevcdecodebin
  * sink: video/x-h264; video/x-h265; video/x-h266; video/x-av1
  * sink_extra: video/x-lcevc
	* src: video/x-raw
* h265partialdecodebin
  * sink: video/x-h265
	* sink_extra_%d: video/x-h265-partial
	* src: video/x-raw
* WebM alpha:
  * sink: video/x-vp8; video/x-vp9
  * sink_extra: video/x-vp8, is-alpha=true; video/x-vp9, is-alpha=true
	* src: video/x-raw, format=AYUV
* audiocommentarybin
  * sink: audio/x-raw
	* sink_extra_%d: audio/x-raw, mixable-matrix=<...>
	* src: audio/x-raw
* dolbyvisionbin
  * sink: video/x-h265
  * sink_extra: video/x-h265, dolby-vision-secondary-track=true
	* src: video/x-raw

The audio commentary, DolbyVision and WebM alpha bins can be complex
to autoplug as they only use normal audio and video formats as inputs,
so an extra field is added to guide the decodebin3 code. This will be
populated by the demuxer, which has created the GstStreamCollection.

### Operation

When a variant requiring a combination is selected, decodebin3 will
intercept the matching stream-start, and will block the pad and wait
for the stream-start to be received on all of the pads required to
decode the variant. It will then look for a matching decoder using the
usual logic, but filtering only on decoders with the right number of
sink pads.

Question: who validates that the stream-start will arrive on all pads?
A timeout in decodebin3 ?

We will create a new klass "Combiningdecoder" that is different from
the normal "Decoder" klass, to avoid auto-plugging each other by
mistake.

### Handle secondary-streams-in-meta directly

As an optimization, a decoder element can be ranked and accept caps
with a `secondary-streams-in-meta` field directly. We can modify the
`autoplug-continue` handler in decodebin3 (handling the signal from
parsebin) to avoid plugging in a demuxer for this format if a ranked decoder
exists.
