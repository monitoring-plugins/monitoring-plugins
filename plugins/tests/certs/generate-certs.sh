#!/bin/sh -e
#
# Recreates the https server certificates
#
# Set the GEN_EXPIRED environment variable to also regenerate
# the expired certificate.

cd "$(dirname "$0")"
trap 'rm -f *.csr; rm -f clientca-cert.srl' EXIT

subj() {
	c="DE"
	st="Bavaria"
	l="Munich"
	o="Monitoring Plugins"
	cn="Monitoring Plugins"
	emailAddress="devel@monitoring-plugins.org"

	if [ -n "$1" ]; then
		# Add to CN
		cn="$cn $1"
	fi

	printf "/C=%s/ST=%s/L=%s/O=%s/CN=%s/emailAddress=%s" \
		"$c" "$st" "$l" "$o" "$cn" "$emailAddress"
}

# server
openssl req -new -x509 -days 3560 -nodes \
	-keyout server-key.pem -out server-cert.pem \
	-subj "$(subj)"
# server, expired
# there is generally no need to regenerate this, as it will stay epxired
[ -n "$GEN_EXPIRED" ] && TZ=UTC faketime -f '2008-01-01 12:00:00' \
	openssl req -new -x509 -days 1 -nodes \
		-keyout expired-key.pem -out expired-cert.pem \
		-subj "$(subj)"

# client, ca
openssl req -new -x509 -days 3560 -nodes \
	-keyout clientca-key.pem -out clientca-cert.pem \
	-subj "$(subj ClientCA)"
echo "01" >clientca-cert.srl
# client
openssl req -new -nodes \
	-keyout client-key.pem -out client-cert.csr \
	-subj "$(subj Client)"
openssl x509 -days 3560 -req -CA clientca-cert.pem -CAkey clientca-key.pem \
	-in client-cert.csr -out client-cert.pem
# client, intermediate
openssl req -new -nodes \
	-keyout clientintermediate-key.pem -out clientintermediate-cert.csr \
	-subj "$(subj ClientIntermediate)"
openssl x509 -days 3560 -req -CA clientca-cert.pem -CAkey clientca-key.pem \
	-extfile ext.cnf -extensions client_ca \
	-in clientintermediate-cert.csr -out clientintermediate-cert.pem
# client, chain
openssl req -new -nodes \
	-keyout clientchain-key.pem -out clientchain-cert.csr \
	-subj "$(subj ClientChain)"
openssl x509 -days 3560 -req -CA clientca-cert.pem -CAkey clientca-key.pem \
	-in clientchain-cert.csr -out clientchain-cert.pem
cat clientintermediate-cert.pem >>clientchain-cert.pem
