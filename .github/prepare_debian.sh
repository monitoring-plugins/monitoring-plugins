#!/bin/bash

set -x
set -e

export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get -y install software-properties-common
if [ $(lsb_release -is) = "Debian" ]; then
  apt-add-repository non-free
  apt-get update
fi
apt-get -y install perl autotools-dev libdbi-dev libldap2-dev libpq-dev libradcli-dev libnet-snmp-perl procps
apt-get -y install libdbi0-dev libdbd-sqlite3 libssl-dev dnsutils snmp-mibs-downloader libsnmp-perl snmpd
apt-get -y install fping snmp netcat smbclient vsftpd apache2 ssl-cert postfix libhttp-daemon-ssl-perl
apt-get -y install libdbd-sybase-perl libnet-dns-perl
apt-get -y install slapd ldap-utils
apt-get -y install gcc make autoconf automake gettext
apt-get -y install faketime
apt-get -y install libmonitoring-plugin-perl
apt-get -y install libcurl4-openssl-dev
apt-get -y install liburiparser-dev
apt-get -y install squid
apt-get -y install openssh-server
apt-get -y install mariadb-server mariadb-client libmariadb-dev
apt-get -y install cron iputils-ping
apt-get -y install iproute2

# remove ipv6 interface from hosts
if [ $(ip addr show | grep "inet6 ::1" | wc -l) -eq "0" ]; then
    sed '/^::1/d' /etc/hosts > /tmp/hosts
    cp -f /tmp/hosts /etc/hosts
fi

ip addr show

cat /etc/hosts


# apache
a2enmod ssl
a2ensite default-ssl
make-ssl-cert generate-default-snakeoil --force-overwrite
service apache2 start

# squid
cp tools/squid.conf /etc/squid/squid.conf
service squid start

# mariadb
service mariadb start
mysql -e "create database IF NOT EXISTS test;" -uroot

# ldap
sed -e 's/cn=admin,dc=nodomain/'$(/usr/sbin/slapcat|grep ^dn:|awk '{print $2}')'/' -i .github/NPTest.cache
service slapd start

# sshd
ssh-keygen -t rsa -N "" -f ~/.ssh/id_rsa
cat ~/.ssh/id_rsa.pub >> ~/.ssh/authorized_keys
service ssh start
sleep 1
ssh-keyscan localhost >> ~/.ssh/known_hosts
touch ~/.ssh/config

# start one login session, required for check_users
( ssh -n -tt root@localhost "top" < /dev/null >/dev/null 2>&1 & )
sleep 1
who
ssh root@localhost "top -b -n 1"

# snmpd
for DIR in /usr/share/snmp/mibs /usr/share/mibs; do
    rm -f $DIR/ietf/SNMPv2-PDU \
          $DIR/ietf/IPSEC-SPD-MIB \
          $DIR/ietf/IPATM-IPMC-MIB \
          $DIR/iana/IANA-IPPM-METRICS-REGISTRY-MIB
done
mkdir -p /var/lib/snmp/mib_indexes
sed -e 's/^agentaddress.*/agentaddress 127.0.0.1/' -i /etc/snmp/snmpd.conf
service snmpd start

# start cron, will be used by check_nagios
cron

# start postfix
service postfix start

# start ftpd
service vsftpd start

# hostname
sed "/NP_HOST_TLS_CERT/s/.*/'NP_HOST_TLS_CERT' => '$(hostname)',/" -i /src/.github/NPTest.cache

# create some test files to lower inodes
for i in $(seq 10); do
    touch /media/ramdisk2/test.$1
done
