package Helper;
use strict;

use Exporter();
use vars qw($VERSION @ISA @EXPORT);
$VERSION = 0.01;
@ISA=qw(Exporter);
@EXPORT=qw(&get_option);

sub get_option ($$) {
    my $file = 'Cache';
    my $response;
    my $var = shift;

    require "$file.pm";
    if(defined($Cache::{$var})){
			$response=$Cache::{$var};
			return $$response;
		}

		my $request = shift;
		my $filename;
		my $path;
		foreach $path (@INC) {
			$filename="$path/$file.pm";
			last if (-e $filename);
		}
		print STDERR "Enter $request\n";
		$response=<STDIN>;
		chop($response);
		open(CACHE,"<$filename") or die "Cannot open cache for reading";
		undef $/;
		my $cache = <CACHE>;
		$/="\n";
		close CACHE;
		$cache =~ s/^(\@EXPORT\s*=\s*qw\(\s*[^\)]*)\)\s*;/$1 $var\)\;/msg;
		$cache =~ s/^1;[\n\s]*\Z/\$$var=\"$response\"\;\n1\;\n/msg;
		open(CACHE,">$filename") or die "Cannot open cache for writing";
		print CACHE $cache;
		close CACHE;
		return $response;
}

1;
