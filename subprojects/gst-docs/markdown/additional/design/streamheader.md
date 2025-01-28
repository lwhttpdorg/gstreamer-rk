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
embedded inside caps. More specifically these buffers are stored in a [value](https://docs.gtk.org/gobject/struct.Value.html)
type `GST_BUFFER` or `GST_TYPE_ARRAY`. This value is itself added to
[caps](caps.md) under the field name "streamheader".

The buffers themselves still get pushed as normal buffers, therefore it is the
sink's elements responsibility to keep a copy, update the copy,  inject into the
stream the streamheader buffers. Additionally sink elements can remove buffers
that are received and are duplicates of buffers in the streamheader if it know
that receiver already has this buffer. When an element start sending data to
a new destination it should start by sending streamheader buffers consecutively
and before any of the data (non-HEADER) buffers they apply to. If necessary, the
element should internally queue non-HEADER buffers until it received the
streamheaders.
