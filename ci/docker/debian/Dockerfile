# HOWOTO:
# podman build -f ci/docker/fedora/Dockerfile -t gstf34 ci/
# toolbox create -i localhost/gstf34

FROM debian:12-slim

ENV GIT_BRANCH="main"
ENV GIT_URL="https://gitlab.freedesktop.org/gstreamer/gstreamer.git"

WORKDIR /app

COPY docker/debian/*.txt docker/debian/*.sh /app/ci/docker/debian/
COPY scripts/ /app/ci/scripts

RUN bash /app/ci/docker/debian/prepare.sh

# Split it into individual runs so it's easier to build
# COPY docker/fedora/install-deps.sh /app/ci/docker/fedora/
# COPY docker/fedora/deps.txt /app/ci/docker/fedora/
# RUN bash ./ci/docker/fedora/install-deps.sh

# COPY docker/fedora/install-gdk-pixbuf.sh /app/ci/docker/fedora/
# RUN bash ./ci/docker/fedora/install-gdk-pixbuf.sh

# COPY docker/fedora/install-wayland-protocols.sh /app/ci/docker/fedora/
# RUN bash ./ci/docker/fedora/install-wayland-protocols.sh

# COPY docker/fedora/install-rust.sh /app/ci/docker/fedora/
# RUN bash ./ci/docker/fedora/install-rust.sh

# copy scripts/ /app/ci/scripts
# COPY docker/fedora/patches/ /app/ci/docker/fedora/patches/
# COPY docker/fedora/virtme-fluster-setup.sh /app/ci/docker/fedora/
# RUN bash ./ci/docker/fedora/virtme-fluster-setup.sh

# COPY docker/fedora/create-subprojects-cache.sh /app/ci/docker/fedora/
# RUN bash ./ci/docker/fedora/create-subprojects-cache.sh
