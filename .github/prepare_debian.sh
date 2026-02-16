#!/bin/bash

set -x
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive

source /etc/os-release

if [ ${ID} = "debian" ]; then
	if [ -f /etc/apt/sources.list.d/debian.sources  ]; then
		sed "s/main/non-free contrib/g" /etc/apt/sources.list.d/debian.sources > /etc/apt/sources.list.d/debian-nonfree.sources
	else
		apt-get update
		apt-get -y install software-properties-common
		apt-add-repository non-free
	fi
fi
apt-get update
apt-get -y install perl \
	autotools-dev \
	libdbi-dev \
	libldap2-dev \
	libpq-dev \
	libradcli-dev \
	libnet-snmp-perl \
	libsnmp-dev \
	procps \
	libdbi0-dev \
	libdbd-sqlite3 \
	libssl-dev \
	dnsutils \
	snmp-mibs-downloader \
	libsnmp-perl \
	snmpd \
	fping \
	snmp \
	netcat-openbsd \
	smbclient \
	vsftpd \
	apache2 \
	ssl-cert \
	postfix \
	libhttp-daemon-ssl-perl \
	libdbd-sybase-perl \
	libnet-dns-perl \
	slapd \
	ldap-utils \
	gcc \
	make \
	autoconf \
	automake \
	gettext \
	faketime \
	libmonitoring-plugin-perl \
	libcurl4-openssl-dev \
	liburiparser-dev \
	squid \
	openssh-server \
	mariadb-server \
	mariadb-client \
	libmariadb-dev \
	libmariadb-dev-compat \
	cron \
	iputils-ping \
	iproute2 \
	libjson-perl

# remove ipv6 interface from hosts
# sed '/^::1/d' /etc/hosts > /tmp/hosts
# cp -f /tmp/hosts /etc/hosts
# ip addr show
# cat /etc/hosts

# apache
a2enmod ssl
a2ensite default-ssl
# replace snakeoil certs with openssl generated ones as the make-ssl-cert ones
# seems to cause problems with our plugins
rm /etc/ssl/certs/ssl-cert-snakeoil.pem
rm /etc/ssl/private/ssl-cert-snakeoil.key
openssl req -nodes -newkey rsa:2048 -x509 -sha256 -days 365 -nodes -keyout /etc/ssl/private/ssl-cert-snakeoil.key -out /etc/ssl/certs/ssl-cert-snakeoil.pem -subj "/C=GB/ST=London/L=London/O=Global Security/OU=IT Department/CN=$(hostname)"
# add a subdomain for testing
cat >/etc/apache2/sites-available/subdomain1.conf <<EOL
<VirtualHost *:80>
    ServerName subdomain1.localhost.com
    DocumentRoot /var/www/subdomain1
    
    ErrorLog \${APACHE_LOG_DIR}/subdomain1_error.log
    CustomLog \${APACHE_LOG_DIR}/subdomain1_access.log combined
</VirtualHost>

<VirtualHost *:443>
    ServerName subdomain1.localhost.com
    DocumentRoot /var/www/subdomain1
    
    SSLEngine on
    SSLCertificateFile /etc/ssl/certs/ssl-cert-snakeoil.pem
    SSLCertificateKeyFile /etc/ssl/private/ssl-cert-snakeoil.key
    
    ErrorLog \${APACHE_LOG_DIR}/subdomain1_ssl_error.log
    CustomLog \${APACHE_LOG_DIR}/subdomain1_ssl_access.log combined
</VirtualHost>
EOL
mkdir -p /var/www/subdomain1
cat >/var/www/subdomain1/index.php <<EOL
Subdomain: subdomain1.localhost.com
EOL
echo '127.0.0.1 subdomain1.localhost' >> /etc/hosts
echo '127.0.0.1 subdomain1.localhost.com' >> /etc/hosts
apache2ctl configtest
a2ensite subdomain1.conf
service apache2 restart

# squid
cp tools/squid.conf /etc/squid/squid.conf
service squid start

# mariadb
service mariadb start || service mysql start
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
ssh -tt localhost </dev/null >/dev/null 2>/dev/null &
disown %1

# snmpd
service snmpd stop
mkdir -p /var/lib/snmp/mib_indexes
sed -e 's/^agentaddress.*/agentaddress 127.0.0.1/' -i /etc/snmp/snmpd.conf
service snmpd start

sed 's/^mibs ://' -i /etc/snmp/snmp.conf

# start cron, will be used by check_nagios
cron

# postfix
cat <<EOD >> /etc/postfix/master.cf
smtps     inet  n       -       n       -       -       smtpd
  -o smtpd_tls_wrappermode=yes
EOD
service postfix start

# start ftpd
service vsftpd start

# hostname
sed "/NP_HOST_TLS_CERT/s/.*/'NP_HOST_TLS_CERT' => '$(hostname)',/" -i /src/.github/NPTest.cache

# create some test files to lower inodes
for i in $(seq 10); do
    touch /media/ramdisk2/test.$i
done
