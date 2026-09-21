#!/bin/bash

# Run inside a disposable archlinux:latest container, with the checkout at /src
# and a writable artifact directory at /artifacts.
set -euo pipefail
export LC_ALL=C

readonly packaging_commit=b12482ed7115ced608fc23d878b9fe6d148df9c0

# The container image has trusted public keys but may not have a local key for
# the keyring package's update hook.
pacman-key --init
pacman -Syu --noconfirm --needed base-devel git sudo
useradd --create-home builder
printf 'builder ALL=(root) NOPASSWD: /usr/bin/pacman\n' > /etc/sudoers.d/builder
chmod 440 /etc/sudoers.d/builder
install -d -o builder -g builder /build /artifacts
trap 'pacman -Q > /artifacts/build-packages.log' EXIT

upstream_commit=$(git -c safe.directory=/src -C /src rev-parse HEAD)
readonly upstream_commit
# Preserve the test merge and tags, without writing to the mounted checkout or
# relying on its Git configuration/credentials during makepkg's source fetch.
git -c safe.directory=/src clone --bare --no-local /src /build/upstream.git
# Forks may not carry the release tags required by the AUR recipe's
# _upstream_version(). Fetch them into the disposable repository only; the
# source below remains pinned to the checkout's commit.
git -C /build/upstream.git fetch --no-tags \
  https://github.com/monitoring-plugins/monitoring-plugins.git \
  'refs/tags/v*:refs/tags/v*'

git clone --single-branch --no-tags --branch monitoring-plugins-git \
  https://github.com/archlinux/aur.git /build/packaging
git -C /build/packaging checkout --detach "$packaging_commit"
test "$(git -C /build/packaging rev-parse HEAD)" = "$packaging_commit"

cd /build/packaging
# Fail if the pinned recipe no longer has the expected single Git source.
# shellcheck disable=SC2016 # Match the literal variable in the PKGBUILD.
readonly original_source='source=("${_pkgname}::git+https://github.com/monitoring-plugins/monitoring-plugins.git")'
test "$(grep -Fxc "$original_source" PKGBUILD)" = 1
sed -i "s|^source=.*|source=(\"monitoring-plugins::git+file:///build/upstream.git#commit=$upstream_commit\")|" PKGBUILD
chown -R builder:builder /build

{
  printf 'Upstream commit: %s\nPackaging commit: %s\n' "$upstream_commit" "$packaging_commit"
  cat /etc/os-release /etc/makepkg.conf
  if [[ -d /etc/makepkg.conf.d ]]; then
    find /etc/makepkg.conf.d -type f -name '*.conf' -print -exec cat {} \;
  fi
  git -c safe.directory=/build/packaging diff -- PKGBUILD
} | tee /artifacts/build-environment.log

# makepkg retains its own logs even when check() or package() fails.
sudo -H -u builder env PKGDEST=/artifacts LOGDEST=/artifacts \
  makepkg --syncdeps --noconfirm --cleanbuild --check --log

test "$(git -c safe.directory=/build/packaging/src/monitoring-plugins \
  -C src/monitoring-plugins rev-parse HEAD)" = "$upstream_commit"

# Select the main package by metadata, excluding a possible debug package.
main_package=
for package in /artifacts/*.pkg.tar.zst; do
  read -r package_name _ < <(pacman -Qp "$package")
  if [[ "$package_name" == monitoring-plugins-git ]]; then
    main_package=$package
    break
  fi
done
test -n "$main_package"
pacman -U --noconfirm "$main_package"
pacman -Q monitoring-plugins-git
