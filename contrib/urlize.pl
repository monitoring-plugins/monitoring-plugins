#!/usr/bin/perl
#
# urlize.pl
#   jcw, 5/12/00
#
# A wrapper around Nagios plugins that provides a URL link in the output
#

($#ARGV < 1) && die "Incorrect arguments";
my $url = shift;

chomp ($result = `@ARGV`);
print "<A HREF=\"$url\">$result</A>\n";

# exit with same exit value as the child produced
exit ($? >> 8);
