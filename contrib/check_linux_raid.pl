#!/usr/bin/perl -w

# Copyright (c) 2002 ISOMEDIA, Inc.
# Written by Steve Milton
# Released under the GNU Public License
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#
# Usage:   check_raid <raid-name>
# Example: check_raid md0
#          WARNING md0 status=[UUU_U], recovery=46.4%, finish=123.0min

use strict;

my %ERRORS=('DEPENDENT'=>4,'UNKNOWN'=>3,'OK'=>0,'WARNING'=>1,'CRITICAL'=>2);

# die with an error if we're not on Linux
if ($^O ne 'linux') {
    print "This plugin only applicable on Linux.\n";
    exit $ERRORS{'UNKNOWN'};
}

open (MDSTAT, "</proc/mdstat") or die "Failed to open /proc/mdstat";
my $found = 0;
my $status = "";
my $recovery = "";
my $finish = "";
my $active = "";
while(<MDSTAT>) {
    if ($found) {
        if (/(\[[_U]+\])/) {
            $status = $1;
            last;
    } elsif (/recovery = (.*?)\s/) {  
            $recovery = $1;
            ($finish) = /finish=(.*?min)/;
	    last;
        }
    } else {
        if (/^$ARGV[0]\s*:/) {
            $found = 1;
            if (/active/) {
                $active = 1;
            }
        }
    }
}

my $msg = "FAILURE";
my $code = "UNKNOWN";
if ($status =~ /_/) {
    if ($recovery) {
        $msg = sprintf "%s status=%s, recovery=%s, finish=%s\n",
        $ARGV[0], $status, $recovery, $finish;
        $code = "WARNING";
    } else {
        $msg = sprintf "%s status=%s\n", $ARGV[0], $status;
        $code = "CRITICAL";
    }
} elsif ($status =~ /U+/) {
    $msg = sprintf "%s status=%s\n", $ARGV[0], $status;
    $code = "OK";
} else {
    if ($active) {
        $msg = sprintf "%s active with no status information.\n",
        $ARGV[0];
        $code = "OK";
    } else {
        $msg = sprintf "%s does not exist.\n", $ARGV[0];
        $code = "CRITICAL";
    }
}

print $code, " ", $msg;
exit ($ERRORS{$code});

