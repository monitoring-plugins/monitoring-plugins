#!/usr/bin/perl 
#
# (c)2000 Benjamin Schmid, blueshift@gmx.net (emergency use only ;-)
# Copyleft by GNU GPL
#
#
# check_email_loop Nagios Plugin
#
# This script sends a mail with a specific id in the subject via
# an given smtp-server to a given email-adress. When the script
# is run again, it checks for this Email (with its unique id) on
# a given pop3 account and send another mail.
# 
#
# Example: check_email_loop.pl -poph=mypop -popu=user -pa=password
# 	   -smtph=mailer -from=returnadress@yoursite.com
#	   -to=remaileradress@friend.com -pendc=2 -lostc=0
#
# This example will send eacht time this check is executed a new
# mail to remaileradress@friend.com using the SMTP-Host mailer.
# Then it looks for any back-forwarded mails in the POP3 host
# mypop. In this Configuration CRITICAL state will be reached if  
# more than 2 Mails are pending (meaning that they did not came 
# back till now) or if a mails got lost (meaning a mail, that was
# send later came back prior to another mail).
# 

use Net::POP3;
use Net::SMTP;
use strict;
use Getopt::Long;
&Getopt::Long::config('auto_abbrev');

# ----------------------------------------

my $TIMEOUT = 120;
my %ERRORS = ('UNKNOWN' , '-1',
              'OK' , '0',
              'WARNING', '1',
              'CRITICAL', '2');

my $state = "UNKNOWN";
my ($sender,$receiver, $pophost, $popuser, $poppasswd, $smtphost);
my ($poptimeout,$smtptimeout,$pinginterval)=(60,60,5);
my ($lostwarn, $lostcrit,$pendwarn, $pendcrit);

# Internal Vars
my ($pop,$msgcount,@msglines,$statinfo,@messageids,$newestid);
my ($matchcount,$statfile) = (0,"check_email_loop.stat");

# Subs declaration
sub usage;
sub messagematchs;
sub nsexit;

# Just in case of problems, let's not hang Nagios
$SIG{'ALRM'} = sub {
     print ("ERROR: $0 Time-Out $TIMEOUT s \n");
     exit $ERRORS{"UNKNOWN"};
};
alarm($TIMEOUT);


# Evaluate Command Line Parameters
my $status = GetOptions(
		        "from=s",\$sender,
			"to=s",\$receiver,
                        "pophost=s",\$pophost,
                        "popuser=s",\$popuser,
			"passwd=s",\$poppasswd,
			"poptimeout=i",\$poptimeout,
			"smtphost=s",\$smtphost,
			"smtptimeout=i",\$smtptimeout,
			"statfile=s",\$statfile,
			"interval=i",\$pinginterval,
			"lostwarr=i",\$lostwarn,
			"lostcrit=i",\$lostcrit,
			"pendwarn=i",\$pendwarn,
			"pendcrit=i",\$pendcrit,
			);
usage() if ($status == 0 || ! ($pophost && $popuser && $poppasswd &&
	$smtphost && $receiver && $sender ));

# Try to read the ids of the last send emails out of statfile
if (open STATF, "$statfile") {
  @messageids = <STATF>;
  chomp @messageids;
  close STATF;
}

# Try to open statfile for writing 
if (!open STATF, ">$statfile") {
  nsexit("Failed to open mail-ID database $statfile for writing",'CRITICAL');
}

# Ok - check if it's time to release another mail

# ...

# creating new serial id
my $serial = time();
$serial = "ID#" . $serial . "#$$";

# sending new ping email
my $smtp = Net::SMTP->new($smtphost,Timeout=>$smtptimeout) 
  || nsexit("SMTP connect timeout ($smtptimeout s)",'CRITICAL');
($smtp->mail($sender) &&
 $smtp->to($receiver) &&
 $smtp->data() &&
 $smtp->datasend("To: $receiver\nSubject: E-Mail Ping [$serial]\n\n".
		 "This is a automatically sended E-Mail.\n".
		 "It ist not intended for human reader.\n\n".
		 "Serial No: $serial\n") &&
 $smtp->dataend() &&
 $smtp->quit
 ) || nsexit("Error delivering message",'CRITICAL');

# no the interessting part: let's if they are receiving ;-)

$pop = Net::POP3->new( $pophost, 
		 Timeout=>$poptimeout) 
       || nsexit("POP3 connect timeout (>$poptimeout s, host: $pophost)",'CRITICAL');

$msgcount=$pop->login($popuser,$poppasswd);

$statinfo="$msgcount mails on POP3";

nsexit("POP3 login failed (user:$popuser)",'CRITICAL') if (!defined($msgcount));

# Count messages, that we are looking 4:
while ($msgcount > 0) {
  @msglines = @{$pop->get($msgcount)};

  for (my $i=0; $i < scalar @messageids; $i++) {
    if (messagematchsid(\@msglines,$messageids[$i])) { 
      $matchcount++;
      # newest received mail than the others, ok remeber id.
      $newestid = $messageids[$i] if ($messageids[$i] > $newestid || !defined $newestid);
      $pop->delete($msgcount);  # remove E-Mail from POP3 server
      splice @messageids, $i, 1;# remove id from List
      last;                     # stop looking in list
    }
  } 

  $msgcount--;
}

$pop->quit();  # necessary for pop3 deletion!

# traverse through the message list and mark the lost mails
# that mean mails that are older than the last received mail.
if (defined $newestid) {
  $newestid =~ /\#(\d+)\#/;
  $newestid = $1;
  for (my $i=0; $i < scalar @messageids; $i++) {
    $messageids[$i] =~ /\#(\d+)\#/;
    my $akid = $1;
    if ($akid < $newestid) {
      $messageids[$i] =~ s/^ID/LI/; # mark lost
    }
  }
}

# Write list to id-Database
foreach my $id (@messageids) {
  print STATF  "$id\n";
}
print STATF "$serial\n";     # remember send mail of this session
close STATF;

# ok - count lost and pending mails;
my @tmp = grep /^ID/, @messageids;
my $pendingm = scalar @tmp;
@tmp = grep /^LI/, @messageids;
my $lostm = scalar @tmp; 

# Evaluate the Warnin/Crit-Levels
if (defined $pendwarn && $pendingm > $pendwarn) { $state = 'WARNING'; }
if (defined $lostwarn && $lostm > $lostwarn) { $state = 'WARNING'; }
if (defined $pendcrit && $pendingm > $pendcrit) { $state = 'CRITICAL'; }
if (defined $lostcrit && $lostm > $lostcrit) { $state = 'CRITICAL'; }

if ((defined $pendwarn || defined $pendcrit || defined $lostwarn 
     || defined $lostcrit) && ($state eq 'UNKNOWN')) {$state='OK';}


# Append Status info
$statinfo = $statinfo . ", $matchcount mail(s) came back,".
            " $pendingm pending, $lostm lost.";

# Exit in a Nagios-compliant way
nsexit($statinfo);

# ----------------------------------------------------------------------

sub usage {
  print "check_email_loop 1.0 Nagios Plugin - Real check of a E-Mail system\n";
  print "=" x 75,"\nERROR: Missing or wrong arguments!\n","=" x 75,"\n";
  print "This script sends a mail with a specific id in the subject via an given\n";
  print "smtp-server to a given email-adress. When the script is run again, it checks\n";
  print "for this Email (with its unique id) on a given pop3 account and sends \n";
  print "another mail.\n";
  print "\nThe following options are available:\n";
  print	"   -from=text         email adress of send (for mail returnr on errors)\n";
  print	"   -to=text           email adress to which the mails should send to\n";
  print "   -pophost=text      IP or name of the POP3-host to be checked\n";
  print "   -popuser=text      Username of the POP3-account\n";
  print	"   -passwd=text       Password for the POP3-user\n";
  print	"   -poptimeout=num    Timeout in seconds for the POP3-server\n";
  print "   -smtphost=text     IP oder name of the SMTP host\n";
  print "   -smtptimeout=num   Timeout in seconds for the SMTP-server\n";
  print "   -statfile=text     File to save ids of messages ($statfile)\n";
#  print "   -interval=num      Time (in minutes) that must pass by before sending\n"
#  print "                      another Ping-mail (gibe a new try);\n"; 
  print "   -lostwarn=num      WARNING-state if more than num lost emails\n";
  print "   -lostcrit=num      CRITICAL \n";
  print "   -pendwarn=num      WARNING-state if more than num pending emails\n";
  print "   -pendcrit=num      CRITICAL \n";
  print " Options may abbreviated!\n";
  print " LOST mails are mails, being sent before the last mail arrived back.\n";
  print " PENDING mails are those, which are not. (supposed to be on the way)\n";
  print "\nExample: \n";
  print " $0 -poph=host -pa=pw -popu=popts -smtph=host -from=root\@me.com\n ";
  print "      -to=remailer\@testxy.com -lostc=0 -pendc=2\n";
  print "\nCopyleft 19.10.2000, Benjamin Schmid\n";
  print "This script comes with ABSOLUTELY NO WARRANTY\n";
  print "This programm is licensed under the terms of the ";
  print "GNU General Public License\n\n";
  exit $ERRORS{"UNKNOWN"};
}

# ---------------------------------------------------------------------

sub nsexit {
  my ($msg,$code) = @_;
  $code=$state if (!defined $code);
  print "$code: $msg\n" if (defined $msg);
  exit $ERRORS{$code};
}

# ---------------------------------------------------------------------

sub messagematchsid {
  my ($mailref,$id) = (@_);
  my (@tmp);
  my $match = 0;
 
  # ID
  $id =~ s/^LI/ID/;    # evtl. remove lost mail mark
  @tmp = grep /Subject: E-Mail Ping \[/, @$mailref;
  chomp @tmp;
  if (($tmp[0] =~ /$id/)) 
    { $match = 1; }

  # Sender:
#  @tmp = grep /^From:\s+/, @$mailref;
#  if (@tmp && $sender ne "") 
#    { $match = $match && ($tmp[0]=~/$sender/); }

  # Receiver:
#  @tmp = grep /^To: /, @$mailref;
#  if (@tmp && $receiver ne "") 
#    { $match = $match && ($tmp[0]=~/$receiver/); }

  return $match;
}

# ---------------------------------------------------------------------
