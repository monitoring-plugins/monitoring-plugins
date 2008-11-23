#!/bin/bash
#
#    Program : check_smb
#            :
#     Author : Cal Evans <cal@calevans.com>
#            :
#    Purpose : Nagios plugin to return the number of users logged into a smb
#            : server and the number of files open.
#            :
# Parameters : --help
#            : --version
#            :
#    Returns : Standard Nagios status_* codes as defined in utils.sh
#            :
#      Notes :
#============:==============================================================
#        1.0 : 06/27/2002
#            : Initial coding
#            :
#        1.1 : 06/28/2002
#            : Re-wrote the user counter to match the file-lock counter.
#            :

#
# Shamelessly stolen from other Nagios plugins.
#
PROGNAME=`basename $0`
PROGPATH=`echo $0 | /bin/sed -e 's,[\\/][^\\/][^\\/]*$,,'`
REVISION=`echo '$Revision: 71 $' | sed -e 's/[^0-9.]//g'`


. $PROGPATH/utils.sh

print_usage() {
        echo "Usage: $PROGNAME --help"
        echo "Usage: $PROGNAME --version"
}

print_help() {
        print_revision $PROGNAME $REVISION
        echo ""
        print_usage
        echo ""
        echo "Samba status check."
        echo ""
        support
}

# No command line arguments are required for this script. We accept only 2,
# --help and --version.  If more than 1 is passed in then we have an error
# condition.

if [ $# -gt 1 ]; then
        print_usage
        exit $STATE_UNKNOWN
fi


#
# If we have arguments, process them.
#
exitstatus=$STATE_WARNING #default
while test -n "$1"; do
        case "$1" in
                --help)
                        print_help
                        exit $STATE_OK
                        ;;
                -h)
                        print_help
                        exit $STATE_OK
                        ;;
                --version)
                        print_revision $PROGNAME $REVISION
                        exit $STATE_OK
                        ;;
                -V)
                        print_revision $PROGNAME $REVISION
                        exit $STATE_OK
                        ;;

                *)
                        echo "Unknown argument: $1"
                        print_usage
                        exit $STATE_UNKNOWN
                        ;;
        esac
        shift
done

#
# No arguments.  Let's kick this pig.
#
total_users=$(smbstatus -b | grep "^[0-9]" | wc -l)

#
# Ok, now let's grab a count of the files.
#
total_files=$(smbstatus | grep "^[0-9]" | wc -l)

#
# now for the dismount.
#
echo "Total Users:$total_users Total Files:$total_files"

#
# let Nagios know that everything is ok.
#
exit $STATE_OK


