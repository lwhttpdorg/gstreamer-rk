# Streamheader

## Purpose
A lot of data streams need a few initial buffers to make sense of the
data stream.  With these initial buffers, it can pick up at any point in
the rest of the data stream.

Examples:
- Vorbis and Theora have three initial Ogg packets
- MPEG4 has codec data
- GDP protocol serializes the initial new_segment event and the initial caps
- MPEG-TS has PAT & PMT tables

It is important that elements that get connected after the stream starts,
receive these initial buffers.  Also, sink elements should know that these
initial buffers are necessary for every new "client" of the sink element;
for example, multifdsink needs to send these initial buffers before the
normal buffers.

## Transport

Data that is essential for data stream utilization are stored in buffers that
are marked with `GST_BUFFER_FLAG_HEADER` flag. Additionally these buffers are
embedded inside caps. More specifically these buffers are stored in a caps field of
type `GST_BUFFER` or `GST_TYPE_ARRAY`. This value is itself added to
[caps](caps.md) under the field name "streamheader".

The buffers themselves will usually still get pushed as normal buffers, therefore it is the
sink element's responsibility to keep a copy, update the copy, and inject into the
stream the streamheader buffers. Additionally sink elements can remove buffers
that are received and are duplicates of buffers in the streamheader if it know
that receiver already has this buffer and it is appropriate to do so given the format of the stream. When an element starts sending data to
a new destination it should start by sending streamheader buffers consecutively
and before any of the data (non-HEADER) buffers they apply to.

## Examples of elements using streamheader

### Elements that can send streamheader caps:

#### Vorbis/Theora/FLAC/Speex encoders:
- vorbisenc
- theoraenc
- flacenc
- speexenc

#### Demuxers that output Vorbis/Theora/FLAC/Speex:
- oggdemux
- flvdemux
- qtdemux

#### Demuxers that output H.264 bytestream:
- asfdemux

#### Muxers that create streamable containers
- oggparse
- asfmux
- mpegtsmux
- flvmux
- qtmux

#### Vòrbis/Theora RTP payloaders
- rtptheorapay
- rtpvorbispay

### Elements that can receive streamheader caps:

#### Network sinks:
- multifdsink
- multisocketsink
- multifilesink
- shmsink
- srtsink
- rtmpsink
- rtmp2sink

#### Vorbis/Theora/FLAC/Speex/Opus decoders:
- amcaudiodec
- opusdec
- theoradec
- vorbisdec
- flacdec
- speexdec

### Elements that can receive and send streamheader caps
Those consume Vorbis/Theora/FLAC/Speex/Opus, and output a streamable container

- oggmux
- gdppay
- theoraparse
- matroskamux

