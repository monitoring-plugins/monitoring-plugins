#!/bin/bash

set -x

set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

BASE_PATH="/src"

ls -la ${BASE_PATH}/.github/os_detect.sh
. ${BASE_PATH}/.github/os_detect.sh

SRCRPM_DIR="/tmp/result-srcrpm"
RPM_DIR="/tmp/result-rpm"
SPEC_DIR="${BASE_PATH}/.github/"
SOURCE_DIR="."
SPEC_FILE="${SPEC_DIR}monitoring-plugins.spec"

cd ${BASE_PATH}

dnf -y --setopt="tsflags=nodocs" update && \
  if [ ${distro_id} != "fedora" ]; then dnf -y --setopt="tsflags=nodocs" install epel-release; else platform_id="$(echo ${platform_id} | sed s/^f/fc/)"; fi && \
  case ${distro_id} in
    ol)
      case ${platform_id} in
        el9)
          dnf config-manager --add-repo https://copr.fedorainfracloud.org/coprs/g/mock/mock-stable/repo/epel-9/group_mock-mock-stable-epel-9.repo
          ;;
        el8)
          dnf config-manager --add-repo https://copr.fedorainfracloud.org/coprs/g/mock/mock-stable/repo/epel-8/group_mock-mock-stable-epel-8.repo
          ;;
      esac
      ;;
  esac
  dnf -y --setopt="tsflags=nodocs" install mock rpm-build git-core && \
  usermod -a -G mock $(whoami)
SRC_RPM="monitoring-plugins-*-1.${platform_id}.src.rpm"
if command -v git > /dev/null 2>&1; then
  git config --global --add safe.directory ${BASE_PATH}
  SHA="$(git rev-parse HEAD)"
  sed "s/^%global commit.*/%global commit ${SHA}/" ${SPEC_FILE} > ${SPEC_DIR}monitoring-plugins-git.spec
  sed -i "s/^%global fromgit.*/%global fromgit 1/" ${SPEC_DIR}monitoring-plugins-git.spec
  SPEC_FILE="${SPEC_DIR}monitoring-plugins-git.spec"
  SRC_RPM="monitoring-plugins-*git.$(echo ${SHA:0:7})*.${platform_id}.src.rpm"
fi
mkdir -p "${SRCRPM_DIR}" "${RPM_DIR}"
#rpmbuild --undefine=_disable_source_fetch --define "_sourcedir ${SOURCE_DIR}" -ba ${SPEC_FILE}
dnf -y --setopt="tsflags=nodocs" install rpmdevtools && spectool -g -C ${SOURCE_DIR} ${SPEC_FILE} && \
mock --dnf --clean --spec ${SPEC_FILE} --sources=${SOURCE_DIR} --result=${SRCRPM_DIR} --build || { cat ${SRCRPM_DIR}/{root,build}.log; exit 1; }
mock --dnf --clean --sources=${SOURCE_DIR} --result=${RPM_DIR} --rebuild ${SRCRPM_DIR}/${SRC_RPM} || { cat ${RPM_DIR}/{root,build}.log; exit 1; }
ls -la ${SOURCE_DIR} ${SRCRPM_DIR} ${RPM_DIR}
