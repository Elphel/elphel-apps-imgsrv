PROGS      = imgsrv
PHPCLI = exif.php imu_setup.php start_gps_compass.php
CONFIGS  = Exif_template.xml 

SRCS = imgsrv.c
OBJS = imgsrv.o

INSTALL    = install
INSTMODE   = 0755
INSTDOCS   = 0644

OWN = -o root -g root

#CFLAGS   += -Wall -I$(ELPHEL_KERNEL_DIR)/include/uapi/elphel
CFLAGS   += -Wall -I$(STAGING_DIR_HOST)/usr/include-uapi

SYSCONFDIR = /etc
BINDIR     = /usr/bin
WWW_PAGES  = /www/pages

all: $(PROGS)

$(PROGS): $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

install: $(PROGS) $(PHPCLI) $(CONFIGS)
	$(INSTALL) $(OWN) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) $(OWN) -d $(DESTDIR)$(SYSCONFDIR)
	$(INSTALL) $(OWN) -d $(DESTDIR)$(WWW_PAGES)
	$(INSTALL) $(OWN) -m $(INSTMODE) $(PROGS)   $(DESTDIR)$(BINDIR)
	$(INSTALL) $(OWN) -m $(INSTDOCS) $(CONFIGS) $(DESTDIR)$(SYSCONFDIR) 
	$(INSTALL) $(OWN) -m $(INSTMODE) $(PHPCLI)  $(DESTDIR)$(WWW_PAGES) 

clean:
	rm -rf $(PROGS) *.o core

#TODO: implement automatic dependencies!
imgsrv.c:$(STAGING_DIR_HOST)/usr/include-uapi/elphel/x393_devices.h
	