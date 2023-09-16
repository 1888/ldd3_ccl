#! /bin/sh
module="scull"
device="scull"
mode="666"

# invoke insmod with all arguments we got
# and use a pathname, as newer modutils don't look in by default.
# if insmod return fail, exit the script.
/sbin/insmod ./$module.ko $* || exit 1

# remove stale nodes
rm -f /dev/${device}[0-3]

# parse the major number dynamic allocated to scull.
major=$(awk "\$2==\"$module\" {print \$1}" /proc/devices)

# make device node
mknod /dev/${device}0 c $major 0
mknod /dev/${device}1 c $major 1
mknod /dev/${device}2 c $major 2
mknod /dev/${device}3 c $major 3

# give appropriate group/permissions, and change the group.
# not all distributions have staff, some have "wheel" instead.
# change to staff group, which includs all uses on your system.
group="staff"
grep -q '^staff:' /etc/group || group="wheel"

chgrp $group /dev/${device}[0-3]
chmod $mode /dev/${device}[0-3]
