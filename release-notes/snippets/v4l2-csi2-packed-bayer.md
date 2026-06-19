## V4L2: CSI-2 packed Bayer format support

Added support for CSI-2 packed Bayer formats (10-bit, 12-bit, 14-bit) in the
V4L2 plugin, enabling capture of packed raw Bayer data from camera sensors.
The new format strings use the `{pattern}{bits}p` naming convention (e.g.
`bggr10p`, `rggb12p`) to distinguish them from the existing unpacked formats.

This is needed to get multistream support working for libcamera via GStreamer
on mobile phones, where camera sensors typically output CSI-2 packed Bayer
data that must be distinguishable from unpacked formats.
