package NPTest;

#
# Helper Functions for testing Nagios Plugins
#

require Exporter;
@ISA       = qw(Exporter);
@EXPORT    = qw(getTestParameter checkCmd skipMissingCmd);
@EXPORT_OK = qw(DetermineTestHarnessDirectory TestsFrom SetCacheFilename);

use strict;
use warnings;

use Cwd;
use File::Basename;

use IO::File;
use Data::Dumper;

use Test;

use vars qw($VERSION);
$VERSION = do { my @r = (q$Revision$ =~ /\d+/g); sprintf "%d."."%02d" x $#r, @r }; # must be all one line, for MakeMaker

=head1 NAME

NPTest - Simplify the testing of Nagios Plugins

=head1 DESCRIPTION

This modules provides convenience functions to assist in the testing
of Nagios Plugins, making the testing code easier to read and write;
hopefully encouraging the development of more complete test suite for
the Nagios Plugins. It is based on the patterns of testing seen in the
1.4.0 release, and continues to use the L<Test> module as the basis of
testing.

=head1 FUNCTIONS

This module defines three public functions, C<getTestParameter(...)>,
C<checkCmd(...)> and C<skipMissingCmd(...)>.  These are exported by
default via the C<use NPTest;> statement.

=over

=item C<getTestParameter(...)>

A flexible and user override-able method of collecting, storing and
retrieving test parameters. This function allows the test harness
developer to interactively request test parameter information from the
user, when the no means of obtaining the information automatically has
been successful. The user is provided with the option of accepting
test harness developer's default value for the parameter, if a suggested
default is provided.

User supplied responses are stored in an external (file-based)
cache. These values are retrieved on subsequent runs alleviating the
user of reconfirming the previous entered responses. The user is able
to override the value of a parameter on any given run by setting the
associated environment variable. These environment variable based
overrides are not stored in the cache, allowing one-time and what-if
based tests on the command line without polluting the cache.

The option exists to store parameters in a scoped means, allowing a
test harness to a localise a parameter should the need arise. This
allows a parameter of the same name to exist in a test harness
specific scope, while not affecting the globally scoped parameter. The
scoping identifier is the name of the test harness sans the trailing
".t".  All cache searches first look to a scoped parameter before
looking for the parameter at global scope. Thus for a test harness
called "check_disk.t" requesting the parameter "mountpoint_valid", the
cache is first searched for "check_disk"/"mountpoint_valid", if this
fails, then a search is conducted for "mountpoint_valid".

The facilitate quick testing setup, it is possible to accept all the
developer provided defaults by setting the environment variable
"NPTEST_ACCEPTDEFAULT" to "1" (or any other perl truth value). Note
that, such defaults are not stored in the cache, as there is currently
no mechanism to edit existing cache entries, save the use of text
editor or removing the cache file completely.

=item C<checkCmd(...)>

This function attempts to encompass the majority of test styles used
in testing Nagios Plugins. As each plug-in is a separate command, the
typical tests we wish to perform are against the exit status of the
command and the output (if any) it generated. Simplifying these tests
into a single function call, makes the test harness easier to read and
maintain and allows additional functionality (such as debugging) to be
provided withoutadditional effort on the part of the test harness
developer.

It is possible to enable debugging via the environment variable
C<NPTEST_DEBUG>. If this environment variable exists and its value in PERL's
boolean context evaluates to true, debugging is enabled.

The function prototype can be expressed as follows:

  Parameter 1 : command => DEFINED SCALAR(string)
  Parameter 2 : desiredExitStatus => ONE OF
                  SCALAR(integer)
                  ARRAYREF(integer)
                  HASHREF(integer,string)
                  UNDEFINED
  Parameter 3 : desiredOutput => SCALAR(string) OR UNDEFINED
  Parameter 4 : exceptions => HASH(integer,string) OR UNDEFINED
  Returns     : SCALAR(integer) as defined by Test::ok(...)

The function treats the first parameter C<$command> as a command line
to execute as part of the test, it is executed only once and its exit
status (C<$?E<gt>E<gt>8>) and output are captured.

At this point if debugging is enabled the command, its exit status and
output are displayed to the tester.

C<checkCmd(...)> allows the testing of either the exit status or the
generated output or both, not testing either will result in neither
the C<Test::ok(...)> or C<Test::skip(...)> functions being called,
something you probably don't want. Note that each defined test
(C<$desiredExitStatus> and C<$desiredOutput>) results in a invocation
of either C<Test::ok(...)> or C<Test::skip(...)>, so remember this
when counting the number of tests to place in the C<Test::plan(...)>
call.

Many Nagios Plugins test network services, some of which may not be
present on all systems. To cater for this, C<checkCmd(...)> allows the
tester to define exceptions based on the command's exit status. These
exceptions are provided to skip tests if the test case developer
believes the service is not being provided. For example, if a site
does not have a POP3 server, the test harness could map the
appropriate exit status to a useful message the person running the
tests, telling the reason the test is being skipped.

Example:

my %exceptions = ( 2 =E<gt> "No POP Server present?" );

$t += checkCmd( "./check_pop I<some args>", 0, undef, %exceptions );

Thus, in the above example, an exit status of 2 does not result in a
failed test case (as the exit status is not the desired value of 0),
but a skipped test case with the message "No POP Server present?"
given as the reason.

Sometimes the exit status of a command should be tested against a set
of possible values, rather than a single value, this could especially
be the case in failure testing. C<checkCmd(...)> support two methods
of testing against a set of desired exit status values.

=over

=item *

Firstly, if C<$desiredExitStatus> is a reference to an array of exit
stati, if the actual exit status of the command is present in the
array, it is used in the call to C<Test::ok(...)> when testing the
exit status.

=item *

Alternatively, if C<$desiredExitStatus> is a reference to a hash of
exit stati (mapped to the strings "continue" or "skip"), similar
processing to the above occurs with the side affect of determining if
any generated output testing should proceed. Note: only the string
"skip" will result in generated output testing being skipped.

=back

=item C<skipMissingCmd(...)>

If a command is missing and the test harness must C<Test::skip()> some
or all of the tests in a given test harness this function provides a
simple iterator to issue an appropriate message the requested number
of times.

=back

=head1 SEE ALSO

L<Test>

The rest of the code, as I have only commented on the major public
functions that test harness writers will use, not all the code present
in this helper module.

=head1 AUTHOR

Copyright (c) 2005 Peter Bray.  All rights reserved.

This package is free software and is provided "as is" without express
or implied warranty.  It may be used, redistributed and/or modified
under the same terms as the Nagios Plugins release.

=cut

#
# Package Scope Variables
#

my( %CACHE ) = ();

# I'm not really sure wether to house a site-specific cache inside
# or outside of the extracted source / build tree - lets default to outside
my( $CACHEFILENAME ) = ( exists( $ENV{'NPTEST_CACHE'} ) && $ENV{'NPTEST_CACHE'} )
                       ? $ENV{'NPTEST_CACHE'} : "/var/tmp/NPTest.cache"; # "../Cache.pdd";

#
# Testing Functions
#

sub checkCmd
{
  my( $command, $desiredExitStatus, $desiredOutput, %exceptions ) = @_;

  my $output     = `${command}`;
  my $exitStatus = $? >> 8;

  $output = "" unless defined( $output );
  chomp( $output );

  if ( exists( $ENV{'NPTEST_DEBUG'} ) && $ENV{'NPTEST_DEBUG'} )
  {
    my( $pkg, $file, $line ) = caller(0);

    print "checkCmd: Called from line $line in $file\n";
    print "Testing : ${command}\n";
    print "Result  : ${exitStatus} AND '${output}'\n";
  }

  my $testStatus;

  my $testOutput = "continue";

  if ( defined( $desiredExitStatus ) )
  {
    if ( ref $desiredExitStatus eq "ARRAY" )
    {
      if ( scalar( grep { $_ == $exitStatus } @{$desiredExitStatus} ) )
      {
	$desiredExitStatus = $exitStatus;
      }
      else
      {
	$desiredExitStatus = -1;
      }
    }
    elsif ( ref $desiredExitStatus eq "HASH" )
    {
      if ( exists( ${$desiredExitStatus}{$exitStatus} ) )
      {
	if ( defined( ${$desiredExitStatus}{$exitStatus} ) )
	{
	  $testOutput = ${$desiredExitStatus}{$exitStatus};
	}
	$desiredExitStatus = $exitStatus;
      }
      else
      {
	$desiredExitStatus = -1;
      }
    }

    if ( %exceptions && exists( $exceptions{$exitStatus} ) )
    {
      $testStatus += skip( $exceptions{$exitStatus}, $exitStatus, $desiredExitStatus );
    }
    else
    {
      $testStatus += ok( $exitStatus, $desiredExitStatus );
    }
  }

  if ( defined( $desiredOutput ) )
  {
    if ( $testOutput ne "skip" )
    {
      $testStatus += ok( $output, $desiredOutput );
    }
    else
    {
      $testStatus += skip( "Skipping output test as requested", $output, $desiredOutput );
    }
  }

  return $testStatus;
}


sub skipMissingCmd
{
  my( $command, $count ) = @_;

  my $testStatus;

  for ( 1 .. $count )
  {
    $testStatus += skip( "Missing ${command} - tests skipped", 1 );
  }

  return $testStatus;
}

sub getTestParameter
{
  my( $param, $envvar, $default, $brief, $scoped ) = @_;

  # Apply default values for optional arguments
  $scoped = ( defined( $scoped ) && $scoped );

  my $testharness = basename( (caller(0))[1], ".t" ); # used for scoping

  if ( defined( $envvar ) &&  exists( $ENV{$envvar} ) && $ENV{$envvar} )
  {
    return $ENV{$envvar}
  }

  my $cachedValue = SearchCache( $param, $testharness );
  if ( defined( $cachedValue ) && $cachedValue )
  {
    return $cachedValue;
  }

  my $defaultValid      = ( defined( $default ) && $default );
  my $autoAcceptDefault = ( exists( $ENV{'NPTEST_ACCEPTDEFAULT'} ) && $ENV{'NPTEST_ACCEPTDEFAULT'} );

  if ( $autoAcceptDefault && $defaultValid )
  {
    return $default;
  }

  my $userResponse = "";

  while ( $userResponse eq "" )
  {
    print STDERR "\n";
    print STDERR "Test Harness         : $testharness\n";
    print STDERR "Test Parameter       : $param\n";
    print STDERR "Environment Variable : $envvar\n";
    print STDERR "Brief Description    : $brief\n";
    print STDERR "Enter value ", ($defaultValid ? "[${default}]" : "[]"), " => ";
    $userResponse = <STDIN>;
    $userResponse = "" if ! defined( $userResponse ); # Handle EOF
    chomp( $userResponse );
    if ( $defaultValid && $userResponse eq "" )
    {
      $userResponse = $default;
    }
  }

  print STDERR "\n";

  # define all user responses at global scope
  SetCacheParameter( $param, ( $scoped ? $testharness : undef ), $userResponse );

  return $userResponse;
}

#
# Internal Cache Management Functions
#

sub SearchCache
{
  my( $param, $scope ) = @_;

  LoadCache();

  if ( exists( $CACHE{$scope} ) && exists( $CACHE{$scope}{$param} ) )
  {
    return $CACHE{$scope}{$param};
  }

  if ( exists( $CACHE{$param} ) )
  {
    return $CACHE{$param};
  }
}

sub SetCacheParameter
{
  my( $param, $scope, $value ) = @_;

  if ( defined( $scope ) )
  {
    $CACHE{$scope}{$param} = $value;
  }
  else
  {
    $CACHE{$param} = $value;
  }

  SaveCache();
}

sub LoadCache
{
  return if exists( $CACHE{'_cache_loaded_'} );

  if ( -f $CACHEFILENAME )
  {
    my( $fileHandle ) = new IO::File;

    if ( ! $fileHandle->open( "< ${CACHEFILENAME}" ) )
    {
      print STDERR "NPTest::LoadCache() : Problem opening ${CACHEFILENAME} : $!\n";
      return;
    }

    my( $fileContents ) = join( "\n", <$fileHandle> );

    $fileHandle->close();

    my( $contentsRef ) = eval $fileContents;
    %CACHE = %{$contentsRef};

  }

  $CACHE{'_cache_loaded_'} = 1;
}


sub SaveCache
{
  delete $CACHE{'_cache_loaded_'};

  my( $fileHandle ) = new IO::File;

  if ( ! $fileHandle->open( "> ${CACHEFILENAME}" ) )
  {
    print STDERR "NPTest::LoadCache() : Problem saving ${CACHEFILENAME} : $!\n";
    return;
  }

  my( $dataDumper ) = new Data::Dumper( [ \%CACHE ] );

  $dataDumper->Terse(1);

  print $fileHandle $dataDumper->Dump();

  $fileHandle->close();

  $CACHE{'_cache_loaded_'} = 1;
}

#
# (Questionable) Public Cache Management Functions
#

sub SetCacheFilename
{
  my( $filename ) = @_;

  # Unfortunately we can not validate the filename
  # in any meaningful way, as it may not yet exist
  $CACHEFILENAME = $filename;
}


#
# Test Harness Wrapper Functions
#

sub DetermineTestHarnessDirectory
{
  my( $userSupplied ) = @_;

  # User Supplied
  if ( defined( $userSupplied ) && $userSupplied )
  {
    if ( -d $userSupplied )
    {
      return $userSupplied;
    }
    else
    {
      return undef; # userSupplied is invalid -> FAIL
    }
  }

  # Simple Case : "t" is a subdirectory of the current directory
  if ( -d "./t" )
  {
    return "./t";
  }

  # To be honest I don't understand which case satisfies the
  # original code in test.pl : when $tstdir == `pwd` w.r.t.
  # $tstdir =~ s|^(.*)/([^/]+)/?$|$1/$2|; and if (-d "../../$2/t")
  # Assuming pwd is "/a/b/c/d/e" then we are testing for "/a/b/c/e/t"
  # if I understand the code correctly (a big assumption)

  # Simple Case : the current directory is "t"
  my $pwd = cwd();

  if ( $pwd =~ m|/t$| )
  {
    return $pwd;

    # The alternate that might work better is
    # chdir( ".." );
    # return "./t";
    # As the current test harnesses assume the application
    # to be tested is in the current directory (ie "./check_disk ....")
  }

  return undef;
}

sub TestsFrom
{
  my( $directory, $excludeIfAppMissing ) = @_;

  $excludeIfAppMissing = 0 unless defined( $excludeIfAppMissing );

  if ( ! opendir( DIR, $directory ) )
  {
    print STDERR "NPTest::TestsFrom() - Failed to open ${directory} : $!\n";
    return ();
  }

  my( @tests ) = ();

  my $filename;
  my $application;

  while ( $filename = readdir( DIR ) )
  {
    if ( $filename =~ m/\.t$/ )
    {
      if ( $excludeIfAppMissing )
      {
	$application = basename( $filename, ".t" );
	if ( ! -e $application )
	{
	  print STDERR "No application (${application}) found for test harness (${filename})\n";
	  next;
	}
      }
      push @tests, "${directory}/${filename}";
    }
  }

  closedir( DIR );

  return @tests;
}



1;
#
# End of File
#
