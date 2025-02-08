#! /bin/bash

set -eux

apt update -y && apt full-upgrade -y
apt install -y --no-install-recommends $(<./ci/docker/debian/deps.txt)

apt remove -y rustc cargo

pip3 install --break-system-packages meson==1.6.1 hotdoc python-gitlab tomli junitparser

apt clean all
