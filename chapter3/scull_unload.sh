#! /bin/sh
module="scull"
device="scull"

/sbin/rmmod $module

# remove stale nodes
rm -f /dev/${device}[0-3]
