PROGS      = imgsrv
PHPCLI = exif.php imu_setup.php start_gps_compass.php
CONFFILES  = Exif_template.xml 

SRCS = imgsrv.c
OBJS = imgsrv.o

#CFLAGS   += -Wall -I$(ELPHEL_KERNEL_DIR)/include/uapi/elphel
CFLAGS   += -Wall -I$(STAGING_DIR_HOST)/usr/include-uapi/elphel

SYSCONFDIR = /etc/
BINDIR     = /usr/bin/
WWW_PAGES  = /www/pages

all: $(PROGS)

$(PROGS): $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

install: 
	install -d $(DESTDIR)$(BINDIR)
	install -m 0755 -t $(DESTDIR)$(BINDIR) $(PROGS)
	install -d $(DESTDIR)$(SYSCONFDIR)
	install -m 0644 -t $(DESTDIR)$(SYSCONFDIR) $(CONFIGS)
	install -d $(DESTDIR)$(WWW_PAGES)
	install -m 0755 -t $(DESTDIR)$(WWW_PAGES) $(PHPCLI) 

clean:
	rm -rf $(PROGS) *.o core

#TODO: implement automatic dependencies!
imgsrv.c:$(STAGING_DIR_HOST)/usr/include-uapi/elphel/x393_devices.h
	