#!/bin/ash

PORT=2323

if [ $1 == "nodebug" ]; then
    echo file circbuf.c -pfl > /sys/kernel/debug/dynamic_debug/control
    echo file sensor_common.c -pfl > /sys/kernel/debug/dynamic_debug/control
    echo file jpeghead.c -pfl > /sys/kernel/debug/dynamic_debug/control
	exit 0
elif [ $1 == "dump_state" ]; then
    echo "func circbuf_valid_ptr +p" > /sys/kernel/debug/dynamic_debug/control 
    echo "func get_image_length +p" > /sys/kernel/debug/dynamic_debug/control
    echo "func dump_interframe_params +p" > /sys/kernel/debug/dynamic_debug/control 
    echo "func dump_state +p" > /sys/kernel/debug/dynamic_debug/control 
	exit 0
elif [ $1 == "full_debug" ]; then
    echo file circbuf.c +pfl > /sys/kernel/debug/dynamic_debug/control
    echo file sensor_common.c +pfl > /sys/kernel/debug/dynamic_debug/control
    echo file jpeghead.c +pfl > /sys/kernel/debug/dynamic_debug/control
	exit 0
else
	# default, turn  off all
    echo file circbuf.c -pfl > /sys/kernel/debug/dynamic_debug/control
    echo file sensor_common.c -pfl > /sys/kernel/debug/dynamic_debug/control
    echo file jpeghead.c -pfl > /sys/kernel/debug/dynamic_debug/control
fi


 
if [ ! -e /dev/circbuf0 ]; then
	mknod /dev/circbuf0 c 135 32
fi
if [ ! -e /dev/circbuf1 ]; then
	mknod /dev/circbuf1 c 135 33
fi
if [ ! -e /dev/circbuf2 ]; then
	mknod /dev/circbuf2 c 135 34
fi
if [ ! -e /dev/circbuf3 ]; then
	mknod /dev/circbuf3 c 135 35
fi

if [ ! -e /dev/jpeghead0 ]; then
	mknod /dev/jpeghead0 c 135 48
fi
if [ ! -e /dev/jpeghead1 ]; then
	mknod /dev/jpeghead1 c 135 49
fi
if [ ! -e /dev/jpeghead2 ]; then
	mknod /dev/jpeghead2 c 135 50
fi
if [ ! -e /dev/jpeghead3 ]; then
	mknod /dev/jpeghead3 c 135 51
fi

# inable interrupts - test code in driver
echo 1 > /dev/circbuf0

/mnt/mmc/circbuf/imgsrv -p $PORT &

