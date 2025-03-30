#! /usr/bin/perl -w -I ..
#
# Disk Space Tests via check_disk
#
#

# TODO: Add in tests for perf data. Need to beef up Monitoring::Plugin::Performance to cater for max, min, etc

use strict;
use Test::More;
use NPTest;
use POSIX qw(ceil floor);
use Data::Dumper;

my $successOutput = '/^DISK OK/';
my $failureOutput = '/^DISK CRITICAL/';
my $warningOutput = '/^DISK WARNING/';

my $result;

my $mountpoint_valid  = getTestParameter( "NP_MOUNTPOINT_VALID", "Path to valid mountpoint",  "/");
my $mountpoint2_valid = getTestParameter( "NP_MOUNTPOINT2_VALID", "Path to another valid mountpoint. Must be different from 1st one", "/var");

my $output_format = "--output-format mp-test-json";

if ($mountpoint_valid eq "" or $mountpoint2_valid eq "") {
	plan skip_all => "Need 2 mountpoints to test";
} else {
	plan tests => 96;
}

$result = NPTest->testCmd(
	"./check_disk -w 1% -c 1% -p $mountpoint_valid -w 1% -c 1% -P -p $mountpoint2_valid $output_format"
	);
cmp_ok( $result->return_code, "==", 0, "Checking two mountpoints (must have at least 1% free in space and inodes)");

like($result->{'mp_test_result'}->{'state'}, "/OK/", "Main result is OK");
like($result->{'mp_test_result'}->{'checks'}->[0]->{'state'}, "/OK/", "First sub result is OK");
like($result->{'mp_test_result'}->{'checks'}->[1]->{'state'}, "/OK/", "Second sub result is OK");

my $absolut_space_mp1 = $result->{'mp_test_result'}->{'checks'}->[1]->{'checks'}->[0]->{'perfdata'}->[0]->{'max'}->{'value'};
# print("absolute space on mp1: ". $absolut_space_mp1 . "\n");

my $free_percent_on_mp1 = ($result->{'mp_test_result'}->{'checks'}->[1]->{'checks'}->[0]->{'perfdata'}->[0]->{'value'}->{'value'} / ($absolut_space_mp1/100));
print("free percent on mp1: ". $free_percent_on_mp1 . "\n");

my $absolut_space_mp2 = $result->{'mp_test_result'}->{'checks'}->[0]->{'checks'}->[0]->{'perfdata'}->[0]->{'max'}->{'value'};
# print("absolute space on mp2: ". $absolut_space_mp2 . "\n");

my $free_percent_on_mp2 = ($result->{'mp_test_result'}->{'checks'}->[0]->{'checks'}->[0]->{'perfdata'}->[0]->{'value'}->{'value'}/ ($absolut_space_mp2/100));
print("free percent on mp2: ". $free_percent_on_mp2 . "\n");

my @perfdata;
@perfdata[0] = $result->{'mp_test_result'}->{'checks'}->[0]->{'checks'}->[0]->{'perfdata'}->[0];
@perfdata[1] = $result->{'mp_test_result'}->{'checks'}->[1]->{'checks'}->[0]->{'perfdata'}->[0];

# Calculate avg_free free on mountpoint1 and mountpoint2
# because if you check in the middle, you should get different errors
my $avg_free_percent = ceil(($free_percent_on_mp1+$free_percent_on_mp2)/2);
# print("avg_free: " . $avg_free_percent . "\n");
my ($more_free, $less_free);
if ($free_percent_on_mp1 > $free_percent_on_mp2) {
	$more_free = $mountpoint_valid;
	$less_free = $mountpoint2_valid;
} elsif ($free_percent_on_mp1 < $free_percent_on_mp2) {
	$more_free = $mountpoint2_valid;
	$less_free = $mountpoint_valid;
} else {
	die "Two mountpoints are the same - cannot do rest of test";
}

print("less free: " . $less_free . "\n");
print("more free: " . $more_free . "\n");

if($free_percent_on_mp1 == $avg_free_percent || $free_percent_on_mp2 == $avg_free_percent) {
	die "One mountpoints has average space free - cannot do rest of test";
}

my $free_inodes_on_mp1 = $result->{'mp_test_result'}->{'checks'}->[1]->{'checks'}[2]->{'perfdata'}->[0]->{'value'}->{'value'};
my $total_inodes_on_mp1 = $result->{'mp_test_result'}->{'checks'}->[1]->{'checks'}[2]->{'perfdata'}->[0]->{'max'}->{'value'};
my $free_inode_percentage_on_mp1 = $free_inodes_on_mp1 / ($total_inodes_on_mp1 / 100);

my $free_inodes_on_mp2 = $result->{'mp_test_result'}->{'checks'}->[0]->{'checks'}[2]->{'perfdata'}->[0]->{'value'}->{'value'};
my $total_inodes_on_mp2 = $result->{'mp_test_result'}->{'checks'}->[0]->{'checks'}[2]->{'perfdata'}->[0]->{'max'}->{'value'};
my $free_inode_percentage_on_mp2 = $free_inodes_on_mp2 / ($total_inodes_on_mp2 / 100);

my $avg_inode_free_percentage = ceil(($free_inode_percentage_on_mp1 + $free_inode_percentage_on_mp2)/2);
my ($more_inode_free, $less_inode_free);
if ($free_inode_percentage_on_mp1 > $free_inode_percentage_on_mp2) {
	$more_inode_free = $mountpoint_valid;
	$less_inode_free = $mountpoint2_valid;
} elsif ($free_inode_percentage_on_mp1 < $free_inode_percentage_on_mp2) {
	$more_inode_free = $mountpoint2_valid;
	$less_inode_free = $mountpoint_valid;
} else {
	die "Two mountpoints with same inodes free - cannot do rest of test";
}
if($free_inode_percentage_on_mp1 == $avg_inode_free_percentage || $free_inode_percentage_on_mp2 == $avg_inode_free_percentage) {
	die "One mountpoints has average inodes free - cannot do rest of test";
}

# Verify performance data
# First check absolute thresholds...
$result = NPTest->testCmd(
        "./check_disk -w 20 -c 10 -p $mountpoint_valid $output_format"
        );

cmp_ok( $result->return_code, "==", 0, "with JSON test format result should always be OK");

my $warn_absth_data = $result->{'mp_test_result'}->{'checks'}->[0]->{'checks'}[0]->{'perfdata'}->[0]->{'warn'}->{'end'}->{'value'};
my $crit_absth_data = $result->{'mp_test_result'}->{'checks'}->[0]->{'checks'}[0]->{'perfdata'}->[0]->{'crit'}->{'end'}->{'value'};
my $total_absth_data= $result->{'mp_test_result'}->{'checks'}->[0]->{'checks'}[0]->{'perfdata'}->[0]->{'max'}->{'value'};

# print("warn: " .$warn_absth_data . "\n");
# print("crit: " .$crit_absth_data . "\n");
# print("total: " .$total_absth_data . "\n");

is ($warn_absth_data <=>  (20 * (2 ** 20)), 0, "Wrong warning in perf data using absolute thresholds");
is ($crit_absth_data <=>  (10 * (2 ** 20)), 0, "Wrong critical in perf data using absolute thresholds");

# Then check percent thresholds.
$result = NPTest->testCmd(
        "./check_disk -w 20% -c 10% -p $mountpoint_valid $output_format"
        );

cmp_ok( $result->return_code, "==", 0, "with JSON test format result should always be OK");

my $warn_percth_data = $result->{'mp_test_result'}->{'checks'}->[0]->{'checks'}[0]->{'perfdata'}->[0]->{'warn'}->{'end'}->{'value'};
my $crit_percth_data = $result->{'mp_test_result'}->{'checks'}->[0]->{'checks'}[0]->{'perfdata'}->[0]->{'crit'}->{'end'}->{'value'};
my $total_percth_data = $result->{'mp_test_result'}->{'checks'}->[0]->{'checks'}[0]->{'perfdata'}->[0]->{'max'}->{'value'};

is ($warn_percth_data <=> int((20/100)*$total_percth_data), 0, "Wrong warning in perf data using percent thresholds");
is ($crit_percth_data <=> int((10/100)*$total_percth_data), 0, "Wrong critical in perf data using percent thresholds");


# Check when order of mount points are reversed, that perf data remains same
$result = NPTest->testCmd(
	"./check_disk -w 1% -c 1% -p $mountpoint2_valid -w 1% -c 1% -p $mountpoint_valid $output_format"
	);
cmp_ok( $result->return_code, "==", 0, "with JSON test format result should always be OK");

# write comparison set for perfdata here, but in reversed order, maybe there is a smarter way
my @perfdata2;
@perfdata2[1] = $result->{'mp_test_result'}->{'checks'}->[0]->{'checks'}->[0]->{'perfdata'}->[0];
@perfdata2[0] = $result->{'mp_test_result'}->{'checks'}->[1]->{'checks'}->[0]->{'perfdata'}->[0];
is_deeply(\@perfdata, \@perfdata2, "perf data for both filesystems same when reversed");

# Basic filesystem checks for sizes
$result = NPTest->testCmd( "./check_disk -w 1 -c 1 -p $more_free $output_format");
cmp_ok( $result->return_code, "==", 0, "with JSON test format result should always be OK");
like($result->{'mp_test_result'}->{'state'}, "/OK/", "At least 1 MB available on $more_free");

$result = NPTest->testCmd( "./check_disk -w 1 -c 1 -p $more_free -p $less_free $output_format" );
cmp_ok( $result->return_code, "==", 0, "with JSON test format result should always be OK");
like($result->{'mp_test_result'}->{'state'}, "/OK/", "At least 1 MB available on $more_free and $less_free");

my $free_mb_on_mp1 =$result->{'mp_test_result'}->{'checks'}->[0]->{'checks'}->[0]->{'perfdata'}->[0]->{'value'}->{'value'} / (1024 * 1024);
my $free_mb_on_mp2 = $result->{'mp_test_result'}->{'checks'}->[1]->{'checks'}->[0]->{'perfdata'}->[0]->{'value'}->{'value'}/ (1024 * 1024);
die "Cannot parse output: $_" unless ($free_mb_on_mp1 && $free_mb_on_mp2);

my $free_mb_on_all = $free_mb_on_mp1 + $free_mb_on_mp2;


$result = NPTest->testCmd( "./check_disk -e -w 1 -c 1 -p $more_free $output_format" );
cmp_ok( $result->return_code, "==", 0, "with JSON test format result should always be OK");

$result = NPTest->testCmd( "./check_disk 100 100 $more_free" );
cmp_ok( $result->return_code, '==', 0, "Old syntax okay" );

$result = NPTest->testCmd( "./check_disk -w 1% -c 1% -p $more_free" );
cmp_ok( $result->return_code, "==", 0, "At least 1% free" );

$result = NPTest->testCmd(
	"./check_disk -w 1% -c 1% -p $more_free -w 100% -c 100% -p $less_free $output_format"
	);
cmp_ok( $result->return_code, "==", 0, "with JSON test format result should always be OK");
like($result->{'mp_test_result'}->{'state'}, "/CRITICAL/", "Get critical on less_free mountpoint $less_free");


$result = NPTest->testCmd(
	"./check_disk -w $avg_free_percent% -c 0% -p $less_free $output_format"
	);
cmp_ok( $result->return_code, "==", 0, "with JSON test format result should always be OK");
like($result->{'mp_test_result'}->{'state'}, "/WARNING/", "Get warning on less_free mountpoint, when checking avg_free");

$result = NPTest->testCmd(
	"./check_disk -w $avg_free_percent% -c $avg_free_percent% -p $more_free"
	);
cmp_ok( $result->return_code, '==', 0, "Get ok on more_free mountpoint, when checking avg_free");

$result = NPTest->testCmd(
	"./check_disk -w $avg_free_percent% -c 0% -p $less_free -w $avg_free_percent% -c $avg_free_percent% -p $more_free"
	);
cmp_ok( $result->return_code, "==", 1, "Combining above two tests, get warning");
my $all_disks = $result->output;

$result = NPTest->testCmd(
	"./check_disk -e -w $avg_free_percent% -c 0% -p $less_free -w $avg_free_percent% -c $avg_free_percent% -p $more_free"
	);
isnt( $result->output, $all_disks, "-e gives different output");

# Need spaces around filesystem name in case less_free and more_free are nested
like( $result->output, qr/ $less_free /, "Found problem $less_free");
unlike( $result->only_output, qr/ $more_free /, "Has ignored $more_free as not a problem");
like( $result->perf_output, qr/'$more_free'=/, "But $more_free is still in perf data");

$result = NPTest->testCmd(
	"./check_disk -w $avg_free_percent% -c 0% -p $more_free"
	);
cmp_ok( $result->return_code, '==', 0, "Get ok on more_free mountpoint, checking avg_free");

$result = NPTest->testCmd(
	"./check_disk -w $avg_free_percent% -c $avg_free_percent% -p $less_free"
	);
cmp_ok( $result->return_code, '==', 2, "Get critical on less_free, checking avg_free");
$result = NPTest->testCmd(
	"./check_disk -w $avg_free_percent% -c 0% -p $more_free -w $avg_free_percent% -c $avg_free_percent% -p $less_free"
	);
cmp_ok( $result->return_code, '==', 2, "Combining above two tests, get critical");

$result = NPTest->testCmd(
	"./check_disk -w $avg_free_percent% -c $avg_free_percent% -p $less_free -w $avg_free_percent% -c 0% -p $more_free"
	);
cmp_ok( $result->return_code, '==', 2, "And reversing arguments should not make a difference");



# Basic inode checks for sizes

$result = NPTest->testCmd( "./check_disk --icritical 1% --iwarning 1% -p $more_inode_free" );
is( $result->return_code, 0, "At least 1% free on inodes for both mountpoints");

$result = NPTest->testCmd( "./check_disk -K 100% -W 100% -p $less_inode_free" );
is( $result->return_code, 2, "Critical requesting 100% free inodes for both mountpoints");

$result = NPTest->testCmd( "./check_disk --iwarning 1% --icritical 1% -p $more_inode_free -K 100% -W 100% -p $less_inode_free" );
is( $result->return_code, 2, "Get critical on less_inode_free mountpoint $less_inode_free");

$result = NPTest->testCmd( "./check_disk -W $avg_inode_free_percentage% -K 0% -p $less_inode_free" );
is( $result->return_code, 1, "Get warning on less_inode_free, when checking average");

$result = NPTest->testCmd( "./check_disk -W $avg_inode_free_percentage% -K $avg_inode_free_percentage% -p $more_inode_free ");
is( $result->return_code, 0, "Get ok on more_inode_free when checking average");

$result = NPTest->testCmd( "./check_disk -W $avg_inode_free_percentage% -K 0% -p $less_inode_free -W $avg_inode_free_percentage% -K $avg_inode_free_percentage% -p $more_inode_free" );
is ($result->return_code, 1, "Combine above two tests, get warning");
$all_disks = $result->output;

$result = NPTest->testCmd( "./check_disk -e -W $avg_inode_free_percentage% -K 0% -p $less_inode_free -W $avg_inode_free_percentage% -K $avg_inode_free_percentage% -p $more_inode_free" );
isnt( $result->output, $all_disks, "-e gives different output");
like( $result->output, qr/$less_inode_free/, "Found problem $less_inode_free");
unlike( $result->only_output, qr/$more_inode_free\s/, "Has ignored $more_inode_free as not a problem");
like( $result->perf_output, qr/$more_inode_free/, "But $more_inode_free is still in perf data");

$result = NPTest->testCmd( "./check_disk -W $avg_inode_free_percentage% -K 0% -p $more_inode_free" );
is( $result->return_code, 0, "Get ok on more_inode_free mountpoint, checking average");

$result = NPTest->testCmd( "./check_disk -W $avg_inode_free_percentage% -K $avg_inode_free_percentage% -p $less_inode_free" );
is( $result->return_code, 2, "Get critical on less_inode_free, checking average");

$result = NPTest->testCmd( "./check_disk -W $avg_inode_free_percentage% -K 0% -p $more_inode_free -W $avg_inode_free_percentage% -K $avg_inode_free_percentage% -p $less_inode_free" );
is( $result->return_code, 2, "Combining above two tests, get critical");

$result = NPTest->testCmd( "./check_disk -W $avg_inode_free_percentage% -K $avg_inode_free_percentage% -p $less_inode_free -W $avg_inode_free_percentage% -K 0% -p $more_inode_free" );
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

$result = NPTest->testCmd( "./check_disk -w 100% -c 100% $output_format ".${mountpoint_valid} );      # 100% empty
cmp_ok( $result->return_code, "==", 0, "100% empty" );
like($result->{'mp_test_result'}->{'state'}, "/CRITICAL/", "100% empty");

$result = NPTest->testCmd( "./check_disk -w 100000000 -c 100000000 $mountpoint_valid" );
cmp_ok( $result->return_code, '==', 2, "Check for 100TB free" );

$result = NPTest->testCmd( "./check_disk -w 100 -c 100 -u TB ".${mountpoint_valid} );      # 100 TB empty
cmp_ok( $result->return_code, "==", 2, "100 TB empty" );


# Checking old syntax of check_disk warn crit [fs], with warn/crit at USED% thresholds
$result = NPTest->testCmd( "./check_disk 0 0 ".${mountpoint_valid} );
cmp_ok( $result->return_code, "==", 2, "Old syntax: 0% used");
# like ( $result->only_output, qr(^[^;]*;[^;]*$), "Select only one path with positional arguments");
# TODO not sure what the above should test, taking it out

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
$result = NPTest->testCmd( "./check_disk -w 0% -c 0% -C -w 0% -c 0% -p $mountpoint_valid $output_format" );
cmp_ok( $result->return_code, "==", 0, "with JSON test format result should always be OK");
cmp_ok(scalar $result->{'mp_test_result'}->{'checks'}, '>', 1, "-C invokes matchall logic again");

# grouping: exit crit if the sum of free megs on mp1+mp2 is less than warn/crit
$result = NPTest->testCmd( "./check_disk -w ". ($free_mb_on_all + 1) ." -c ". ($free_mb_on_all + 1) ." -g group -p $mountpoint_valid -p $mountpoint2_valid" );
cmp_ok( $result->return_code, '==', 2, "grouping: exit crit if the sum of free megs on mp1+mp2 is less than warn/crit\nInstead received: " . $result->output);

# grouping: exit warning if the sum of free megs on mp1+mp2 is between -w and -c
$result = NPTest->testCmd( "./check_disk -w ". ($free_mb_on_all + 1) ." -c ". ($free_mb_on_all - 1) ." -g group -p $mountpoint_valid -p $mountpoint2_valid" );
cmp_ok( $result->return_code, '==', 1, "grouping: exit warning if the sum of free megs on mp1+mp2 is between -w and -c ");

# grouping: exit ok if the sum of free megs on mp1+mp2 is more than warn/crit
$result = NPTest->testCmd( "./check_disk -w ". ($free_mb_on_all - 1) ." -c ". ($free_mb_on_all - 1) ." -g group -p $mountpoint_valid -p $mountpoint2_valid" );
cmp_ok( $result->return_code, '==', 0, "grouping: exit ok if the sum of free megs on mp1+mp2 is more than warn/crit");

# grouping: exit unknown if group name is given after -p
$result = NPTest->testCmd( "./check_disk -w ". ($free_mb_on_all - 1) ." -c ". ($free_mb_on_all - 1) ." -p $mountpoint_valid -g group -p $mountpoint2_valid" );
cmp_ok( $result->return_code, '==', 3, "Invalid options: -p must come after groupname");

# regex: exit unknown if given regex is not compilable
$result = NPTest->testCmd( "./check_disk -w 1 -c 1 -r '('" );
cmp_ok( $result->return_code, '==', 3, "Exit UNKNOWN if regex is not compilable");

# ignore: exit unknown, if all paths are deselected using -i
$result = NPTest->testCmd( "./check_disk -w 0% -c 0% -p $mountpoint_valid -p $mountpoint2_valid -i '$mountpoint_valid' -i '$mountpoint2_valid'" );
cmp_ok( $result->return_code, '==', 3, "ignore-ereg: Unknown if all fs are ignored (case sensitive)");

# ignore: exit unknown, if all paths are deselected using -I
$result = NPTest->testCmd( "./check_disk -w 0% -c 0% -p $mountpoint_valid -p $mountpoint2_valid -I '".uc($mountpoint_valid)."' -I '".uc($mountpoint2_valid)."'" );
cmp_ok( $result->return_code, '==', 3, "ignore-ereg: Unknown if all fs are ignored (case insensitive)");

# ignore: exit unknown, if all paths are deselected using -i
$result = NPTest->testCmd( "./check_disk -w 0% -c 0% -p $mountpoint_valid -p $mountpoint2_valid -i '.*'" );
cmp_ok( $result->return_code, '==', 3, "ignore-ereg: Unknown if all fs are ignored using -i '.*'");

# ignore: test if ignored path is actually ignored
$result = NPTest->testCmd( "./check_disk -w 0% -c 0% -p $mountpoint_valid -p $mountpoint2_valid -i '^$mountpoint2_valid\$'");
like( $result->output, qr/$mountpoint_valid/, "output data does have $mountpoint_valid in it");
unlike( $result->output, qr/$mountpoint2_valid/, "output data does not have $mountpoint2_valid in it");

# ignore: test if all paths are listed when ignore regex doesn't match
$result = NPTest->testCmd( "./check_disk -w 0% -c 0% -p $mountpoint_valid -p $mountpoint2_valid -i '^barbazJodsf\$'");
like( $result->output, qr/$mountpoint_valid/, "ignore: output data does have $mountpoint_valid when regex doesn't match");
like( $result->output, qr/$mountpoint2_valid/,"ignore: output data does have $mountpoint2_valid when regex doesn't match");

# ignore-missing: exit okay, when fs is not accessible
$result = NPTest->testCmd( "./check_disk --ignore-missing -w 0% -c 0% -p /bob");
cmp_ok( $result->return_code, '==', 0, "ignore-missing: return okay for not existing filesystem /bob");
like( $result->output, '/No filesystems were found for the provided parameters.*$/', 'Output OK');

# ignore-missing: exit okay, when regex does not match
$result = NPTest->testCmd( "./check_disk --ignore-missing -w 0% -c 0% -r /bob");
cmp_ok( $result->return_code, '==', 0, "ignore-missing: return okay for regular expression not matching");
like( $result->output, '/No filesystems were found for the provided parameters.*$/', 'Output OK');

# ignore-missing: exit okay, when fs with exact match (-E) is not found
$result = NPTest->testCmd( "./check_disk --ignore-missing -w 0% -c 0% -E -p /etc");
cmp_ok( $result->return_code, '==', 0, "ignore-missing: return okay when exact match does not find fs");
like( $result->output, '/No filesystems were found for the provided parameters.*$/', 'Output OK');

# ignore-missing: exit okay, when checking one existing fs and one non-existing fs (regex)
$result = NPTest->testCmd( "./check_disk --ignore-missing -w 0% -c 0% -r '/bob' -r '^/\$'");
cmp_ok( $result->return_code, '==', 0, "ignore-missing: return okay for regular expression not matching");

# ignore-missing: exit okay, when checking one existing fs and one non-existing fs (path)
$result = NPTest->testCmd( "./check_disk --ignore-missing -w 0% -c 0% -p '/bob' -p '/'");
cmp_ok( $result->return_code, '==', 0, "ignore-missing: return okay for regular expression not matching");
# like( $result->output, '/^DISK OK - free space: / .*; - ignored paths: /bob;.*$/', 'Output OK');

# ignore-missing: exit okay, when checking one non-existing fs (path) and one ignored
$result = NPTest->testCmd( "./check_disk -n -w 0% -c 0% -r /dummy -i /dummy2");
cmp_ok( $result->return_code, '==', 0, "ignore-missing: return okay for regular expression not matching");
like( $result->output, '/No filesystems were found for the provided parameters.*$/', 'Output OK');

# ignore-missing: exit okay, when regex match does not find anything
$result = NPTest->testCmd( "./check_disk -n -e -l -w 10% -c 5% -W 10% -K 5% -r /dummy");
cmp_ok( $result->return_code, '==', 0, "ignore-missing: return okay for regular expression not matching");

# ignore-missing: exit okay, when regex match does not find anything
$result = NPTest->testCmd( "./check_disk -n -l -w 10% -c 5% -W 10% -K 5% -r /dummy");
cmp_ok( $result->return_code, '==', 0, "ignore-missing: return okay for regular expression not matching");
like( $result->output, '/No filesystems were found for the provided parameters.*$/', 'Output OK');
