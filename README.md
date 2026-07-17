# GStreamer for RK3588

This repository is a downstream fork of the
[GStreamer monorepo](https://gitlab.freedesktop.org/gstreamer/gstreamer), based
on GStreamer 1.29.2. It contains integration fixes for the RK3588 VPU exposed
through the Linux stateless V4L2 Request API, together with Debian packaging
that can replace the corresponding distribution packages.

The current target is an RK3588 system running a recent mainline-derived Linux
kernel. Kernel support, device-tree configuration, firmware and access to
`/dev/media*` and `/dev/video*` are prerequisites; GStreamer cannot expose a
codec that the kernel driver does not advertise.

## RK3588 changes

The downstream changes currently include:

- corrected V4L2 media-controller topology discovery;
- preference for RKVDEC over the older Hantro decoder when both are exposed;
- runtime VP8 frame-size enumeration through `VIDIOC_ENUM_FRAMESIZES`;
- safer V4L2 multi-planar buffer handling and coded-size validation;
- `GstVideoMeta` negotiation fixes for DMA-DRM/DMABuf output and GL upload;
- decoder ranking adjustments that prefer FFmpeg over libvpx for VP8 and VP9
  software fallback on RK3588;
- a TensorFlow Lite build compatibility fix; and
- Debian-compatible runtime and development package splitting.

H.264, H.265/HEVC and AV1 stateless hardware decoding have been verified on
the target system. VP8 hardware decoding is exposed only when the kernel driver
reports usable caps. VP9 is not currently exposed by the tested kernel through
the stateless V4L2 decoder, so VP8 and VP9 normally use the FFmpeg software
fallback. This repository does not add hardware encoding support that is
missing from the kernel V4L2 API.

## Build dependencies

Install the compiler, Debian packaging tools and libraries used by the selected
GStreamer components:

```sh
sudo apt install \
  bash-completion bison build-essential cmake \
  debhelper devscripts dpkg-dev flex \
  gettext git gitlint libasound2-dev \
  libavcodec-dev libavfilter-dev libavformat-dev libavutil-dev \
  libcairo2-dev libdrm-dev libegl-dev libgbm-dev \
  libgdk-pixbuf-2.0-dev libgl-dev libgles-dev libglib2.0-dev \
  libgmp-dev libgsl-dev libgtk-3-dev libgtk-4-dev \
  libgudev-1.0-dev libjpeg-dev libnice-dev libogg-dev \
  libopus-dev liborc-0.4-dev libpango1.0-dev libpng-dev \
  libpulse-dev libsharp-dev libsoup-3.0-dev libssl-dev \
  libswresample-dev libswscale-dev libtheora-dev libudev-dev \
  libv4l-dev libvorbis-dev libvpx-dev libwayland-dev \
  libwebp-dev libx11-dev libx11-xcb-dev libxdamage-dev \
  libxext-dev libxfixes-dev libxml2-dev libxv-dev \
  meson ninja-build pkg-config pre-commit \
  python3 v4l-utils \
  zlib1g-dev
```

The authoritative dependency list is maintained in `debian/control`. Verify it
before building packages:

```sh
dpkg-checkbuilddeps
```

## Development build with Meson

The following configuration creates a focused development build containing the
RK3588 codecs, container demuxers, parsers, desktop playback elements and
command-line tools. It disables tests, examples, benchmarks, documentation and
GObject introspection.

```sh
meson setup builddir \
  -Dauto_features=auto \
  -Dbad=enabled \
  -Dbase=enabled \
  -Dbenchmarks=disabled \
  --buildtype=release \
  -Ddoc=disabled \
  -Dexamples=disabled \
  -Dgood=enabled \
  -Dgst-plugins-bad:v4l2codecs=enabled \
  -Dgst-plugins-bad:videoparsers=enabled \
  -Dgst-plugins-base:alsa=enabled \
  -Dgst-plugins-base:app=enabled \
  -Dgst-plugins-base:audioconvert=enabled \
  -Dgst-plugins-base:audioresample=enabled \
  -Dgst-plugins-base:gl=enabled \
  -Dgst-plugins-base:pango=enabled \
  -Dgst-plugins-base:playback=enabled \
  -Dgst-plugins-base:typefind=enabled \
  -Dgst-plugins-base:videoconvertscale=enabled \
  -Dgst-plugins-base:x11=enabled \
  -Dgst-plugins-good:audiofx=enabled \
  -Dgst-plugins-good:autodetect=enabled \
  -Dgst-plugins-good:deinterlace=enabled \
  -Dgst-plugins-good:gtk3=enabled \
  -Dgst-plugins-good:isomp4=enabled \
  -Dgst-plugins-good:matroska=enabled \
  -Dgst-plugins-good:pulse=enabled \
  -Dgst-plugins-good:v4l2=enabled \
  -Dgst-plugins-good:v4l2-gudev=disabled \
  -Dgst-plugins-good:v4l2-libv4l2=disabled \
  -Dgst-plugins-good:v4l2-probe=true \
  -Dgst-plugins-good:videofilter=enabled \
  -Dgst-plugins-good:vpx=enabled \
  -Dgstreamer:check=enabled \
  -Dgtk_doc=disabled \
  -Dintrospection=disabled \
  -Dlibav=enabled \
  --prefix=/usr \
  -Dtests=disabled \
  -Dtools=enabled \
  -Dugly=disabled
```

Compile the tree:

```sh
meson compile -C builddir -j "$(nproc)"
```

Use Meson's development environment to test the build without installing it:

```sh
export GST_REGISTRY="/tmp/gstreamer-rk-registry-$$.bin"

meson devenv -C builddir gst-inspect-1.0 --version
meson devenv -C builddir gst-inspect-1.0 v4l2codecs
meson devenv -C builddir gst-play-1.0 /path/to/video.mp4
```

For a clean reconfiguration, remove `builddir` or use Meson's reconfigure
operation after changing options:

```sh
meson setup --reconfigure builddir
```

## VPU device access

The user running GStreamer must be able to open both the media-controller and
video device nodes. On Debian, add the user to the `video` group and then log
out and back in:

```sh
sudo usermod -aG video "$USER"
```

Confirm access and inspect the formats exported by the kernel:

```sh
ls -l /dev/media* /dev/video*
v4l2-ctl --list-devices
media-ctl -p
```

Do not diagnose missing GStreamer elements only from `gst-inspect-1.0` cache
contents. Clear the per-user registry after changing the kernel, plugins or
device permissions:

```sh
rm -f ~/.cache/gstreamer-1.0/registry.*
```

## Building Debian packages

```sh
dpkg-checkbuilddeps
```

```sh
DEB_BUILD_OPTIONS="parallel=$(nproc) noddebs" dpkg-buildpackage -b -uc -us
```

Clean the Debian build tree and all debhelper-generated package directories:

```sh
debian/rules clean
```

## License

This downstream repository retains the licenses of the corresponding upstream
GStreamer modules. See `LICENSE` and `debian/copyright` for details.
