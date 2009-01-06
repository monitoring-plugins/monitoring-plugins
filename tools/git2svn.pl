#!/usr/bin/perl
#
# This script pulls the current branch, then walks the first parents and
# commit each of them into subversion.
#
# Copyright (C) 2008  Thomas Guyot-Sionnest <dermoth@aei.ca>
#
# The subversion repository must not be taking any external commit or this
# script will erase them. This script cannot run off a bare repository.
#
# *** INITIAL SETUP ***
#
# 1. Run this command line to get the repository up and ready for this script:
#
# $ cd /path/to/repo/; git log -1 --pretty=format:%H >.git/git2svn.last_commit_hash
#
# 2. Configure the lines below... $ENV{'GIT_DIR'} must point to the .git
#    directory of the git-svn repo.
#
# *** INITIAL SETUP ***
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 3 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.

use strict;
use warnings;

# This is the git working tree. Must be tied to a SVN repository
$ENV{'GIT_DIR'} = '/path/to/nagiosplug/.git';

# For some strange reasons this is needed:
$ENV{'GIT_SVN_ID'} = 'trunk';

# Path to git binary
my $git = '/usr/bin/git';

# Force commits from the hash stored in git2svn.last_commit_hash regardless
# of the state of the current repository. Use this if the repository was
# updated manually or if you need to set that hash to a specific value.
# NB: Re-committing old hashes will revert then roll again changes to SVN.
my $FORCE = 0;

# Print debug output. Useful if you want to see what's being committed.
my $DEBUG = 0;

for (@ARGV) {
	$FORCE = 1 if (m/force/);
	$DEBUG = 1 if (m/debug/);
	if (m/help/ || m/--help/ || m/-h/) {
		print "Usage: $0 [ debug ] [ force ] [ help ]\n";
		exit 0;
	}
}

# 1st get the current commit hash - we'll start committing to SVN from this one
print "Reading saved hash from $ENV{'GIT_DIR'}/git2svn.last_commit_hash\n" if ($DEBUG);
open(SAVHASH, "<$ENV{'GIT_DIR'}/git2svn.last_commit_hash")
	or die("Can't open $ENV{'GIT_DIR'}/git2svn.last_commit_hash: $!");
my $saved_commit_hash = <SAVHASH>;
chomp $saved_commit_hash;
print "Saved commit hash: $saved_commit_hash\n" if ($DEBUG);
close(SAVHASH);

my $last_commit_hash;
if ($FORCE) {
	$last_commit_hash = $saved_commit_hash;
	print "Forcing last commit hash to $last_commit_hash\n" if ($DEBUG);
} else {
	print "Running: $git log -1 --pretty=format:%H\n" if ($DEBUG);
	$last_commit_hash = `$git log -1 --pretty=format:%H`;
	die("Failed to retrieve last commit hash") if ($?);
	chomp $last_commit_hash;
	print "Last commit hash:  $last_commit_hash\n" if ($DEBUG);

	# Sanity check
	die("Last commit hash and saved commit hash don't match, aborting")
		if ($last_commit_hash ne $saved_commit_hash);
}

# 2nd pull the remote tree
print "Running: $git pull\n" if ($DEBUG);
`$git pull`;
die("Failed to pull") if ($?);

# Then list all first parents since the last one and insert them into an array
my @commits;
print "Running: $git rev-list --first-parent $last_commit_hash..HEAD\n" if ($DEBUG);
open(REVLIST, "$git rev-list --first-parent $last_commit_hash..HEAD|")
	or die("Failed to retrieve revision list: $!");

while (<REVLIST>) {
	chomp;
	unshift @commits, $_;
	print "Prepending the list with $_\n" if ($DEBUG);
}

close(REVLIST);

if (@commits == 0) {
	print "Nothing to do.\n";
	exit 0;
}

# Finally, commit every revision found into SVN
foreach my $commit (@commits) {
	print "Commiting $commit to Subversion\n";
	print "Running: $git svn set-tree --add-author-from $commit\n"	if ($DEBUG);
	`$git svn set-tree --add-author-from $commit`;
	die("Failed to commit hash $commit") if ($?);
}

# Once done, update the last commit hash
$last_commit_hash = pop @commits;
print "Writing last commit hash to $ENV{'GIT_DIR'}/git2svn.last_commit_hash\n" if ($DEBUG);
open(SAVHASH, ">$ENV{'GIT_DIR'}/git2svn.last_commit_hash")
	or die("Can't open $ENV{'GIT_DIR'}/git2svn.last_commit_hash for writing: $!");
print SAVHASH $last_commit_hash;
close(SAVHASH);

