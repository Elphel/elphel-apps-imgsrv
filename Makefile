PROGS      = imgsrv
PHPCLI = exif.php imu_setup.php start_gps_compass.php
CONFFILES  = Exif_template.xml 

SRCS = imgsrv.c
OBJS = imgsrv.o

CFLAGS   += -Wall -I$(ELPHEL_KERNEL_DIR)/include/elphel

all: $(PROGS)

$(PROGS): $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
clean:
	rm -rf $(PROGS) *.o core
