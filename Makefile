PROGS      = imgsrv
PHPCLI = exif.php imu_setup.php start_gps_compass.php
CONFFILES  = Exif_template.xml 

SRCS = imgsrv.c
OBJS = imgsrv.o

#CFLAGS   += -Wall -I$(ELPHEL_KERNEL_DIR)/include/uapi/elphel
CFLAGS   += -Wall -I$(STAGING_DIR_HOST)/usr/include-uapi/elphel


all: $(PROGS)

$(PROGS): $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
clean:
	rm -rf $(PROGS) *.o core

#TODO: implement automatic dependencies!
imgsrv.c:$(STAGING_DIR_HOST)/usr/include-uapi/elphel/x393_devices.h
	