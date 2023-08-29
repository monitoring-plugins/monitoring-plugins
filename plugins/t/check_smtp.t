#! /usr/bin/perl -w -I ..
#
# Simple Mail Transfer Protocol (SMTP) Test via check_smtp
#
#

use strict;
use Test::More;
use NPTest;

my $host_tcp_smtp            = getTestParameter( "NP_HOST_TCP_SMTP",
					   "A host providing an SMTP Service (a mail server)", "mailhost");
my $host_tcp_smtp_starttls   = getTestParameter( "NP_HOST_TCP_SMTP_STARTTLS",
					   "A host providing SMTP with STARTTLS", $host_tcp_smtp);
my $host_tcp_smtp_nostarttls = getTestParameter( "NP_HOST_TCP_SMTP_NOSTARTTLS",
					   "A host providing SMTP without STARTTLS", "");
my $host_tcp_smtp_tls        = getTestParameter( "NP_HOST_TCP_SMTP_TLS",
					   "A host providing SMTP with TLS", $host_tcp_smtp);

my $host_nonresponsive = getTestParameter( "NP_HOST_NONRESPONSIVE", 
					   "The hostname of system not responsive to network requests", "10.0.0.1" );

my $hostname_invalid   = getTestParameter( "NP_HOSTNAME_INVALID",   
                                           "An invalid (not known to DNS) hostname", "nosuchhost" );
my $res;

plan tests => 16;

SKIP: {
	skip "No SMTP server defined", 4 unless $host_tcp_smtp;
	$res = NPTest->testCmd( "./check_smtp $host_tcp_smtp" );
	is ($res->return_code, 0, "OK");
	
	$res = NPTest->testCmd( "./check_smtp -H $host_tcp_smtp -p 25 -w 9 -c 9 -t 10 -e 220" );
	is ($res->return_code, 0, "OK, within 9 second response");

	$res = NPTest->testCmd( "./check_smtp -H $host_tcp_smtp -p 25 -wt 9 -ct 9 -to 10 -e 220" );
	is ($res->return_code, 0, "OK, old syntax");

	$res = NPTest->testCmd( "./check_smtp -H $host_tcp_smtp -e 221" );
	is ($res->return_code, 1, "WARNING - got correct error when expecting 221 instead of 220" );

	TODO: {
		local $TODO = "Output is over two lines";
		like ( $res->output, qr/^SMTP WARNING/, "Correct error message" );
	}

	$res = NPTest->testCmd( "./check_smtp -H $host_tcp_smtp --ssl -p 25" );
	is ($res->return_code, 2, "Check rc of connecting to $host_tcp_smtp with TLS on standard SMTP port" );
	like ($res->output, qr/^CRITICAL - Cannot make SSL connection\./, "Check output of connecting to $host_tcp_smtp with TLS on standard SMTP port");
}

SKIP: {
	skip "No SMTP server with STARTTLS defined", 1 unless $host_tcp_smtp_starttls;
	# SSL connection for STARTTLS
	$res = NPTest->testCmd( "./check_smtp -H $host_tcp_smtp_starttls -p 25 -S" );
	is ($res->return_code, 0, "OK, with STARTTLS" );
}

SKIP: {
	skip "No SMTP server without STARTTLS defined", 2 unless $host_tcp_smtp_nostarttls;
	$res = NPTest->testCmd( "./check_smtp -H $host_tcp_smtp_nostarttls -p 25 -S" );
	is ($res->return_code, 1, "OK, got warning from server without STARTTLS");
	is ($res->output, "WARNING - TLS not supported by server", "Right error message" );
}

SKIP: {
	skip "No SMTP server with TLS defined", 1 unless $host_tcp_smtp_tls;
	$res = NPTest->testCmd( "./check_smtp -H $host_tcp_smtp_tls --ssl" );
	is ($res->return_code, 0, "Check rc of connecting to $host_tcp_smtp_tls with TLS" );
	like ($res->output, qr/^SMTP OK - /, "Check output of connecting to $host_tcp_smtp_tls with TLS" );

	my $unused_port = 4465;
	$res = NPTest->testCmd( "./check_smtp -H $host_tcp_smtp_tls -p $unused_port --ssl" );
	is ($res->return_code, 2, "Check rc of connecting to $host_tcp_smtp_tls with TLS on unused port $unused_port" );
	like ($res->output, qr/^connect to address $host_tcp_smtp_tls and port $unused_port: Connection refused/, "Check output of connecting to $host_tcp_smtp_tls with TLS on unused port $unused_port");
}

$res = NPTest->testCmd( "./check_smtp $host_nonresponsive" );
is ($res->return_code, 2, "CRITICAL - host non responding" );

$res = NPTest->testCmd( "./check_smtp $hostname_invalid" );
is ($res->return_code, 3, "UNKNOWN - hostname invalid" );

