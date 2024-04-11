# HOWOTO:
# podman build -f ci/docker/fedora/Dockerfile -t gstf40 ci/
# toolbox create -i localhost/gstf40

# FROM fedora:39
FROM registry.fedoraproject.org/fedora-toolbox:41
# FROM registry.fedoraproject.org/fedora-toolbox:34

ENV GIT_BRANCH="main"
ENV GIT_URL="https://gitlab.freedesktop.org/gstreamer/gstreamer.git"

WORKDIR /app

COPY docker/fedora/*.txt docker/fedora/*.sh /app/ci/docker/fedora/
COPY scripts/ /app/ci/scripts
COPY docker/fedora/patches/ /app/ci/docker/fedora/patches/

RUN bash /app/ci/docker/fedora/prepare.sh

# Split it into individual runs so it's easier to build
# COPY docker/fedora/install-deps.sh /app/ci/docker/fedora/
# COPY docker/fedora/deps.txt /app/ci/docker/fedora/
# RUN bash ./ci/docker/fedora/install-deps.sh

# COPY docker/fedora/install-gdk-pixbuf.sh /app/ci/docker/fedora/
# RUN bash ./ci/docker/fedora/install-gdk-pixbuf.sh

# COPY docker/fedora/install-wayland-protocols.sh /app/ci/docker/fedora/
# RUN bash ./ci/docker/fedora/install-wayland-protocols.sh

# COPY docker/fedora/install-rust.sh /app/ci/docker/fedora/
# RUN bash ./ci/scripts/install-rust.sh

# copy scripts/ /app/ci/scripts
# COPY docker/fedora/patches/ /app/ci/docker/fedora/patches/
# COPY docker/fedora/virtme-fluster-setup.sh /app/ci/docker/fedora/
# RUN bash ./ci/docker/fedora/virtme-fluster-setup.sh

# COPY docker/fedora/create-subprojects-cache.sh /app/ci/docker/fedora/
# RUN bash ./ci/scripts/create-subprojects-cache.sh
