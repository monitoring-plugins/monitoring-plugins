#! /bin/sh

if [ -x /bin/mktemp ]; then
	TEMP=`/bin/mktemp $1.$$.XXXXXX` || exit 1
else
	TEMP=$1.$$.`date +"%S"`
	umask 177
	touch $TEMP
fi

sed -f subst $1 > $TEMP

chmod +x $TEMP
touch -r $1 $TEMP
cp -p $TEMP $1
rm $TEMP
