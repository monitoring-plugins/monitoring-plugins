#! /usr/bin/perl -w -I ..
#
# Disk Space Tests via check_disk
#
#

# TODO: Add in tests for perf data. Need to beef up Nagios::Plugin::Performance to cater for max, min, etc

use strict;
use Test::More;
use NPTest;
use POSIX qw(ceil floor);

my $successOutput = '/^DISK OK/';
my $failureOutput = '/^DISK CRITICAL/';
my $warningOutput = '/^DISK WARNING/';

my $result;

my $mountpoint_valid  = getTestParameter( "NP_MOUNTPOINT_VALID", "Path to valid mountpoint",  "/");
my $mountpoint2_valid = getTestParameter( "NP_MOUNTPOINT2_VALID", "Path to another valid mountpoint. Must be different from 1st one", "/var");

if ($mountpoint_valid eq "" or $mountpoint2_valid eq "") {
	plan skip_all => "Need 2 mountpoints to test";
} else {
	plan tests => 78;
}

$result = NPTest->testCmd( 
	"./check_disk -w 1% -c 1% -p $mountpoint_valid -w 1% -c 1% -p $mountpoint2_valid" 
	);
cmp_ok( $result->return_code, "==", 0, "Checking two mountpoints (must have at least 1% free in space and inodes)");
my $c = 0;
$_ = $result->output;
$c++ while /\(/g;	# counts number of "(" - should be two
cmp_ok( $c, '==', 2, "Got two mountpoints in output");


# Get perf data
# Should use Nagios::Plugin
my @perf_data = sort(split(/ /, $result->perf_output));


# Calculate avg_free free on mountpoint1 and mountpoint2
# because if you check in the middle, you should get different errors
$_ = $result->output;
my ($free_on_mp1, $free_on_mp2) = (m/\((\d+)%.*\((\d+)%/);
die "Cannot parse output: $_" unless ($free_on_mp1 && $free_on_mp2);
my $avg_free = ceil(($free_on_mp1+$free_on_mp2)/2);
my ($more_free, $less_free);
if ($free_on_mp1 > $free_on_mp2) {
	$more_free = $mountpoint_valid;
	$less_free = $mountpoint2_valid;
} elsif ($free_on_mp1 < $free_on_mp2) {
	$more_free = $mountpoint2_valid;
	$less_free = $mountpoint_valid;
} else {
	die "Two mountpoints are the same - cannot do rest of test";
}
if($free_on_mp1 == $avg_free || $free_on_mp2 == $avg_free) {
	die "One mountpoints has average space free - cannot do rest of test";
}


# Do same for inodes
$_ = $result->output;
my ($free_inode_on_mp1, $free_inode_on_mp2) = (m/inode=(\d+)%.*inode=(\d+)%/);
die "Cannot parse free inodes: $_" unless ($free_inode_on_mp1 && $free_inode_on_mp2);
my $avg_inode_free = ceil(($free_inode_on_mp1 + $free_inode_on_mp2)/2);
my ($more_inode_free, $less_inode_free);
if ($free_inode_on_mp1 > $free_inode_on_mp2) {
	$more_inode_free = $mountpoint_valid;
	$less_inode_free = $mountpoint2_valid;
} elsif ($free_inode_on_mp1 < $free_inode_on_mp2) {
	$more_inode_free = $mountpoint2_valid;
	$less_inode_free = $mountpoint_valid;
} else {
	die "Two mountpoints with same inodes free - cannot do rest of test";
}
if($free_inode_on_mp1 == $avg_inode_free || $free_inode_on_mp2 == $avg_inode_free) {
	die "One mountpoints has average inodes free - cannot do rest of test";
}

# Verify performance data
# First check absolute thresholds...
$result = NPTest->testCmd(
        "./check_disk -w 20 -c 10 -p $mountpoint_valid"
        );
$_ = $result->perf_output;
my ($warn_absth_data, $crit_absth_data, $total_absth_data) = (m/=.[^;]*;(\d+);(\d+);\d+;(\d+)/);
is ($warn_absth_data, $total_absth_data - 20, "Wrong warning in perf data using absolute thresholds");
is ($crit_absth_data, $total_absth_data - 10, "Wrong critical in perf data using absolute thresholds");

# Then check percent thresholds.
$result = NPTest->testCmd(
        "./check_disk -w 20% -c 10% -p $mountpoint_valid"
        );
$_ = $result->perf_output;
my ($warn_percth_data, $crit_percth_data, $total_percth_data) = (m/=.[^;]*;(\d+);(\d+);\d+;(\d+)/);
is ($warn_percth_data, int((1-20/100)*$total_percth_data), "Wrong warning in perf data using percent thresholds");
is ($crit_percth_data, int((1-10/100)*$total_percth_data), "Wrong critical in perf data using percent thresholds");


# Check when order of mount points are reversed, that perf data remains same
$result = NPTest->testCmd( 
	"./check_disk -w 1% -c 1% -p $mountpoint2_valid -w 1% -c 1% -p $mountpoint_valid" 
	);
@_ = sort(split(/ /, $result->perf_output));
is_deeply( \@perf_data, \@_, "perf data for both filesystems same when reversed");


# Basic filesystem checks for sizes
$result = NPTest->testCmd( "./check_disk -w 1 -c 1 -p $more_free" );
cmp_ok( $result->return_code, '==', 0, "At least 1 MB available on $more_free");
like  ( $result->output, $successOutput, "OK output" );
like  ( $result->only_output, qr/free space/, "Have free space text");
like  ( $result->only_output, qr/$more_free/, "Have disk name in text");

$result = NPTest->testCmd( "./check_disk -w 1 -c 1 -p $more_free -p $less_free" );
cmp_ok( $result->return_code, '==', 0, "At least 1 MB available on $more_free and $less_free");
$_ = $result->output;
my ($free_mb_on_mp1, $free_mb_on_mp2) = (m/(\d+) MB .* (\d+) MB /g);
my $free_mb_on_all = $free_mb_on_mp1 + $free_mb_on_mp2;



$result = NPTest->testCmd( "./check_disk -e -w 1 -c 1 -p $more_free" );
is( $result->only_output, "DISK OK", "No print out of disks with -e for OKs");

$result = NPTest->testCmd( "./check_disk 100 100 $more_free" );
cmp_ok( $result->return_code, '==', 0, "Old syntax okay" );

$result = NPTest->testCmd( "./check_disk -w 1% -c 1% -p $more_free" );
cmp_ok( $result->return_code, "==", 0, "At least 1% free" );

$result = NPTest->testCmd( 
	"./check_disk -w 1% -c 1% -p $more_free -w 100% -c 100% -p $less_free" 
	);
cmp_ok( $result->return_code, "==", 2, "Get critical on less_free mountpoint $less_free" );
like( $result->output, $failureOutput, "Right output" );


$result = NPTest->testCmd(
	"./check_disk -w $avg_free% -c 0% -p $less_free"
	);
cmp_ok( $result->return_code, '==', 1, "Get warning on less_free mountpoint, when checking avg_free");

$result = NPTest->testCmd(
	"./check_disk -w $avg_free% -c $avg_free% -p $more_free"
	);
cmp_ok( $result->return_code, '==', 0, "Get ok on more_free mountpoint, when checking avg_free");

$result = NPTest->testCmd( 
	"./check_disk -w $avg_free% -c 0% -p $less_free -w $avg_free% -c $avg_free% -p $more_free" 
	);
cmp_ok( $result->return_code, "==", 1, "Combining above two tests, get warning");
my $all_disks = $result->output;

$result = NPTest->testCmd(
	"./check_disk -e -w $avg_free% -c 0% -p $less_free -w $avg_free% -c $avg_free% -p $more_free" 
	);
isnt( $result->output, $all_disks, "-e gives different output");

# Need spaces around filesystem name in case less_free and more_free are nested
like( $result->output, qr/ $less_free /, "Found problem $less_free");
unlike( $result->only_output, qr/ $more_free /, "Has ignored $more_free as not a problem");
like( $result->perf_output, qr/ $more_free=/, "But $more_free is still in perf data");

$result = NPTest->testCmd(
	"./check_disk -w $avg_free% -c 0% -p $more_free"
	);
cmp_ok( $result->return_code, '==', 0, "Get ok on more_free mountpoint, checking avg_free");

$result = NPTest->testCmd(
	"./check_disk -w $avg_free% -c $avg_free% -p $less_free"
	);
cmp_ok( $result->return_code, '==', 2, "Get critical on less_free, checking avg_free");
$result = NPTest->testCmd(
	"./check_disk -w $avg_free% -c 0% -p $more_free -w $avg_free% -c $avg_free% -p $less_free"
	);
cmp_ok( $result->return_code, '==', 2, "Combining above two tests, get critical");

$result = NPTest->testCmd(
	"./check_disk -w $avg_free% -c $avg_free% -p $less_free -w $avg_free% -c 0% -p $more_free"
	);
cmp_ok( $result->return_code, '==', 2, "And reversing arguments should not make a difference");



# Basic inode checks for sizes

$result = NPTest->testCmd( "./check_disk --icritical 1% --iwarning 1% -p $more_inode_free" );
is( $result->return_code, 0, "At least 1% free on inodes for both mountpoints");

$result = NPTest->testCmd( "./check_disk -K 100% -W 100% -p $less_inode_free" );
is( $result->return_code, 2, "Critical requesting 100% free inodes for both mountpoints");

$result = NPTest->testCmd( "./check_disk --iwarning 1% --icritical 1% -p $more_inode_free -K 100% -W 100% -p $less_inode_free" );
is( $result->return_code, 2, "Get critical on less_inode_free mountpoint $less_inode_free");

$result = NPTest->testCmd( "./check_disk -W $avg_inode_free% -K 0% -p $less_inode_free" );
is( $result->return_code, 1, "Get warning on less_inode_free, when checking average");

$result = NPTest->testCmd( "./check_disk -W $avg_inode_free% -K $avg_inode_free% -p $more_inode_free ");
is( $result->return_code, 0, "Get ok on more_inode_free when checking average");

$result = NPTest->testCmd( "./check_disk -W $avg_inode_free% -K 0% -p $less_inode_free -W $avg_inode_free% -K $avg_inode_free% -p $more_inode_free" );
is ($result->return_code, 1, "Combine above two tests, get warning");
$all_disks = $result->output;

$result = NPTest->testCmd( "./check_disk -e -W $avg_inode_free% -K 0% -p $less_inode_free -W $avg_inode_free% -K $avg_inode_free% -p $more_inode_free" );
isnt( $result->output, $all_disks, "-e gives different output");
like( $result->output, qr/$less_inode_free/, "Found problem $less_inode_free");
unlike( $result->only_output, qr/$more_inode_free\s/, "Has ignored $more_inode_free as not a problem");
like( $result->perf_output, qr/$more_inode_free/, "But $more_inode_free is still in perf data");

$result = NPTest->testCmd( "./check_disk -W $avg_inode_free% -K 0% -p $more_inode_free" );
is( $result->return_code, 0, "Get ok on more_inode_free mountpoint, checking average");

$result = NPTest->testCmd( "./check_disk -W $avg_inode_free% -K $avg_inode_free% -p $less_inode_free" );
is( $result->return_code, 2, "Get critical on less_inode_free, checking average");

$result = NPTest->testCmd( "./check_disk -W $avg_inode_free% -K 0% -p $more_inode_free -W $avg_inode_free% -K $avg_inode_free% -p $less_inode_free" );
is( $result->return_code, 2, "Combining above two tests, get critical");

$result = NPTest->testCmd( "./check_disk -W $avg_inode_free% -K $avg_inode_free% -p $less_inode_free -W $avg_inode_free% -K 0% -p $more_inode_free" );
cmp_ok( $result->return_code, '==', 2, "And reversing arguments should not make a difference");






TODO: {
	local $TODO = "Invalid percent figures";
	$result = NPTest->testCmd(
		"./check_disk -w 10% -c 15% -p $mountpoint_valid"
		);
	cmp_ok( $result->return_code, '==', 3, "Invalid command line options" );
}

$result = NPTest->testCmd( 
	"./check_disk -p $mountpoint_valid -w 10% -c 15%"
	);
cmp_ok( $result->return_code, "==", 3, "Invalid options: -p must come after thresholds" );

$result = NPTest->testCmd( "./check_disk -w 100% -c 100% ".${mountpoint_valid} );      # 100% empty
cmp_ok( $result->return_code, "==", 2, "100% empty" );
like( $result->output, $failureOutput, "Right output" );

$result = NPTest->testCmd( "./check_disk -w 100000 -c 100000 $mountpoint_valid" );
cmp_ok( $result->return_code, '==', 2, "Check for 100GB free" );

$result = NPTest->testCmd( "./check_disk -w 100 -c 100 -u GB ".${mountpoint_valid} );      # 100 GB empty
cmp_ok( $result->return_code, "==", 2, "100 GB empty" );


# Checking old syntax of check_disk warn crit [fs], with warn/crit at USED% thresholds
$result = NPTest->testCmd( "./check_disk 0 0 ".${mountpoint_valid} );
cmp_ok( $result->return_code, "==", 2, "Old syntax: 0% used");
like ( $result->only_output, qr(^[^;]*;[^;]*$), "Select only one path with positional arguments");

$result = NPTest->testCmd( "./check_disk 100 100 $mountpoint_valid" );
cmp_ok( $result->return_code, '==', 0, "Old syntax: 100% used" );

$result = NPTest->testCmd( "./check_disk 0 100 $mountpoint_valid" );
cmp_ok( $result->return_code, '==', 1, "Old syntax: warn 0% used" );

TODO: {
	local $TODO = "Invalid values";
	$result = NPTest->testCmd( "./check_disk 0 200 $mountpoint_valid" );
	cmp_ok( $result->return_code, '==', 3, "Old syntax: Error with values outside percent range" );

	$result = NPTest->testCmd( "./check_disk 200 200 $mountpoint_valid" );
	cmp_ok( $result->return_code, '==', 3, "Old syntax: Error with values outside percent range" );

	$result = NPTest->testCmd( "./check_disk 200 0 $mountpoint_valid" );
	cmp_ok( $result->return_code, '==', 3, "Old syntax: Error with values outside percent range" );
}

$result = NPTest->testCmd( "./check_disk -w 0% -c 0% -p /bob" );
cmp_ok( $result->return_code, '==', 2, "Checking /bob - return error because /bob does not exist" );
like( $result->output, '/^DISK CRITICAL - /bob is not accessible:.*$/', 'Output OK');

$result = NPTest->testCmd( "./check_disk -w 0% -c 0% -p /" );
my $root_output = $result->output;

$result = NPTest->testCmd( "./check_disk -w 0% -c 0% -p /etc" );
cmp_ok( $result->return_code, '==', 0, "Checking /etc - should return info for /" );
cmp_ok( $result->output, 'eq', $root_output, "check_disk /etc gives same as check_disk /");

$result = NPTest->testCmd( "./check_disk -w 0% -c 0% -E -p /etc " );
cmp_ok( $result->return_code, '==', 2, "... unless -E/--exact-match is specified");

$result = NPTest->testCmd( "./check_disk -w 0% -c 0% -p /etc -E " );
cmp_ok( $result->return_code, '==', 3, "-E/--exact-match must be specified before -p");

$result = NPTest->testCmd( "./check_disk -w 0% -c 0% -r /etc -E " );
cmp_ok( $result->return_code, '==', 3, "-E/--exact-match must be specified before -r");

$result = NPTest->testCmd( "./check_disk -w 0% -c 0% -p / -p /bob" );
cmp_ok( $result->return_code, '==', 2, "Checking / and /bob gives critical");
unlike( $result->perf_output, '/\/bob/', "perf data does not have /bob in it");

$result = NPTest->testCmd( "./check_disk -w 0% -c 0% -p / -p /" );
unlike( $result->output, '/ \/ .* \/ /', "Should not show same filesystem twice");

# are partitions added if -C is given without path selection -p ?
$result = NPTest->testCmd( "./check_disk -w 0% -c 0% -C -w 0% -c 0% -p $mountpoint_valid" );
like( $result->output, '/;.*;\|/', "-C selects partitions if -p is not given");

# grouping: exit crit if the sum of free megs on mp1+mp2 is less than warn/crit
$result = NPTest->testCmd( "./check_disk -w ". ($free_mb_on_all + 1) ." -c ". ($free_mb_on_all + 1) ."-g group -p $mountpoint_valid -p $mountpoint2_valid" );
cmp_ok( $result->return_code, '==', 2, "grouping: exit crit if the sum of free megs on mp1+mp2 is less than warn/crit");

# grouping: exit warning if the sum of free megs on mp1+mp2 is between -w and -c
$result = NPTest->testCmd( "./check_disk -w ". ($free_mb_on_all + 1) ." -c ". ($free_mb_on_all - 1) ." -g group -p $mountpoint_valid -p $mountpoint2_valid" );
cmp_ok( $result->return_code, '==', 1, "grouping: exit warning if the sum of free megs on mp1+mp2 is between -w and -c ");

# grouping: exit ok if the sum of free megs on mp1+mp2 is more than warn/crit
$result = NPTest->testCmd( "./check_disk -w ". ($free_mb_on_all - 1) ." -c ". ($free_mb_on_all - 1) ." -g group -p $mountpoint_valid -p $mountpoint2_valid" );
cmp_ok( $result->return_code, '==', 0, "grouping: exit ok if the sum of free megs on mp1+mp2 is more than warn/crit");

# grouping: exit unknown if group name is given after -p 
$result = NPTest->testCmd( "./check_disk -w ". ($free_mb_on_all - 1) ." -c ". ($free_mb_on_all - 1) ." -p $mountpoint_valid -g group -p $mountpoint2_valid" );
cmp_ok( $result->return_code, '==', 3, "Invalid options: -p must come after groupname");

# regex: exit unknown if given regex is not compileable
$result = NPTest->testCmd( "./check_disk -w 1 -c 1 -r '('" );
cmp_ok( $result->return_code, '==', 3, "Exit UNKNOWN if regex is not compileable");

# ignore: exit unknown, if all pathes are deselected using -i
$result = NPTest->testCmd( "./check_disk -w 0% -c 0% -p $mountpoint_valid -p $mountpoint2_valid -i '$mountpoint_valid' -i '$mountpoint2_valid'" );
cmp_ok( $result->return_code, '==', 3, "ignore-ereg: Unknown if all fs are ignored (case sensitive)");

# ignore: exit unknown, if all pathes are deselected using -I
$result = NPTest->testCmd( "./check_disk -w 0% -c 0% -p $mountpoint_valid -p $mountpoint2_valid -I '".uc($mountpoint_valid)."' -I '".uc($mountpoint2_valid)."'" );
cmp_ok( $result->return_code, '==', 3, "ignore-ereg: Unknown if all fs are ignored (case insensitive)");

# ignore: exit unknown, if all pathes are deselected using -i
$result = NPTest->testCmd( "./check_disk -w 0% -c 0% -p $mountpoint_valid -p $mountpoint2_valid -i '.*'" );
cmp_ok( $result->return_code, '==', 3, "ignore-ereg: Unknown if all fs are ignored using -i '.*'");

# ignore: test if ignored path is actually ignored
$result = NPTest->testCmd( "./check_disk -w 0% -c 0% -p $mountpoint_valid -p $mountpoint2_valid -i '^$mountpoint2_valid\$'");
like( $result->output, qr/$mountpoint_valid/, "output data does have $mountpoint_valid in it");
unlike( $result->output, qr/$mountpoint2_valid/, "output data does not have $mountpoint2_valid in it");

# ignore: test if all pathes are listed when ignore regex doesn't match
$result = NPTest->testCmd( "./check_disk -w 0% -c 0% -p $mountpoint_valid -p $mountpoint2_valid -i '^barbazJodsf\$'");
like( $result->output, qr/$mountpoint_valid/, "ignore: output data does have $mountpoint_valid when regex doesn't match");
like( $result->output, qr/$mountpoint2_valid/,"ignore: output data does have $mountpoint2_valid when regex doesn't match");
