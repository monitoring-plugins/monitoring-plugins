#!/bin/sh -e
# workaround for really bare-bones Archlinux containers:
if [ -x "$(command -v pacman)" ]; then
    pacman --noconfirm -Sy
    pacman --noconfirm -S grep gawk sed
fi

os_release_file=
if [ -s "/etc/os-release" ]; then
  os_release_file="/etc/os-release"
elif [ -s "/usr/lib/os-release" ]; then
  os_release_file="/usr/lib/os-release"
else
  echo >&2 "Cannot find an os-release file ..."
  return 1
fi
export distro_id=$(grep '^ID=' $os_release_file|awk -F = '{print $2}'|sed 's/\"//g')
export platform_id=$(grep '^PLATFORM_ID=' /etc/os-release|awk -F = '{print $2}'|sed 's/\"//g'| cut -d":" -f2)
