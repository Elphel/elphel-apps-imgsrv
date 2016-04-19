AXIS_USABLE_LIBS = UCLIBC GLIBC
#include $(AXIS_TOP_DIR)/tools/build/Rules.axis

INSTDIR    = $(prefix)/usr/local/sbin/
PHPDIR   = $(prefix)/usr/html/
CONFDIR   = $(prefix)/etc/
INSTMODE   = 0755
INSTOTHER  = 0644
INSTOWNER  = root
INSTGROUP  = root

INCDIR     = $(prefix)/include

PROGS      = imgsrv
#OTHERFILES = exif_init.php
PHPCLI = exif.php imu_setup.php start_gps_compass.php
CONFFILES  = Exif_template.xml 

SRCS = imgsrv.c

OBJS = imgsrv.o

CFLAGS   += -Wall -I$(ELPHEL_KERNEL_DIR)/include/elphel
#CFLAGS   += -Wall -I$(INCDIR) -I$(ELPHEL_KERNEL_DIR)/include 
#CFLAGS   += -Wall -I$(INCDIR) -I$(AXIS_KERNEL_DIR)/include -save-temps -dA -dP

all: $(PROGS)

$(PROGS): $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
#	cris-strip -s $@
install: $(PROGS)
	$(INSTALL) -d $(INSTDIR)
	$(INSTALL) -m $(INSTMODE) -o $(INSTOWNER) -g $(INSTGROUP) $(PROGS) $(INSTDIR)
	$(INSTALL) -d $(PHPDIR)
	$(INSTALL) -m $(INSTMODE) -o $(INSTOWNER) -g $(INSTGROUP) $(PHPCLI) $(PHPDIR)
	$(INSTALL) -d $(CONFDIR)
	$(INSTALL) -m $(INSTOTHER) -o $(INSTOWNER) -g $(INSTGROUP) $(CONFFILES) $(CONFDIR)
clean:
	rm -rf $(PROGS) *.o core
dependency:
	make depend
depend:
	makedepend -Y -- $(CFLAGS) -- $(SRCS) 2>/dev/null
	touch dependency
# DO NOT DELETE
