#! /bin/bash

RUN_FILE=zone-irrigation.py

PREV_MD5=`md5sum $RUN_FILE`
git fetch origin master
git reset --hard FETCH_HEAD
git clean -df
NEW_MD5=`md5sum $RUN_FILE`


echo $PREV_MD5
echo $NEW_MD5

if [ "$PREV_MD5" != "$NEW_MD5" ]; then
    logger -s "Restarting zone-irrigation..."
    sudo systemctl restart zone-irrigation.service
fi
