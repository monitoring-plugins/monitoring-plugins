package packet_utils;

# $Id: packet_utils.pm 1100 2005-01-25 09:12:47Z stanleyhopcroft $

# Revision 1.1  2005/01/25 09:12:47  stanleyhopcroft
# packet creation and dumping hacks used by check_ica* and check_lotus
#
# Revision 1.1  2005-01-25 15:28:58+11  anwsmh
# Initial revision
#
	
require Exporter;
@ISA = qw(Exporter);
@EXPORT_OK = qw(tethereal pdump);
	
sub tethereal {
	my ($tethereal_dump, $start_byte) = @_ ;
	
	# Return a string or array (depending on context) containing the characters from a tethereal trace.
	# Skip all stuff until the first byte given by $start_byte in the first row ..
	
	&outahere("Invalid tethereal dump:", substr($tethereal_dump, 0, 71), 'fails to match m#\d\d\d\d  \S\S(?: \S\S){1,15}#')
		unless $tethereal_dump =~ m#\d\d\d\d  \S\S(?: \S\S){1,15}# ;
	
	my @tethereal_dump	= split(/\n/, $tethereal_dump) ;
	my $first_line		= shift @tethereal_dump ;
	$first_line			= unpack('x6 a48', $first_line) ;
												# Take one extra space (after hex bytes) to use a template of 'a2 x' x 16 
												# instead of 'a2 x' x 15 . 'a2'
	my $last_line		= pop @tethereal_dump ;
	$last_line			= unpack('x6 a48', $last_line) ;
	
	my $idx = index($first_line, $start_byte) ;
	
	&outahere(qq(Invalid tethereal dump: "$start_byte" not found in first line - "$first_line")) 
		if $idx == -1 ;
	
	$first_line = substr($first_line, $idx) ;
	
	my ($dump, @dump) = ('', ()) ;
	
	my $bytes = 0 ;
	$bytes++
		while $first_line =~ m#\b\S\S#g ;
	push @dump, unpack('a2x' x $bytes, $first_line) ;
	
	push @dump, unpack('x6 ' . 'a2x' x 16, $_) 
		foreach @tethereal_dump ;
	
	$bytes = 0 ;
	$bytes++
		while $last_line =~ m#\b\S\S#g ;

												# Be more cautious with the last line; the ASCII decode may
												# have been omitted.

	push @dump, unpack(('a2x' x ($bytes - 1)) . 'a2', $last_line) ;
	
	return wantarray ? map pack('H2', $_), @dump : pack('H2' x scalar @dump, @dump) ;
	# return wantarray ? map hex($_), @dump : pack('H2' x scalar @dump, @dump) ;
	
}

sub pdump {
	my ($x) = shift @_ ;
	my (@bytes_in_row, $row, $dump) ;

	my $number_in_row = 16 ;
	my $number_of_bytes = length $x ;
	my $full_rows = int( $number_of_bytes / $number_in_row ) ;
	my $bytes_in_last_row = $number_of_bytes % $number_in_row ;
	my $template = "a$number_in_row " x $full_rows ;
	my $nr = 0 ;
												# Output format styled on tethereal.
	foreach $row ( unpack($template, $x) ) {
		@bytes_in_row = unpack('C*', $row) ;
		$row =~ tr /\x00-\x1f\x80-\xff/./ ;
		$dump .= join('  ', sprintf('%4.4x', $nr * 0x10), join(' ', map { sprintf "%2.2x", $_} @bytes_in_row), $row) ;
		$dump .= "\n" ;
		$nr++ ;
	}

	if ( $bytes_in_last_row ) {
		my $number_of_spaces = ($number_in_row - $bytes_in_last_row)*3  - 2 ;

												# 3 spaces (2 digts + 1 space) for each digit printed
												# minus two spaces for those added by the join('  ',) below.

		$row = substr($x, -$bytes_in_last_row) ;
		@bytes_in_row = unpack('C*', $row) ;
		$row =~ tr /\x00-\x1f\x80-\xff/./ ;
												# my $bytes = join(' ', map { sprintf "%2.2x", $_} @bytes_in_row) ;
												# See comment below.
		my $spaces = ' ' x $number_of_spaces ;
		$dump .= join('  ', sprintf("%4.4x", $nr * 0x10 ), join(' ', map { sprintf "%2.2x", $_} @bytes_in_row), $spaces, $row) ;
		$dump .= "\n" ;
	}

	print STDERR $dump, "\n" ;

=begin comment

tsitc> perl -MBenchmark -e 'timethese(1_00_000, { printf => q<printf "%2.2x %2.2x %2.2x\n", 61, 62, 63>, sprintf => q<$x = sprintf "%2
.2x %2.2x %2.2x\n", 61, 62, 63; print $x>} )' | perl -ne 'print if /intf/'

Benchmark: timing 100000 iterations of printf, sprintf...
    printf:  1 wallclock secs ( 0.55 usr +  0.00 sys =  0.55 CPU)
   sprintf:  0 wallclock secs ( 0.11 usr +  0.00 sys =  0.11 CPU)

so having sprintf in a loop seems more rational than a printf for the line ...

=comment

=cut
  
}

	
	
