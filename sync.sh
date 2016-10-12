#!/bin/bash
FTPUSER=root
FTPPASS=pass
FTPPORT=2021
SYNCTSPREV=".lastsync"
SYNCTSNOW=".thissync"

#Get current timestamp
SYNCSTART=`date`
echo $SYNCSTART > $SYNCTSNOW

#Get last successful sync timestamp
printf "\n### Requesting last sync timestamp from MINIX ###\n"
lftp -e "get /usr/src/$SYNCTSPREV; quit" -u $FTPUSER,$FTPPASS -p $FTPPORT localhost || exit
SYNCNEWERTHAN=`cat $SYNCTSPREV`
rm $SYNCTSPREV

#Copy all files that have changed since then to a temporary directory
printf "\n### Preparing a list of modified files to send ###\n"
SYNCDIR=`mktemp -d` || exit
mkdir $SYNCDIR/src/
find src -type f ! -path "*.git*" -newermt "$SYNCNEWERTHAN" -exec cp --parents {} $SYNCDIR \; || exit
printf "Done\n"

#Mirror the updated files to MINIX
printf "\n### Sending modified files to MINIX ###\n"
lftp -e "mirror -R $SYNCDIR/src/ /usr/; quit" -u $FTPUSER,$FTPPASS -p $FTPPORT localhost || exit

#Write last sync timestamp to MINIX
printf "\n### Sending current sync timestamp to MINIX ###\n"
lftp -e "put $SYNCTSNOW -o /usr/src/$SYNCTSPREV; quit" -u $FTPUSER,$FTPPASS -p $FTPPORT localhost || exit
rm $SYNCTSNOW

#Done
printf "\n### Sync Complete ###\n"
rm -rf $SYNCDIR
