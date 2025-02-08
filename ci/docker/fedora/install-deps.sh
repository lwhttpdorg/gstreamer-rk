#! /bin/bash

set -eux

# Fedora base image disable installing documentation files. See https://pagure.io/atomic-wg/issue/308
# We need them to cleanly build our doc.
sed -i '/tsflags=nodocs/d' /etc/dnf/dnf.conf
dnf -y swap coreutils-single coreutils-full
dnf -y swap glibc-minimal-langpack glibc-all-langpacks

# Add rpm fusion repositories in order to access all of the gst plugins
dnf install -y \
  "https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm" \
  "https://mirrors.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm"

# Enable the debuginfo repos so -debug packages are kept in sync
dnf install -y dnf-plugins-core
dnf config-manager --set-enabled '*-debuginfo'

dnf upgrade -y && dnf distro-sync -y

# Build and install our own minimal opencv, the distro-provided opencv has 2GB
# of deps, including gstreamer itself
OPENCV_VERSION="4.10.0"
dnf install -y gcc-c++ zlib-devel nasm cmake ninja-build
curl -L -o opencv-$OPENCV_VERSION.tar.gz https://github.com/opencv/opencv/archive/refs/tags/$OPENCV_VERSION.tar.gz
curl -L -o opencv_contrib-$OPENCV_VERSION.tar.gz https://github.com/opencv/opencv_contrib/archive/refs/tags/$OPENCV_VERSION.tar.gz
echo "b2171af5be6b26f7a06b1229948bbb2bdaa74fcf5cd097e0af6378fce50a6eb9  opencv-4.10.0.tar.gz
65597f8fb8dc2b876c1b45b928bbcc5f772ddbaf97539bf1b737623d0604cba1  opencv_contrib-4.10.0.tar.gz" > opencv.sha256sum
sha256sum opencv.sha256sum
tar -xf opencv-$OPENCV_VERSION.tar.gz
tar -xf opencv_contrib-$OPENCV_VERSION.tar.gz
cmake -S opencv-$OPENCV_VERSION -B opencv_build -G Ninja \
  -DOPENCV_EXTRA_MODULES_PATH="opencv_contrib-$OPENCV_VERSION/modules/bgsegm;opencv_contrib-$OPENCV_VERSION/modules/plot;opencv_contrib-$OPENCV_VERSION/modules/tracking" \
  -DBUILD_opencv_calib3d=ON \
  -DBUILD_opencv_features2d=ON \
  -DBUILD_opencv_flann=ON \
  -DBUILD_opencv_imgproc=ON \
  -DBUILD_opencv_imgcodecs=ON \
  -DBUILD_opencv_objdetect=ON \
  -DBUILD_opencv_video=ON \
  -DOPENCV_GENERATE_PKGCONFIG=ON \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=OFF \
  -DBUILD_PERF_TESTS=OFF \
  -DBUILD_EXAMPLES=OFF \
  -DBUILD_DOCS=OFF \
  -DBUILD_JAVA=OFF \
  -DBUILD_PYTHON=OFF \
  -DBUILD_opencv_apps=OFF \
  -DBUILD_opencv_dnn=OFF \
  -DBUILD_opencv_gapi=OFF \
  -DBUILD_opencv_highgui=OFF \
  -DBUILD_opencv_ml=OFF \
  -DBUILD_opencv_photo=OFF \
  -DBUILD_opencv_stitching=OFF \
  -DBUILD_opencv_videoio=OFF \
  -DWITH_1394=OFF \
  -DWITH_FFMPEG=OFF \
  -DWITH_GSTREAMER=OFF \
  -DWITH_GTK=OFF \
  -DWITH_IPP=OFF \
  -DWITH_JPEG=OFF \
  -DWITH_OPENEXR=OFF \
  -DWITH_PNG=OFF \
  -DWITH_TIFF=OFF \
  -DWITH_V4L=OFF \
  -DWITH_WEBP=OFF \
  -DWITH_OPENJPEG=OFF \
  -DWITH_JASPER=OFF \
  -DWITH_IMGCODEC_PFM=OFF \
  -DWITH_IMGCODEC_PFM=OFF \
  -DWITH_IMGCODEC_SUNRASTER=OFF \
  -DWITH_PROTOBUF=OFF \
  -DWITH_FLATBUFFERS=OFF \
  -DWITH_OPENCL=OFF
ninja -C opencv_build install
rm -rf opencv*

# Install the dependencies of gstreamer
dnf install --setopt=install_weak_deps=false -y $(<./ci/docker/fedora/deps.txt)

# Install devhelp files for hotdoc. Use the rpms so we don't pull in tons of
# unnecessary deps, including gstreamer itself.
dnf download glib2-doc gtk3-devel-docs gtk4-devel-docs libsoup-doc
rpm -i --nodeps *.rpm
rm -f *.rpm

# Make sure we don't end up installing these from some transient dependency
dnf remove -y "gstreamer1*-devel" rust cargo meson 'fdk-aac-free*'

pip3 install meson==1.6.1 python-gitlab tomli junitparser bs4
pip3 install git+https://github.com/hotdoc/hotdoc.git@8c1cc997f5bc16e068710a8a8121f79ac25cbcce

# Install most debug symbols, except the big ones from things we use
debug_packages=$(rpm -qa | grep -v -i \
    -e bash \
    -e bat \
    -e bluez \
    -e boost \
    -e ccache \
    -e ceph \
    -e clang \
    -e cmake \
    -e colord \
    -e compiler-rt \
    -e cpp \
    -e cups \
    -e demos \
    -e flexiblas \
    -e flite \
    -e gcc \
    -e gcc-debuginfo \
    -e gcc-debugsource \
    -e gdal \
    -e gdb \
    -e geocode \
    -e git \
    -e glusterfs \
    -e gpg \
    -e GraphicsMagick \
    -e groff \
    -e gstreamer1 \
    -e java \
    -e leptonica \
    -e libdap \
    -e libdb \
    -e libdnf \
    -e libspatialite \
    -e llvm \
    -e lua \
    -e MUMPS \
    -e NetworkManager \
    -e nodejs \
    -e openblas \
    -e opencv \
    -e openexr \
    -e perl \
    -e poppler \
    -e qt5 \
    -e qt6 \
    -e sequoia \
    -e spice \
    -e sqlite \
    -e suitesparse \
    -e systemd \
    -e tesseract \
    -e tests \
    -e tools \
    -e tpm2 \
    -e unbound \
    -e valgrind \
    -e vim \
    -e vtk \
    -e xen \
    -e xerces \
    -e xorg \
)
dnf debuginfo-install -y --best --allowerasing --skip-broken $debug_packages

echo "Removing DNF cache"
dnf clean all

rm -rf /var/cache/dnf /var/log/dnf*
