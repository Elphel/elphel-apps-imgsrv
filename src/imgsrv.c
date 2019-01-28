/** @file imgsrv.c
 * @brief Simple and fast HTTP server to send camera still images
 * @copyright Copyright (C) 2007-2016 Elphel, Inc.
 *
 * @par <b>License</b>
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <string.h>
#include <syslog.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <asm/byteorder.h>
#include <elphel/c313a.h>
#include <elphel/exifa.h>
#include <elphel/x393_devices.h>

#define ELPHEL_DEBUG 0
#if ELPHEL_DEBUG
#define ELPHEL_DEBUG_THIS 1
#else
#define ELPHEL_DEBUG_THIS 0
#endif

//#define D1(x) fprintf(stderr, "%s:%d:%s: ", __FILE__, __LINE__, __FUNCTION__); x
#define D1(x)

#if ELPHEL_DEBUG_THIS
#define D(x) fprintf(stderr, "%s:%d:%s: ", __FILE__, __LINE__, __FUNCTION__); x
#else
#define D(x)
#endif

/** @brief HEADER_SIZE is defined to be larger than actual header (with EXIF) to use compile-time buffer */
#define JPEG_HEADER_MAXSIZE       0x300
/** @brief The size of trailing marker */
#define TRAILER_SIZE              0x02
/** @brief The length of MakerNote buffer in @e long */
#define MAKERNOTE_LEN             16
#ifndef GLOBALPARS_SNGL // until kernel recompiled, will go to c313a.h
#define GLOBALPARS_SNGL(x) (globalPars[(x)-FRAMEPAR_GLOBALS])     ///< should work in drivers and applications, First 32 parameter values are not erased with initGlobalPars
#endif

/**
 * @struct file_set
 * This structure holds a set of file names and file descriptors corresponding to
 * one sensor port.
 * @var file_set::port_num
 * Sensor port number this set corresponds to
 * @var file_set::cirbuf_fn
 * The file name of circular buffer
 * @var file_set::circbuf_fd
 * The file descriptor of circular buffer
 * @var file_set::jphead_fn
 * The file name of JPEG header buffer
 * @var file_set::jphead_fd
 * The file descriptor of JPEG header buffer
 * @var file_set::exif_dev_name
 * The file name of Exif buffer
 * @var file_set::exif_dev_fd
 * The file descriptor of Exif buffer
 * @var file_set::exifmeta_dev_name
 * The file name of Exif meta buffer
 * @var file_set::exifmeta_dev_fd
 * The file descriptor of Exif meta buffer
 */
struct file_set {
	unsigned short port_num;
	const char     *cirbuf_fn;
	int            circbuf_fd;
	const char     *jphead_fn;
	int            jphead_fd;
	const char     *exif_dev_name;
	int            exif_dev_fd;
	const char     *exifmeta_dev_name;
	int            exifmeta_dev_fd;
    const char     *framepars_dev_name;
    int            framepars_dev_fd;
    int            sensor_port;
    int            timestamp_name;
    int            base_chn;
};

static struct file_set  files[SENSOR_PORTS];

unsigned long * ccam_dma_buf;
char trailer[TRAILER_SIZE] = { 0xff, 0xd9 };
const char *circbuf_fnames[] = {
        DEV393_PATH(DEV393_CIRCBUF0),
        DEV393_PATH(DEV393_CIRCBUF1),
        DEV393_PATH(DEV393_CIRCBUF2),
        DEV393_PATH(DEV393_CIRCBUF3)
};
const char *jhead_fnames[] = {
        DEV393_PATH(DEV393_JPEGHEAD0),
        DEV393_PATH(DEV393_JPEGHEAD1),
        DEV393_PATH(DEV393_JPEGHEAD2),
        DEV393_PATH(DEV393_JPEGHEAD3)
};

static const char *exif_dev_names[SENSOR_PORTS] = {
        DEV393_PATH(DEV393_EXIF0),
        DEV393_PATH(DEV393_EXIF1),
        DEV393_PATH(DEV393_EXIF2),
        DEV393_PATH(DEV393_EXIF3)};
static const char *exifmeta_dev_names[SENSOR_PORTS] = {
        DEV393_PATH(DEV393_EXIF_META0),
        DEV393_PATH(DEV393_EXIF_META1),
        DEV393_PATH(DEV393_EXIF_META2),
        DEV393_PATH(DEV393_EXIF_META3)};
static const char *framepars_dev_names[SENSOR_PORTS] = {
        DEV393_PATH(DEV393_FRAMEPARS0),
        DEV393_PATH(DEV393_FRAMEPARS1),
        DEV393_PATH(DEV393_FRAMEPARS2),
        DEV393_PATH(DEV393_FRAMEPARS3)};




const char app_args[] = "Usage:\n%s -p <port_number_1> [<port_number_2> <port_number_3> <port_number_4>]\n" \
		"Start image server, bind it to ports <port_number_1> <port_number_2> <port_number_3> <port_number_4>\n" \
		"or to ports <port_number_1> <port_number_1 + 1> <port_number_1 + 2> <port_number_1 + 3> if " \
		"<port_number_2>, <port_number_3> and <port_number_4> are not provided\n";

const char url_args[] = "This server supports sequence of commands entered in the URL (separated by \"/\", case sensitive )\n"
		"executing them from left to right as they appear in the URL\n"
		"img -         send image at the current pointer (if it is first command - use last acquired image, if none available - send 1x1 gif)\n"
		"bimg -        same as above, but save the whole image in the memory before sending - useful to avoid overruns with slow connection \n"
		"simg, sbimg - similar to img, bimg but will immediately suggest to save instead of opening image in the browser\n"
		"mimg[n] -     send images as a multipart JPEG (all commands after will be ignored), possible to specify optional fps reduction\n"
		"              i.e. mimg4 - skip 3 frames for each frame output (1/4 fps) \n"
		"bmimg[n] -    same as above, buffered\n"
		"pointers -    send XML-formatted data about frames available in the camera circular buffer)\n"
		"frame -       return current frame number as plain text\n"
		"wframe -      wait for the next frame sync, return current frame number as plain text\n\n"
		"Any of the 7 commands above can appear only once in the URL string, the second instance will be ignored. If none of the 5\n"
		"are present in the URL 1x1 pixel gif will be returned to the client after executing the URL command.\n\n"
		"torp -   set frame pointer to global read pointer, maintained by the camera driver. If frame at that pointer\n"
		"         is invalid, use scnd (see below)\n"
		"towp -   set frame pointer to hardware write pointer - position where next frame will be acquired\n"
		"prev -   move to previous frame in buffer, nop if there are none\n"
		"next -   move to the next frame in buffer (will stop at write pointer, see towp above)\n"
		"last -   move to the most recently acquired frame, or to write pointer if there are none.\n"
		"         this command is implied at the start of the url command sequence.\n"
		"first -  move to the oldest frame still available in the buffer. It is not safe to rely on it if\n"
		"         more frames are expected - data might be overwritten at any moment and the output will be corrupted.\n"
		"second - move to the second oldest frame in the buffer - somewhat safer than \"first\" - there will be time for\n"
		"         \"next\" command at least before frame data (and pointer structures) will be overwritten.\n"
		"save   - save current frame pointer as a global read pointer that holds it values between server requests.\n"
		"         This pointer is shared between all the clients and applications that use it.\n"
		"wait   - Wait until there will be a frame at current pointer ready\n"
		"trig   - send a single internal trigger (and puts internal trigger in single-shot mode\n"
		"         In this special mode autoexposure/white balance will not work in most cases,\n"
		"         camera should be set in triggered mode (TRIG=4), internal (TRIG_CONDITION=0).\n"
		"         No effect on free-running or \"slave\" cameras, so it is OK to send it to all.\n"
        "timestamp_name - format file name as <seconds>_<useconds>_<channel>.<ext>\n"
		"bchn[n]- base channel = n, <channel> = n + port number (0-3) - for unique naming of multicamera systems."
		;


// path to file containing serial number
static const char path_to_serial[] = "/sys/devices/soc0/elphel393-init/serial";

int  sendImage(struct file_set *fset, int bufferImageData, int use_Exif, int saveImage);
void sendBuffer(void * buffer, int len);
void listener_loop(struct file_set *fset);
void errorMsgXML(char * msg);
int  framePointersXML(struct file_set *fset);
int  metaXML(struct file_set *fset, int mode); // mode: 0 - new (send headers), 1 - continue, 2 - finish
int  printExifXML(int exif_page, struct file_set *fset);
int  out1x1gif(void);
void waitFrameSync(struct file_set *fset);

unsigned long  getCurrentFrameNumberSensor    ( struct file_set *fset);
unsigned long  getCurrentFrameNumberCompressor( struct file_set *fset);

/** @brief Read no more then 255 bytes from file */
#define saferead255(f, d, l) read(f, d, ((l) < 256) ? (l) : 255)

/**
 * @brief read Camera Serial Number from SysFS
 * @param[in]   filename   file path
 * @param[in]   serial     pointer to buffer
 * @return      0 if data was extracted successfully and negative error code otherwise
 */
int readCameraSerialNumberFromSysFS(char *path,char *serial){
	int fd;
	fd = open(path,O_RDONLY);
	if (fd<0) return -1;
	read(fd,serial,strlen(serial));
	close(fd);
	return 0;
}

/**
 * @brief Print Exif data in XML format to @e stdout
 * @param[in]   exif_page   page number in Exif buffer
 * @param[in]   fset        file set for which the data should be printed
 * @return      0 if data was extracted successfully and negative error code otherwise
 */
int printExifXML(int exif_page, struct file_set *fset)
{
	int indx;
	long numfields = 0;
	struct exif_dir_table_t dir_table_entry;
	int fd_exifdir;
	struct exif_dir_table_t exif_dir[ExifKmlNumber]; // store locations of the fields needed for KML generations in the Exif block

	// Create Exif directory
	// Read the size  of the Exif data
	fd_exifdir = open(DEV393_PATH(DEV393_EXIF_METADIR), O_RDONLY);
	if (fd_exifdir < 0) {
		printf("<error>\"Opening %s\"</error>\n", DEV393_PATH(DEV393_EXIF_METADIR));
		return -2; // Error opening Exif directory
	}

	for (indx = 0; indx < ExifKmlNumber; indx++) exif_dir[indx].ltag = 0;
	while (read(fd_exifdir, &dir_table_entry, sizeof(dir_table_entry)) > 0) {
		switch (dir_table_entry.ltag) {
		case Exif_Image_ImageDescription:      indx = Exif_Image_ImageDescription_Index; break;
		case Exif_Image_ImageNumber:           indx = Exif_Image_ImageNumber_Index; break;
		case Exif_Photo_DateTimeOriginal:      indx = Exif_Photo_DateTimeOriginal_Index; break;
		case Exif_Photo_SubSecTimeOriginal:    indx = Exif_Photo_SubSecTimeOriginal_Index; break;
		case Exif_Photo_ExposureTime:          indx = Exif_Photo_ExposureTime_Index; break;
		case Exif_Photo_MakerNote:             indx = Exif_Photo_MakerNote_Index; break;
		case Exif_Image_Orientation:           indx = Exif_Image_Orientation_Index; break;
		case Exif_GPSInfo_GPSLatitudeRef:      indx = Exif_GPSInfo_GPSLatitudeRef_Index; break;
		case Exif_GPSInfo_GPSLatitude:         indx = Exif_GPSInfo_GPSLatitude_Index; break;
		case Exif_GPSInfo_GPSLongitudeRef:     indx = Exif_GPSInfo_GPSLongitudeRef_Index; break;
		case Exif_GPSInfo_GPSLongitude:        indx = Exif_GPSInfo_GPSLongitude_Index; break;
		case Exif_GPSInfo_GPSAltitudeRef:      indx = Exif_GPSInfo_GPSAltitudeRef_Index; break;
		case Exif_GPSInfo_GPSAltitude:         indx = Exif_GPSInfo_GPSAltitude_Index; break;
		case Exif_GPSInfo_GPSTimeStamp:        indx = Exif_GPSInfo_GPSTimeStamp_Index; break;
		case Exif_GPSInfo_GPSDateStamp:        indx = Exif_GPSInfo_GPSDateStamp_Index; break;
		case Exif_GPSInfo_GPSMeasureMode:      indx = Exif_GPSInfo_GPSMeasureMode_Index; break;
		case Exif_GPSInfo_CompassDirectionRef: indx = Exif_GPSInfo_CompassDirectionRef_Index; break;
		case Exif_GPSInfo_CompassDirection:    indx = Exif_GPSInfo_CompassDirection_Index; break;
		case Exif_GPSInfo_CompassPitchRef:     indx = Exif_GPSInfo_CompassPitchRef_Index; break;
		case Exif_GPSInfo_CompassPitch:        indx = Exif_GPSInfo_CompassPitch_Index; break;
		case Exif_GPSInfo_CompassRollRef:      indx = Exif_GPSInfo_CompassRollRef_Index; break;
		case Exif_GPSInfo_CompassRoll:         indx = Exif_GPSInfo_CompassRoll_Index; break;
		case Exif_Image_PageNumber:            indx = Exif_Image_PageNumber_Index; break;
		default: indx = -1;
		}
		if (indx >= 0) {
			memcpy(&(exif_dir[indx]), &dir_table_entry, sizeof(dir_table_entry));
			numfields++;
		}
	}
	close(fd_exifdir);
//    fprintf(stderr, "%s:%d:%s: Starting - 2\n", __FILE__, __LINE__, __FUNCTION__);

	// Create XML files itself
	long rational3[6];
	long makerNote[MAKERNOTE_LEN];
	long exif_page_start;
	char val[256];
	int hours = 0, minutes = 0;
	double seconds = 0.0;
	double longitude = 0.0, latitude = 0.0,  altitude = 0.0,  heading = 0.0,  roll = 0.0, pitch = 0.0, exposure = 0.0;
	const char CameraSerialNumber[] = "FFFFFFFFFFFF";
	val[255] = '\0';
//	int fd_exif = open(fset->exif_dev_name, O_RDONLY);
	if (fset->exif_dev_fd <0)
	    fset->exif_dev_fd = open(fset->exif_dev_name, O_RDONLY);
	if (fset->exif_dev_fd < 0) {
		printf("<error>\"Opening %s\"</error>\n", fset->exif_dev_name);
		return -3;
	}
	//	fset->exif_dev_fd = fd_exif;
	exif_page_start = lseek(fset->exif_dev_fd, exif_page, SEEK_END); // select specified Exif page
    printf("<debugExifPage>\"%d (0x%x)\"</debugExifPage>\n", exif_page,exif_page);
    printf("<debugExifPageStart>\"0x%08x\"</debugExifPageStart>\n", exif_page_start);
	if (exif_page_start < 0) {
		printf("<error>\"Exif page (%d) is out of range\"</error>\n", exif_page);
	    fprintf(stderr, "%s:%d:%s: Exif page (%d) is out of range\n", __FILE__, __LINE__, __FUNCTION__, exif_page);
		close(fset->exif_dev_fd);
		fset->exif_dev_fd = -1;
		return -1; // Error opening Exif
	}
	// Camera Serial Number
	readCameraSerialNumberFromSysFS(&path_to_serial,CameraSerialNumber);
	printf("<CameraSerialNumber>\"%.2s:%.2s:%.2s:%.2s:%.2s:%.2s\"</CameraSerialNumber>\n",
			&CameraSerialNumber[0],
			&CameraSerialNumber[2],
			&CameraSerialNumber[4],
			&CameraSerialNumber[6],
			&CameraSerialNumber[8],
			&CameraSerialNumber[10]);

	// Image Description
	if (exif_dir[Exif_Image_ImageDescription_Index].ltag == Exif_Image_ImageDescription) { // Exif_Image_ImageDescription is present in template
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_Image_ImageDescription_Index].dst,
				SEEK_SET);
		saferead255(fset->exif_dev_fd, val, exif_dir[Exif_Image_ImageDescription_Index].len);
		printf("<ImageDescription>\"%s\"</ImageDescription>\n", val);
	}
	// Exif_Image_ImageNumber_Index           0x13
	if (exif_dir[Exif_Image_ImageNumber_Index].ltag == Exif_Image_ImageNumber) { // Exif_Image_ImageNumber_Index is present in template
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_Image_ImageNumber_Index].dst,
				SEEK_SET);
		read(fset->exif_dev_fd, rational3, 4);
		sprintf(val, "%ld", (long)__cpu_to_be32( rational3[0]));
		printf("<ImageNumber>\"%s\"</ImageNumber>\n", val);
	}

	// Exif_Image_Orientation_Index           0x14
	if (exif_dir[Exif_Image_Orientation_Index].ltag == Exif_Image_Orientation) { // Exif_Image_Orientation_Index is present in template
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_Image_Orientation_Index].dst,
				SEEK_SET);
		rational3[0] = 0;
		read(fset->exif_dev_fd, rational3, 2);
		sprintf(val, "%ld", (long)( rational3[0] >> 8));
		printf("<Orientation>\"%s\"</Orientation>\n", val);
	}

	// Exif_Image_PageNumber
	if (exif_dir[Exif_Image_PageNumber_Index].ltag == Exif_Image_PageNumber) { // Exif_Image_Orientation_Index is present in template
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_Image_PageNumber_Index].dst,
				SEEK_SET);
		rational3[0] = 0;
		read(fset->exif_dev_fd, rational3, 2);
		sprintf(val, "%u", __cpu_to_be16(rational3[0]));
		printf("<SensorNumber>\"%s\"</SensorNumber>\n", val);
	}

	// DateTimeOriginal (with subseconds)
	if (exif_dir[Exif_Photo_DateTimeOriginal_Index].ltag == Exif_Photo_DateTimeOriginal) {
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_Photo_DateTimeOriginal_Index].dst,
				SEEK_SET);
		read(fset->exif_dev_fd, val, 19);
		val[19] = '\0';
		if (exif_dir[Exif_Photo_SubSecTimeOriginal_Index].ltag == Exif_Photo_SubSecTimeOriginal) {
			val[19] = '.';
			lseek(fset->exif_dev_fd,
					exif_page_start + exif_dir[Exif_Photo_SubSecTimeOriginal_Index].dst,
					SEEK_SET);
			read(fset->exif_dev_fd, &val[20], 7);
			val[27] = '\0';
		}
		printf("<DateTimeOriginal>\"%s\"</DateTimeOriginal>\n", val);
	}
	// Exif_Photo_ExposureTime
	if (exif_dir[Exif_Photo_ExposureTime_Index].ltag == Exif_Photo_ExposureTime) { // Exif_Photo_ExposureTime is present in template
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_Photo_ExposureTime_Index].dst,
				SEEK_SET);
		read(fset->exif_dev_fd, rational3, 8);
		exposure = (1.0 * __cpu_to_be32( rational3[0])) / __cpu_to_be32( rational3[1]);
		sprintf(val, "%f", exposure);
		printf("<ExposureTime>\"%s\"</ExposureTime>\n", val);
	}

	// Exif_Photo_MakerNote
	if (exif_dir[Exif_Photo_MakerNote_Index].ltag == Exif_Photo_MakerNote) { // Exif_Photo_MakerNote is present in template
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_Photo_MakerNote_Index].dst,
				SEEK_SET);
		read(fset->exif_dev_fd,  makerNote, 64);
		sprintf(val,
				"0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx,0x%08lx",
				(long)__cpu_to_be32(makerNote[0]),
				(long)__cpu_to_be32(makerNote[1]),
				(long)__cpu_to_be32(makerNote[2]),
				(long)__cpu_to_be32(makerNote[3]),
				(long)__cpu_to_be32(makerNote[4]),
				(long)__cpu_to_be32(makerNote[5]),
				(long)__cpu_to_be32(makerNote[6]),
				(long)__cpu_to_be32(makerNote[7]),
				(long)__cpu_to_be32(makerNote[8]),
				(long)__cpu_to_be32(makerNote[9]),
				(long)__cpu_to_be32(makerNote[10]),
				(long)__cpu_to_be32(makerNote[11]),
				(long)__cpu_to_be32(makerNote[12]),
				(long)__cpu_to_be32(makerNote[13]),
				(long)__cpu_to_be32(makerNote[14]),
				(long)__cpu_to_be32(makerNote[15]));
		printf("<MakerNote>\"%s\"</MakerNote>\n", val);
	}

	// GPS measure mode
	if (exif_dir[Exif_GPSInfo_GPSMeasureMode_Index].ltag == Exif_GPSInfo_GPSMeasureMode) {
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_GPSInfo_GPSMeasureMode_Index].dst,
				SEEK_SET);
		read(fset->exif_dev_fd, val, 1);
		val[1] = '\0';
		printf("<GPSMeasureMode>\"%s\"</GPSMeasureMode>\n", val);
	}

	// GPS date/time
	if (exif_dir[Exif_GPSInfo_GPSDateStamp_Index].ltag == Exif_GPSInfo_GPSDateStamp) {
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_GPSInfo_GPSDateStamp_Index].dst,
				SEEK_SET);
		read(fset->exif_dev_fd, val, 10);
		val[10] = '\0';
		if (exif_dir[Exif_GPSInfo_GPSTimeStamp_Index].ltag == Exif_GPSInfo_GPSTimeStamp) {
			lseek(fset->exif_dev_fd,
					exif_page_start + exif_dir[Exif_GPSInfo_GPSTimeStamp_Index].dst,
					SEEK_SET);
			read(fset->exif_dev_fd, rational3, 24);
			hours =   __cpu_to_be32( rational3[0]);
			minutes = __cpu_to_be32( rational3[2]);
			seconds = (1.0 * (__cpu_to_be32( rational3[4]) + 1)) / __cpu_to_be32( rational3[5]); // GPS likes ".999", let's inc by one - anyway will round that out
			sprintf(&val[10], " %02d:%02d:%05.2f", hours, minutes, seconds);
		}
		printf("<GPSDateTime>\"%s\"</GPSDateTime>\n", val);
/*
        printf("<DBGGPSDateTime>\"0x%x 0x%x 0x%x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\"</DBGGPSDateTime>\n",
                Exif_GPSInfo_GPSDateStamp, Exif_GPSInfo_GPSDateStamp_Index, exif_dir[Exif_GPSInfo_GPSTimeStamp_Index].dst,
                val[0],val[1],val[2],val[3],val[4],val[5],val[6],val[7],val[8],val[9],val[10],val[11],val[12]);
*/
	}

	// knowing format provided from GPS - degrees and minutes only, no seconds:
	// GPS Longitude
	if (exif_dir[Exif_GPSInfo_GPSLongitude_Index].ltag == Exif_GPSInfo_GPSLongitude) { // Exif_GPSInfo_GPSLongitude is present in template
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_GPSInfo_GPSLongitude_Index].dst,
				SEEK_SET);
		read(fset->exif_dev_fd, rational3, 24);
		longitude = __cpu_to_be32( rational3[0]) / (1.0 * __cpu_to_be32( rational3[1])) + __cpu_to_be32( rational3[2]) / (60.0 * __cpu_to_be32( rational3[3]));
		if (exif_dir[Exif_GPSInfo_GPSLongitudeRef_Index].ltag == Exif_GPSInfo_GPSLongitudeRef) {
			lseek(fset->exif_dev_fd,
					exif_page_start + exif_dir[Exif_GPSInfo_GPSLongitudeRef_Index].dst,
					SEEK_SET);
			read(fset->exif_dev_fd, val, 1);
			if (val[0] != 'E') longitude = -longitude;
		}
		sprintf(val, "%f", longitude);
		printf("<GPSLongitude>\"%s\"</GPSLongitude>\n", val);
	}
	// GPS Latitude
	if (exif_dir[Exif_GPSInfo_GPSLatitude_Index].ltag == Exif_GPSInfo_GPSLatitude) { // Exif_GPSInfo_GPSLatitude is present in template
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_GPSInfo_GPSLatitude_Index].dst,
				SEEK_SET);
		read(fset->exif_dev_fd, rational3, 24);
		latitude = __cpu_to_be32( rational3[0]) / (1.0 * __cpu_to_be32( rational3[1])) + __cpu_to_be32( rational3[2]) / (60.0 * __cpu_to_be32( rational3[3]));
		if (exif_dir[Exif_GPSInfo_GPSLatitudeRef_Index].ltag == Exif_GPSInfo_GPSLatitudeRef) {
			lseek(fset->exif_dev_fd,
					exif_page_start + exif_dir[Exif_GPSInfo_GPSLatitudeRef_Index].dst,
					SEEK_SET);
			read(fset->exif_dev_fd, val, 1);
			if (val[0] != 'N') latitude = -latitude;
		}
		sprintf(val, "%f", latitude);
		printf("<GPSLatitude>\"%s\"</GPSLatitude>\n", val);
	}
	// GPS Altitude
	if (exif_dir[Exif_GPSInfo_GPSAltitude_Index].ltag == Exif_GPSInfo_GPSAltitude) { // Exif_GPSInfo_GPSAltitude is present in template
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_GPSInfo_GPSAltitude_Index].dst,
				SEEK_SET);
		read(fset->exif_dev_fd, rational3, 8);
		altitude = (1.0 * __cpu_to_be32( rational3[0])) / __cpu_to_be32( rational3[1]);

		if (exif_dir[Exif_GPSInfo_GPSAltitudeRef_Index].ltag == Exif_GPSInfo_GPSAltitudeRef) {
			lseek(fset->exif_dev_fd,
					exif_page_start + exif_dir[Exif_GPSInfo_GPSAltitudeRef_Index].dst,
					SEEK_SET);
			read(fset->exif_dev_fd, val, 1);
			if (val[0] != '\0') altitude = -altitude;
		}
		sprintf(val, "%f", altitude);
		printf("<GPSAltitude>\"%s\"</GPSAltitude>\n", val);
	}
	// Compass Direction (magnetic)
	if (exif_dir[Exif_GPSInfo_CompassDirection_Index].ltag == Exif_GPSInfo_CompassDirection) { // Exif_GPSInfo_CompassDirection is present in template
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_GPSInfo_CompassDirection_Index].dst,
				SEEK_SET);
		read(fset->exif_dev_fd, rational3, 8);
		heading = (1.0 * __cpu_to_be32( rational3[0])) / __cpu_to_be32( rational3[1]);
		sprintf(val, "%f", heading);
		printf("<CompassDirection>\"%s\"</CompassDirection>\n", val);
	}
	// Processing 'hacked' pitch and roll (made of Exif destination latitude/longitude)
	// Compass Roll
	if (exif_dir[Exif_GPSInfo_CompassRoll_Index].ltag == Exif_GPSInfo_CompassRoll) { // Exif_GPSInfo_CompassRoll is present in template
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_GPSInfo_CompassRoll_Index].dst,
				SEEK_SET);
		read(fset->exif_dev_fd, rational3, 8);
		roll = (1.0 * __cpu_to_be32( rational3[0])) / __cpu_to_be32( rational3[1]);

		if (exif_dir[Exif_GPSInfo_CompassRollRef_Index].ltag == Exif_GPSInfo_CompassRollRef) {
			lseek(fset->exif_dev_fd,
					exif_page_start + exif_dir[Exif_GPSInfo_CompassRollRef_Index].dst,
					SEEK_SET);
			read(fset->exif_dev_fd, val, 1);
			if (val[0] != EXIF_COMPASS_ROLL_ASCII[0]) roll = -roll;
		}
		sprintf(val, "%f", roll);
		printf("<CompassRoll>\"%s\"</CompassRoll>\n", val);
	}

	// Compass Pitch
	if (exif_dir[Exif_GPSInfo_CompassPitch_Index].ltag == Exif_GPSInfo_CompassPitch) { // Exif_GPSInfo_CompassPitch is present in template
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_GPSInfo_CompassPitch_Index].dst,
				SEEK_SET);
		read(fset->exif_dev_fd, rational3, 8);
		pitch = (1.0 * __cpu_to_be32( rational3[0])) / __cpu_to_be32( rational3[1]);

		if (exif_dir[Exif_GPSInfo_CompassPitchRef_Index].ltag == Exif_GPSInfo_CompassPitchRef) {
			lseek(fset->exif_dev_fd,
					exif_page_start + exif_dir[Exif_GPSInfo_CompassPitchRef_Index].dst,
					SEEK_SET);
			read(fset->exif_dev_fd, val, 1);
			if (val[0] != EXIF_COMPASS_PITCH_ASCII[0]) pitch = -pitch;
		}
		sprintf(val, "%f", pitch);
		printf("<CompassPitch>\"%s\"</CompassPitch>\n", val);
	}
//    fprintf(stderr, "%s:%d:%s: Exif done\n", __FILE__, __LINE__, __FUNCTION__);
	close(fset->exif_dev_fd);
	fset->exif_dev_fd = -1;
	return 0;
}

/**
 * @brief Print frame parameters and Exif data to @e stdout
 * @param[in]   fset   file set for which the data should be printed
 * @param[in]   mode   mode of operation. Can be one of the following:
 * mode | action
 * -----|-------
 * 0    | send new page including HTTP header
 * 1    | continue sending XML tags
 * 2    | finish and send closing XML tag
 * @return      0 if data was sent successfully and -1 if a frame was not found at
 * the location pointer by JPEG pointer
 */
int  metaXML(struct file_set *fset, int mode)
{
	int frameParamPointer = 0;
	struct interframe_params_t frame_params;
	int jpeg_len, jpeg_start, buff_size, timestamp_start;
	int frame_number,compressed_frame_number;
	// Adding current frame number (as in pointers)
    if (fset->framepars_dev_fd <0)
        fset->framepars_dev_fd = open(fset->framepars_dev_name, O_RDWR);
    if (fset->framepars_dev_fd < 0) { // check control OK
        printf("Error opening %s\n", fset->framepars_dev_name);
        fprintf(stderr, "%s:%d:%s: Error opening %s\n", __FILE__, __LINE__, __FUNCTION__, fset->framepars_dev_name);
        fset->framepars_dev_fd = -1;
        return -1;
    }
    if (fset->circbuf_fd <0) {
        fset->circbuf_fd = open(fset->cirbuf_fn, O_RDWR);
    }
    if (fset->circbuf_fd <0) {
        printf("Error opening %s\n", fset->cirbuf_fn);
        fprintf(stderr, "%s:%d:%s: Error opening %s\n", __FILE__, __LINE__, __FUNCTION__, fset->cirbuf_fn);
    }
    frame_number =            lseek(fset->framepars_dev_fd, 0, SEEK_CUR );
    compressed_frame_number = lseek(fset->circbuf_fd, LSEEK_CIRC_GETFRAME, SEEK_END );

	if (mode == 2) { // just close the xml file
		printf("</meta>\n");
		return 0;
	} else if (mode == 0) { // open the XML output (length is undefined - multiple frames meta data might be output)
		printf("HTTP/1.0 200 OK\r\n");
		printf("Server: Elphel Imgsrv\r\n");
		printf("Access-Control-Allow-Origin: *\r\n");
		printf("Access-Control-Expose-Headers: Content-Disposition\r\n");
		printf("Content-Type: text/xml\r\n");
		printf("Pragma: no-cache\r\n");
		printf("\r\n");
		printf("<?xml version=\"1.0\"?>\n" \
				"<meta>\n");
	}

	jpeg_start = lseek(fset->circbuf_fd, 0, SEEK_CUR); //get the current read pointer
//    jpeg_start = lseek(fset->circbuf_fd, 1, SEEK_CUR)-1; //get the current read pointer
	D(fprintf(stderr, "jpeg_start= (long)=0x%x\n", jpeg_start >> 2));
	if (jpeg_start < 0 ) {
		printf("<error>\"No frame at the current pointer\"</error>\n");
		return -1;
	}
	buff_size = lseek(fset->circbuf_fd, 0, SEEK_END);
	// restore file pointer after lseek-ing the end
	lseek(fset->circbuf_fd, jpeg_start, SEEK_SET);

	frameParamPointer = jpeg_start - 32;
	if (frameParamPointer < 0) frameParamPointer += buff_size;
	memcpy(&frame_params, (unsigned long* )&ccam_dma_buf[frameParamPointer >> 2], 32); // ccam_dma_buf - global
	jpeg_len=frame_params.frame_length;
	// Copy timestamp (goes after the image data)
	timestamp_start=jpeg_start+((jpeg_len+CCAM_MMAP_META+3) & (~0x1f)) + 32 - CCAM_MMAP_META_SEC; // magic shift - should index first byte of the time stamp
	if (timestamp_start >= buff_size) timestamp_start-=buff_size;
	memcpy (&(frame_params.timestamp_sec), (unsigned long * ) &ccam_dma_buf[timestamp_start>>2],8);
	printf ("<frame>\n" \
			"<start>    0x%x </start>\n" \
			"<hash32_r> 0x%x </hash32_r>\n" \
			"<hash32_g> 0x%x </hash32_g>\n" \
			"<hash32_gb>0x%x </hash32_gb>\n" \
			"<hash32_b> 0x%x </hash32_b>\n" \
			"<quality2> 0x%x </quality2>\n" \
			"<color> 0x%x </color>\n" \
			"<byrshift> 0x%x </byrshift>\n" \
			"<width> 0x%x </width>\n" \
			"<height> 0x%x </height>\n" \
			"<meta_index> 0x%x </meta_index>\n" \
			"<timestamp>  %ld.%06ld</timestamp>\n" \
			"<signffff> 0x%x </signffff>\n" \
            "<currentSensorFrame>%d</currentSensorFrame>\n" \
            "<latestCompressedFrame>%d</latestCompressedFrame>\n"\
            "<timestampHex>  %08x %08x</timestampHex>\n" \
            "<currentSensorFrameHex>0x%08x</currentSensorFrameHex>\n" \
            "<latestCompressedFrameHex>0x%08x</latestCompressedFrameHex>\n"

			, (int) jpeg_start
			, (int) frame_params.hash32_r
			, (int) frame_params.hash32_g
			, (int) frame_params.hash32_gb
			, (int) frame_params.hash32_b
			, (int) frame_params.quality2
			, (int) frame_params.color          // color mode //18
			, (int) frame_params.byrshift       // bayer shift in compressor //19
			, (int) frame_params.width          // frame width, pixels   20-21 - NOTE: should be 20-21
			, (int) frame_params.height         // frame height, pixels  22-23
			, (int) frame_params.meta_index     // index of the linked meta page
			,       frame_params.timestamp_sec
			,       frame_params.timestamp_usec
			, (int) frame_params.signffff
            , (int) frame_number
            , (int) compressed_frame_number
            ,       frame_params.timestamp_sec
            ,       frame_params.timestamp_usec
            , (int) frame_number
            , (int) compressed_frame_number
	);
	// 28-31 unsigned long  timestamp_sec ; //! number of seconds since 1970 till the start of the frame exposure
	// 28-31 unsigned long  frame_length ;  //! JPEG frame length in circular buffer, bytes
	//          };
	// 32-35 unsigned long  timestamp_usec; //! number of microseconds to add

	if (frame_params.signffff !=0xffff) {
		printf("<error>\"wrong signature (should be 0xffff)\"</error>\n");
	} else {
		// Put Exif data here
		printf ("<Exif>\n");
		printExifXML(frame_params.meta_index, fset);
		printf ("</Exif>\n");
	}
	printf ("</frame>\n");
	return 0;
}

/**
 * @brief Read current frame number from frame parameters buffer
 * @return frame number
 */
// TODO NC393 - now 2 types of current frame - one being acquired from sensor and the one already compressed
// First one comes from framepars, second - from circbuf (that is more interesting for the imgsrv)
unsigned long  getCurrentFrameNumberSensor(struct file_set *fset)
{
	unsigned long frame_number;
	if (fset->framepars_dev_fd <0)
	    fset->framepars_dev_fd = open(fset->framepars_dev_name, O_RDWR);
    if (fset->framepars_dev_fd <0) {
        printf("Error opening %s\n", fset->framepars_dev_name);
        fprintf(stderr, "%s:%d:%s: Error opening %s\n", __FILE__, __LINE__, __FUNCTION__, fset->framepars_dev_name);
        return 0;
    }
	frame_number = lseek(fset->framepars_dev_fd, 0, SEEK_CUR );
    fprintf(stderr, "%s:%d:%s: frame_number= %d\n", __FILE__, __LINE__, __FUNCTION__, frame_number);
	return frame_number;
}

unsigned long  getCurrentFrameNumberCompressor(struct file_set *fset)
{
    unsigned long frame_number;
    if (fset->circbuf_fd <0) {
        fset->circbuf_fd = open(fset->cirbuf_fn, O_RDWR);
    }
    if (fset->circbuf_fd <0) {
        printf("Error opening %s\n", fset->cirbuf_fn);
        fprintf(stderr, "%s:%d:%s: Error opening %s\n", __FILE__, __LINE__, __FUNCTION__, fset->cirbuf_fn);
        return 0;
    }
    frame_number = lseek(fset->circbuf_fd, LSEEK_CIRC_GETFRAME, SEEK_END );
    fprintf(stderr, "%s:%d:%s: frame_number= %d\n", __FILE__, __LINE__, __FUNCTION__, frame_number);
    return frame_number;
}

/**
 * @brief Wait for the next frame sync (frame captured is handled differently - through wait command)
 * @return None
 */

void waitFrameSync(struct file_set *fset)
{
    if (fset->framepars_dev_fd <0)
        fset->framepars_dev_fd = open(fset->framepars_dev_name, O_RDWR);
    if (fset->framepars_dev_fd <0) {
        printf("Error opening %s\n", fset->framepars_dev_name);
        fprintf(stderr, "%s:%d:%s: Error opening %s\n", __FILE__, __LINE__, __FUNCTION__, fset->framepars_dev_name);
        return;
    }

    lseek(fset->framepars_dev_fd, LSEEK_FRAME_WAIT_REL + 1, SEEK_END); // skip 1 frame before returning
    fprintf(stderr, "%s:%d:%s: \n", __FILE__, __LINE__, __FUNCTION__);
}


/**
 * @brief Read circular buffer pointers and write them to @e stdout in XML format
 * @param[in]   fset   file set for which the data should be printed
 * @return      0 if data was sent successfully and -1 in case of an error
 */
int framePointersXML(struct file_set *fset)
{
//	const char ctlFileName[] = "/dev/frameparsall";
//	int fd_fparmsall;
	// set to 768 - sizeof(s) can be more than 512
	char s[768]; // 512 // 341;
	int p, wp, rp;
	int nf = 0;
	int nfl = 0;
	int buf_free, buf_used, frame_size;
	int save_p; // save current file pointer, then restore it before return
	int frame16, frame_number, compressed_frame_number, sensor_state, compressor_state;
	char *cp_sensor_state, *cp_compressor_state;

#if 0
	struct framepars_all_t   **frameParsAll;
	struct framepars_t       *aframePars[SENSOR_PORTS];
	unsigned long            *aglobalPars[SENSOR_PORTS];
#else
	//Need to mmap just one port
    struct framepars_all_t   *frameParsAll;
    struct framepars_t       *framePars;
    unsigned long            *globalPars;


#endif
//	int fd_circ = fset->circbuf_fd;
//    fprintf(stderr, "%s:%d:%s: Starting\n", __FILE__, __LINE__, __FUNCTION__);
    if (fset->framepars_dev_fd <0)
        fset->framepars_dev_fd = open(fset->framepars_dev_name, O_RDWR);
	if (fset->framepars_dev_fd < 0) { // check control OK
		printf("Error opening %s\n", fset->framepars_dev_name);
		fprintf(stderr, "%s:%d:%s: Error opening %s\n", __FILE__, __LINE__, __FUNCTION__, fset->framepars_dev_name);
		fset->framepars_dev_fd = -1;
		return -1;
	}
    if (fset->circbuf_fd <0) {
        fset->circbuf_fd = open(fset->cirbuf_fn, O_RDWR);
    }
    if (fset->circbuf_fd <0) {
        printf("Error opening %s\n", fset->cirbuf_fn);
        fprintf(stderr, "%s:%d:%s: Error opening %s\n", __FILE__, __LINE__, __FUNCTION__, fset->cirbuf_fn);
    }
	// now try to mmap
	frameParsAll = (struct framepars_all_t*) mmap(0, sizeof(struct framepars_all_t), PROT_READ, MAP_SHARED, fset->framepars_dev_fd, 0);
	if ((int)frameParsAll == -1) {
		frameParsAll = NULL;
		printf("Error in mmap /dev/frameparsall");
		fprintf(stderr, "%s:%d:%s: Error in mmap in %s\n", __FILE__, __LINE__, __FUNCTION__, fset->framepars_dev_name);
		close(fset->framepars_dev_fd);
		fset->framepars_dev_fd = -1;
		return -1;
	}
    framePars=frameParsAll->framePars;
    globalPars=frameParsAll->globalPars;

//	for (int j = 0; j < SENSOR_PORTS; j++) {
//		aframePars[j] = frameParsAll[j]->framePars;
//		aglobalPars[j] = frameParsAll[j]->globalPars;
//	}
//->port_num
	// Read current sensor state - defined in c313a.h
	frame_number =            lseek(fset->framepars_dev_fd, 0, SEEK_CUR );
	compressed_frame_number = lseek(fset->circbuf_fd, LSEEK_CIRC_GETFRAME, SEEK_END );
	frame16 = frame_number & PARS_FRAMES_MASK;
	sensor_state = framePars[frame16].pars[P_SENSOR_RUN];
	compressor_state = framePars[frame16].pars[P_COMPRESSOR_RUN];
	cp_sensor_state =     (sensor_state == 0) ?
			"SENSOR_RUN_STOP" :
			((sensor_state == 1) ?
					"SENSOR_RUN_SINGLE" :
					((sensor_state == 2) ? "SENSOR_RUN_CONT" : "UNKNOWN"));
	cp_compressor_state = (compressor_state == 0) ?
			"COMPRESSOR_RUN_STOP" :
			((compressor_state == 1) ?
					"COMPRESSOR_RUN_SINGLE" :
					((compressor_state == 2) ? "COMPRESSOR_RUN_CONT" : "UNKNOWN"));

	save_p = lseek(fset->circbuf_fd, 0, SEEK_CUR);           // save current file pointer before temporarily moving it
	rp = lseek(fset->circbuf_fd, LSEEK_CIRC_TORP, SEEK_END); // set current rp global rp (may be invalid <0)
	wp = lseek(fset->circbuf_fd, LSEEK_CIRC_TOWP, SEEK_END); // set current rp pointer to FPGA write pointer
	p = wp;
	while ((p >= 0) & (nf < 500)) {
		if (p == rp) nfl = nf;
		p = lseek(fset->circbuf_fd, LSEEK_CIRC_PREV, SEEK_END);
		nf++;
	}
	buf_free = GLOBALPARS_SNGL(G_FREECIRCBUF);
	frame_size = GLOBALPARS_SNGL(G_FRAME_SIZE);

	lseek(fset->circbuf_fd, save_p, SEEK_SET);                       // restore file pointer after temporarily moving it
	buf_free = lseek(fset->circbuf_fd, LSEEK_CIRC_FREE, SEEK_END);   // will change file pointer
	lseek(fset->circbuf_fd, save_p, SEEK_SET);                       // restore file pointer after temporarily moving it
	buf_used = lseek(fset->circbuf_fd, LSEEK_CIRC_USED, SEEK_END);   // will change file pointer
	lseek(fset->circbuf_fd, save_p, SEEK_SET);                       // restore file pointer after temporarily moving it

	sprintf(s, "<?xml version=\"1.0\"?>\n" \
			"<frame_pointers>\n" \
			"  <this>%d</this>\n" \
			"  <write>%d</write>\n"	\
			"  <read>%d</read>\n" \
			"  <frames>%d</frames>\n" \
			"  <left>%d</left>\n" \
			"  <free>%d</free>\n" \
			"  <used>%d</used>\n" \
			"  <frame>%d</frame>\n"	\
            "  <frameHex>0x%x</frameHex>\n" \
            "  <compressedFrame>%d</compressedFrame>\n" \
            "  <compressedFrameHex>0x%x</compressedFrameHex>\n" \
            "  <lag>%d</lag>\n" \
			"  <frame_size>%d</frame_size>\n" \
			"  <sensor_state>\"%s\"</sensor_state>\n" \
			"  <compressor_state>\"%s\"</compressor_state>\n" \
			"</frame_pointers>\n",
			save_p,
			wp,
			rp,
			nf - 1,
			nfl,
			buf_free,
			buf_used,
			frame_number,
            frame_number,
			compressed_frame_number,
            compressed_frame_number,
            frame_number - compressed_frame_number,
			frame_size,
			cp_sensor_state,
			cp_compressor_state);
	printf("HTTP/1.0 200 OK\r\n");
	printf("Server: Elphel Imgsrv\r\n");
	printf("Access-Control-Allow-Origin: *\r\n");
	printf("Access-Control-Expose-Headers: Content-Disposition\r\n");
	printf("Content-Length: %d\r\n", strlen(s));
	printf("Content-Type: text/xml\r\n");
	printf("Pragma: no-cache\r\n");
	printf("\r\n");
	printf("%s",s);

	munmap(frameParsAll, sizeof(struct framepars_all_t));
	close(fset->framepars_dev_fd);
    fset->framepars_dev_fd = -1;
	D(fprintf(stderr, ">%s< [%d bytes]\n", s, strlen(s)));
	return 0;
}

/**
 * @brief Send 1x1 pixel GIF file as indication of an error
 * @return this function always returns 0
 */
int out1x1gif(void)
{
	char s[] = "HTTP/1.0 200 OK\r\n" \
			"Server: Elphel Imgsrv\r\n" \
			"Access-Control-Allow-Origin: *\r\n" \
			"Access-Control-Expose-Headers: Content-Disposition\r\n" \
			"Content-Length: 35\r\n" \
			"Content-Type: image/gif\r\n" \
			"\r\n" \
			"GIF87a\x01\x00\x01\x00\x80\x01\x00\x00\x00\x00" \
			"\xff\xff\xff\x2c\x00\x00\x00\x00\x01\x00\x01\x00\x00\x02\x02\x4c" \
			"\x01\x00\x3b";

	fwrite(s, 1, sizeof(s), stdout);        // we have zeros in the string
	return 0;                               // always good
}

/**
 * @brief Print error message to @e stdout in XML format
 * @param[in]   msg   error message string
 * @return      None
 */
void errorMsgXML(char * msg)
{
	char s[1024];

	sprintf(s, "<?xml version=\"1.0\"?>\n" \
			"<frame_params>\n" \
			"<error>%s</error>\n" \
			"</frame_params>\n", msg);
	D(fprintf(stderr, ">%s< [%d bytes]", s, strlen(s)));
	printf("HTTP/1.0 200 OK\r\n");
	printf("Server: Elphel Imgsrv\r\n");
	printf("Access-Control-Allow-Origin: *\r\n");
	printf("Access-Control-Expose-Headers: Content-Disposition\r\n");
	printf("Content-Length: %d\r\n", strlen(s));
	printf("Content-Type: text/xml\r\n");
	printf("Pragma: no-cache\r\n");
	printf("\r\n");
	printf("%s",s);
}

/**
 * @brief Read, prepare and send single image file
 *
 * This function reads image file data from circular buffer, JPEG header data from
 * JPEG header buffer and optionally Exif data from Exif buffer and then sends
 * prepared JPEG file to @e stdout
 * @param[in]   fset            file set for which the data should be obtained
 * @param[in]   bufferImageData write image data to buffer before sending
 * @param[in]   use_Exif        flag indicating that Exif should be used
 * @param[in]   saveImage       open file save dialog in web browser
 * @return      0 if file was successfully sent and negative error code otherwise
 */
int  sendImage(struct file_set *fset, int bufferImageData, int use_Exif, int saveImage)
{
	int exifDataSize = 0;
	int frameParamPointer = 0;
	struct interframe_params_t frame_params;
	int buff_size;
	int jpeg_len;   // bytes
	int jpeg_start; // bytes
	int head_size;
	int jpeg_full_size, jpeg_this_size;
	char * jpeg_copy;
	int l, i;
	int color_mode;
	char * mime_type;
	char * extension;
	int timestamp_start;

	jpeg_start = lseek(fset->circbuf_fd, 0, SEEK_CUR);     //get the current read pointer
//    jpeg_start = lseek(fset->circbuf_fd, 1, SEEK_CUR)-1;   //just for testing, until rebuilt/rebooted kernel


	D(fprintf(stderr, "jpeg_start (long) = 0x%x\n", jpeg_start));
	if (fset->jphead_fd<0)
	    fset->jphead_fd = open(fset->jphead_fn, O_RDWR);
	if (fset->jphead_fd < 0) { // check control OK
		fprintf(stderr, "Error opening %s\n", fset->jphead_fn);
		return -1;
	}
//	fset->jphead_fd = fd_head;
	lseek(fset->jphead_fd, jpeg_start + 1, SEEK_END);              // create JPEG header, find out it's size
	head_size = lseek(fset->jphead_fd, 0, SEEK_END);
	if (head_size > JPEG_HEADER_MAXSIZE) {
		fprintf(stderr, "%s:%d: Too big JPEG header (%d > %d)", __FILE__, __LINE__, head_size, JPEG_HEADER_MAXSIZE );
		close(fset->jphead_fd);
		fset->jphead_fd = -1;
		return -2;
	}
	/* find total buffer length (it is in defines, actually in c313a.h */
	buff_size = lseek(fset->circbuf_fd, 0, SEEK_END); // it is supposed to be open?
	/* restore file poinetr after lseek-ing the end */
	lseek(fset->circbuf_fd, jpeg_start, SEEK_SET);
	D(fprintf(stderr, "position (longs) = 0x%x\n", (int)lseek(fset->circbuf_fd, 0, SEEK_CUR)));

	/* now let's try mmap itself */
	frameParamPointer = jpeg_start - sizeof(struct interframe_params_t) + 4;
	if (frameParamPointer < 0)
		frameParamPointer += buff_size;
//	fprintf(stderr, "frameParamPointer = 0x%x, jpeg_start = 0x%x, buff_size = 0x%x\n",frameParamPointer, jpeg_start, buff_size);
	memcpy(&frame_params, (unsigned long*)&ccam_dma_buf[frameParamPointer >> 2], sizeof(struct interframe_params_t) - 4);
	jpeg_len = frame_params.frame_length;
	color_mode = frame_params.color;
	if (frame_params.signffff != 0xffff) {
		fprintf(stderr, "wrong signature signff = 0x%x \n", (int)frame_params.signffff);
		for (i=0; i< sizeof(struct interframe_params_t)/4;i++){
		    fprintf(stderr, "%08lx ",ccam_dma_buf[(frameParamPointer >> 2) + i]);
		    if (!((i+1) & 0x7)){
		        fprintf(stderr, "\n ");
		    }

		}
#if ELPHEL_DEBUG_THIS
		lseek(fset->circbuf_fd, LSEEK_CIRC_STOP_COMPRESSOR, SEEK_END);
#endif
		close(fset->jphead_fd);
		fset->jphead_fd = -1;
		return -4;
	}

    // Copy timestamp (goes after the image data)
    timestamp_start=jpeg_start+((jpeg_len+CCAM_MMAP_META+3) & (~0x1f)) + 32 - CCAM_MMAP_META_SEC; // magic shift - should index first byte of the time stamp
    if (timestamp_start >= buff_size) timestamp_start-=buff_size;
    memcpy (&(frame_params.timestamp_sec), (unsigned long * ) &ccam_dma_buf[timestamp_start>>2],8);

	if (use_Exif) {
		D(fprintf(stderr,"frame_params.meta_index=0x%x\n",(int) frame_params.meta_index));
		// read Exif to buffer:
		if (fset->exif_dev_fd <0)
		    fset->exif_dev_fd = open(fset->exif_dev_name, O_RDONLY);
		if (fset->exif_dev_fd < 0) {                                 // check control OK
			fprintf(stderr, "Error opening %s\n", fset->exif_dev_name);
			close(fset->jphead_fd);
			fset->jphead_fd = -1;
			return -5;
		}
		exifDataSize = lseek(fset->exif_dev_fd, 1, SEEK_END);        // at the beginning of page 1 - position == page length
		if (exifDataSize < 0) exifDataSize = 0;            // error from lseek;
		if (!exifDataSize){
		    close(fset->exif_dev_fd);
		    fset->exif_dev_fd = -1;
		}

	} else {
		frame_params.meta_index=0;
		fset->exif_dev_fd = -1;
	}
	// Maybe make buffer that will fit both Exif and JPEG?
	// Get metadata, update Exif and JFIF headers if ep and ed pointers are non-zero (NULL will generate files with JFIF-only headers)
	// Now we always malloc buffer, before - only for bimg, using fixed-size header buffer - was it faster?

	jpeg_full_size = jpeg_len + head_size + 2 + exifDataSize;

//	fprintf(stderr, "jpeg_len = 0x%x, head_size = 0x%x, exifDataSize = 0x%x, jpeg_full_size = 0x%x\n",
//			jpeg_len, head_size, exifDataSize, jpeg_full_size);

	if (bufferImageData) jpeg_this_size = jpeg_full_size;  // header+frame
	else jpeg_this_size = head_size + exifDataSize;        // only header
	jpeg_copy = malloc(jpeg_this_size);
	if (!jpeg_copy) {
		syslog(LOG_ERR, "%s:%d malloc (%d) failed", __FILE__, __LINE__, jpeg_this_size);
		// If we really want it, but don't get it - let's try more
		for (i = 0; i < 10; i++) {
			usleep(((jpeg_this_size & 0x3ff) + 5) * 100);  // up to 0.1 sec
			jpeg_copy = malloc(jpeg_this_size);
			if (jpeg_copy) break;
			syslog(LOG_ERR, "%s:%d malloc (%d) failed", __FILE__, __LINE__, jpeg_this_size);
		}
		if (!jpeg_copy) {
			syslog(LOG_ERR, "%s:%d malloc (%d) failed 10 times - giving up", __FILE__, __LINE__, jpeg_this_size);
			exit(1);
		}
	}

	lseek(fset->jphead_fd, 0, 0);
	read(fset->jphead_fd, &jpeg_copy[exifDataSize], head_size);
	close(fset->jphead_fd);
	fset->jphead_fd = -1;
	if (exifDataSize > 0) {                                // insert Exif
		memcpy(jpeg_copy, &jpeg_copy[exifDataSize], 2);    // copy first 2 bytes of the JFIF header before Exif
		lseek(fset->exif_dev_fd,frame_params.meta_index,SEEK_END);   // select meta page to use (matching frame)
		read(fset->exif_dev_fd, &jpeg_copy[2], exifDataSize);        // Insert Exif itself
		close(fset->exif_dev_fd);
		fset->exif_dev_fd = -1;
	}
	switch (color_mode) {
	//   case COLORMODE_MONO6:    //! monochrome, (4:2:0),
	//   case COLORMODE_COLOR:    //! color, 4:2:0, 18x18(old)
	case COLORMODE_JP46:    // jp4, original (4:2:0)
	case COLORMODE_JP46DC:  // jp4, dc -improved (4:2:0)
		mime_type = "jp46";
		extension = "jp46";
		break;
		//   case COLORMODE_COLOR20://  color, 4:2:0, 20x20, middle of the tile (not yet implemented)
	case COLORMODE_JP4:             // jp4, 4 blocks, (legacy)
	case COLORMODE_JP4DC:           // jp4, 4 blocks, dc -improved
	case COLORMODE_JP4DIFF:         // jp4, 4 blocks, differential red := (R-G1), blue:=(B-G1), green=G1, green2 (G2-G1). G1 is defined by Bayer shift, any pixel can be used
	case COLORMODE_JP4HDR:          // jp4, 4 blocks, differential HDR: red := (R-G1), blue:=(B-G1), green=G1, green2 (high gain)=G2) (G1 and G2 - diagonally opposite)
	case COLORMODE_JP4DIFF2:        // jp4, 4 blocks, differential, divide differences by 2: red := (R-G1)/2, blue:=(B-G1)/2, green=G1, green2 (G2-G1)/2
	case COLORMODE_JP4HDR2:         // jp4, 4 blocks, differential HDR: red := (R-G1)/2, blue:=(B-G1)/2, green=G1, green2 (high gain)=G2),
		mime_type = "jp4";
		extension = "jp4";
		break;
		//   case COLORMODE_MONO4:    //! monochrome, 4 blocks (but still with 2x2 macroblocks)
	default:
		mime_type = "jpeg";
		extension = "jpeg";

	}
	//   char * mime_type;
	//   char * extension;
	/*
   #define COLORMODE_MONO6     0 // monochrome, (4:2:0),
   #define COLORMODE_COLOR     1 // color, 4:2:0, 18x18(old)
   #define COLORMODE_JP46      2 // jp4, original (4:2:0)
   #define COLORMODE_JP46DC    3 // jp4, dc -improved (4:2:0)
   #define COLORMODE_COLOR20   4 //  color, 4:2:0, 20x20, middle of the tile (not yet implemented)
   #define COLORMODE_JP4       5 // jp4, 4 blocks, (legacy)
   #define COLORMODE_JP4DC     6 // jp4, 4 blocks, dc -improved
   #define COLORMODE_JP4DIFF   7 // jp4, 4 blocks, differential red := (R-G1), blue:=(B-G1), green=G1, green2 (G2-G1). G1 is defined by Bayer shift, any pixel can be used
   #define COLORMODE_JP4HDR    8 // jp4, 4 blocks, differential HDR: red := (R-G1), blue:=(B-G1), green=G1, green2 (high gain)=G2) (G1 and G2 - diagonally opposite)
   #define COLORMODE_JP4DIFF2  9 // jp4, 4 blocks, differential, divide differences by 2: red := (R-G1)/2, blue:=(B-G1)/2, green=G1, green2 (G2-G1)/2
   #define COLORMODE_JP4HDR2  10 // jp4, 4 blocks, differential HDR: red := (R-G1)/2, blue:=(B-G1)/2, green=G1, green2 (high gain)=G2),
   #define COLORMODE_MONO4    14 // monochrome, 4 blocks (but still with 2x2 macroblocks)
	 */
	printf("Content-Type: image/%s\r\n", mime_type);
    if (saveImage) {
        printf("Content-Disposition: attachment; filename=\"%s%ld_%06d_%d.%s\"\r\n",      // does not open, asks for filename to save
                fset->timestamp_name?"":"elphelimg_", frame_params.timestamp_sec,  frame_params.timestamp_usec, fset->sensor_port + fset->base_chn, extension);
    } else {
        printf("Content-Disposition: inline; filename=\"%s%ld_%06d_%d.%s\"\r\n",                    // opens in browser, asks to save on right-click
                fset->timestamp_name?"":"elphelimg_", frame_params.timestamp_sec,  frame_params.timestamp_usec, fset->sensor_port + fset->base_chn, extension);
    }

	if (bufferImageData) {                                                                                                                  /* Buffer the whole file before sending over the network to make sure it will not be corrupted if the circular buffer will be overrun) */
#if ELPHEL_DEBUG_THIS
		unsigned long start_time, end_time;
		start_time = lseek(fset->circbuf_fd, LSEEK_CIRC_UTIME, SEEK_END);
#endif
		l = head_size + exifDataSize;
		/* JPEG image data may be split in two segments (rolled over buffer end) - process both variants */
		if ((jpeg_start + jpeg_len) > buff_size) { // two segments
			memcpy(&jpeg_copy[l], (unsigned long* )&ccam_dma_buf[jpeg_start >> 2], buff_size - jpeg_start);
			l += buff_size - jpeg_start;
			memcpy(&jpeg_copy[l], (unsigned long* )&ccam_dma_buf[0], jpeg_len - (buff_size - jpeg_start));
		    D1(fprintf(stderr, "Two parts (buffered), jpeg_start (long) = 0x%x\n", jpeg_start));
		} else { /* single segment */
			memcpy(&jpeg_copy[l], (unsigned long* )&ccam_dma_buf[jpeg_start >> 2], jpeg_len);
            D1(fprintf(stderr, "One part (buffered), jpeg_start (long) = 0x%x, buff_size = 0x%x\n", jpeg_start, buff_size));
		}
#if ELPHEL_DEBUG_THIS
		end_time = lseek(fset->circbuf_fd, LSEEK_CIRC_UTIME, SEEK_END);
		D(fprintf(stderr, "memcpy time = %lu\n", end_time - start_time));
#endif
		memcpy(&jpeg_copy[jpeg_len + head_size + exifDataSize], trailer, 2);
		printf("Content-Length: %d\r\n", jpeg_full_size);
		printf("\r\n");
		sendBuffer(jpeg_copy, jpeg_full_size);
	} else { /* fast connection, no need to buffer image, so we'll try to run it faster */
		printf("Content-Length: %d\r\n", jpeg_full_size);
		printf("\r\n");
		sendBuffer(jpeg_copy, head_size + exifDataSize);        //JPEG+Exif
		/* JPEG image data may be split in two segments (rolled over buffer end) - process both variants */
		if ((jpeg_start + jpeg_len) > buff_size) {              // two segments
			/* copy from the beginning of the frame to the end of the buffer */
			sendBuffer((void*)&ccam_dma_buf[jpeg_start >> 2], buff_size - jpeg_start);
			/* copy from the beginning of the buffer to the end of the frame */
			sendBuffer((void*)&ccam_dma_buf[0], jpeg_len - (buff_size - jpeg_start));
            D1(fprintf(stderr, "Two parts (non-buffered), jpeg_start (long) = 0x%x\n", jpeg_start));
		} else { // single segment
			/* copy from the beginning of the frame to the end of the frame (no buffer rollovers) */
			sendBuffer((void*)&ccam_dma_buf[jpeg_start >> 2], jpeg_len);
            D1(fprintf(stderr, "One part (non-buffered), jpeg_start (long) = 0x%x, buff_size = 0x%x\n", jpeg_start, buff_size));
		}
		sendBuffer(trailer, 2);
	}
	free(jpeg_copy);
	return 0;
}

/**
 * @brief Send data from buffer to @e stdout. The writes are repeated until
 * all data is sent.
 * @param[in]   buffer   pointer to data buffer
 * @param[in]   len      the length of data buffer
 * @return      None
 */
void sendBuffer(void * buffer, int len)
{
	int bytesLeft = len;
	int offset = 0;
	int bytesWritten;
	char * cbuffer = (char*)buffer;

	D(fprintf(stderr, "buffer=%p, len=%d\n", buffer, len));
	while (bytesLeft > 0) {
		D(fprintf(stderr, " --bytesLeft=%d\n", bytesLeft));

		bytesWritten = fwrite(&cbuffer[offset], 1, bytesLeft, stdout);
		bytesLeft -=   bytesWritten;
		offset +=      bytesWritten;
	}
}

/**
 * @brief Main processing function. Parent process forks into this function,
 * listens to commands sent over socket and sends data back.
 * @param[in]   fset   file set for which the data should be processed
 * @return      None, this function should not return
 */
void listener_loop(struct file_set *fset)
{
	char errormsg[1024];
//	int fd_circ;
	int this_p;             // current frame pointer (bytes)
	int rslt;
	int buf_images = 0;
	int suggest_save_images = 0;
	char buf [1024];
	int len = 0;
	char * cp, *cp1, *cp2;
	int slow, skip;         // reduce frame rate by slow
	int base_chn;
	int sent2socket = -1;   // 1 - img, 2 - meta, 3 - pointers
	struct sockaddr_in sock;
	int res;
	int one = 1;
	int buff_size;
    int frame_number,compressed_frame_number;

	memset((char*)&sock, 0, sizeof(sock));
	sock.sin_port = htons(fset->port_num);
	sock.sin_family = AF_INET;
	res = socket(AF_INET, SOCK_STREAM, 0);
	setsockopt(res, SOL_SOCKET, SO_REUSEADDR, (char*)&one, sizeof(one));
	bind(res, (struct sockaddr* )&sock, sizeof(sock));
	listen(res, 10);
	int exif_enable = 1;
	while (1) {
		int fd;
		fd = accept(res, NULL, 0);
		if (fd == -1) continue;
		signal(SIGCHLD, SIG_IGN); // no zombies, please!

		/* do we need any fork at all if we now serve images to one client at a time? */
		if (fork() == 0) {
			close(res);
			/* setup stdin and stdout */
			fflush(stdout);
			fflush(stderr);
			dup2(fd, 0);
			dup2(fd, 1);
			close(fd);
			/* We need just the first line of the GET to read parameters */
			if (fgets(buf, sizeof(buf) - 1, stdin)) len = strlen(buf);
			cp = buf;
			strsep(&cp, "/?"); // ignore everything before first "/" or "?"
			if (cp) {
				// Now cp points to the first character after the first "/" or "?" in the url
				// we need to remove everything after (and including) the first space
				cp1 = strchr(cp, ' ');
				if (cp1) cp1[0] = '\0';
			}
			if (!cp || (strlen(cp) == 0)) { // no url commands - probably the server url was manually entered
				printf("HTTP/1.0 200 OK\r\n");
				printf("Server: Elphel Imgsrv\r\n");
				printf("Access-Control-Allow-Origin: *\r\n");
				printf("Access-Control-Expose-Headers: Content-Disposition\r\n");
				printf("Content-Length: %d\r\n", strlen(url_args));
				printf("Content-Type: text/plain\r\n");
				printf("\r\n");
				printf(url_args);
				fflush(stdout);
				_exit(0);
			}
			// Process 'frame' and 'wframe' commands - they do not need circbuf (now they do!)
			if ((strncmp(cp, "frame", 5) == 0) || (strncmp(cp, "wframe", 6) == 0) || (strncmp(cp, "sframe", 6) == 0)) {
				if (strncmp(cp, "wframe", 6) == 0) waitFrameSync(fset);
				printf("HTTP/1.0 200 OK\r\n");
				printf("Server: Elphel Imgsrv\r\n");
				printf("Access-Control-Allow-Origin: *\r\n");
				printf("Access-Control-Expose-Headers: Content-Disposition\r\n");
				printf("Content-Length: 11\r\n");
				printf("Content-Type: text/plain\r\n");
				printf("\r\n");
				if (strncmp(cp, "frame", 5) == 0)
				    printf("%010ld\r\n", getCurrentFrameNumberCompressor(fset));
				else //sframe
                    printf("%010ld\r\n", getCurrentFrameNumberSensor(fset));
				fflush(stdout);
				_exit(0);
			}
			// now process the commands one at a time, but first - open the circbuf file and setup the pointer
			if (fset->circbuf_fd<0)
			    fset->circbuf_fd = open(fset->cirbuf_fn, O_RDWR);
			if (fset->circbuf_fd < 0) { // check control OK
				fprintf(stderr, "Error opening %s\n", fset->cirbuf_fn);
				out1x1gif();
				fflush(stdout);
				_exit(0);
			}
//			fset->circbuf_fd = fd_circ;
			/* find total buffer length (it is in defines, actually in c313a.h */
			buff_size = lseek(fset->circbuf_fd, 0, SEEK_END);
//			fprintf(stderr, "%s: read circbuf size: %d\n", __func__, buff_size);
			/* now let's try mmap itself */
			ccam_dma_buf = (unsigned long*)mmap(0, buff_size, PROT_READ, MAP_SHARED, fset->circbuf_fd, 0);
			if ((int)ccam_dma_buf == -1) {
				fprintf(stderr, "Error in mmap\n");
				close(fset->circbuf_fd); // - caller opened, caller - to close
				fset->circbuf_fd = -1;
				out1x1gif();
				fflush(stdout);
				_exit(0);
			}
			this_p = lseek(fset->circbuf_fd, LSEEK_CIRC_LAST, SEEK_END);
			D(fprintf(stderr, "%s: current frame pointer this_p (in bytes): 0x%x\n", __func__, this_p));
			// continue with iterating through the commands
			while ((cp1 = strsep(&cp, "/?&"))) {
				// if the first character is a digit, it is a file pointer
				if (strchr("0123456789", cp1[0])) {
					this_p = lseek(fset->circbuf_fd, strtol(cp1, NULL, 10), SEEK_SET);
				} else if ((strcmp(cp1, "img") == 0) || (strcmp(cp1, "bimg") == 0) || (strcmp(cp1, "simg") == 0) || (strcmp(cp1, "sbimg") == 0)) {
//					fprintf(stderr, "%s: processing img command\n", __func__);
					if (sent2socket > 0) break;                             // image/xmldata was already sent to socket, ignore
					if (lseek(fset->circbuf_fd, LSEEK_CIRC_READY, SEEK_END) < 0) {   // here passes OK, some not ready error is later, in sendimage (make it return different numbers)
						rslt = out1x1gif();
					    compressed_frame_number = lseek(fset->circbuf_fd, LSEEK_CIRC_GETFRAME, SEEK_END );
                        if (fset->framepars_dev_fd <0)
                            fset->framepars_dev_fd = open(fset->framepars_dev_name, O_RDWR);
                        frame_number =            lseek(fset->framepars_dev_fd, 0, SEEK_CUR );
                        fprintf(stderr, "%s: no frame is available. Sensor frame = %d(0x%x), compressor frame = %d(0x%x)\n",
                                __func__, frame_number, frame_number, compressed_frame_number, compressed_frame_number);

					} else {
						printf("HTTP/1.0 200 OK\r\n");
						printf("Server: Elphel Imgsrv\r\n");
						printf("Access-Control-Allow-Origin: *\r\n");
						printf("Access-Control-Expose-Headers: Content-Disposition\r\n");
						printf("Expires: 0\r\n");
						printf("Pragma: no-cache\r\n");
						buf_images = ((strcmp(cp1, "img") == 0) || (strcmp(cp1, "simg") == 0)) ? 0 : 1;
						suggest_save_images = ((strcmp(cp1, "simg") == 0) || (strcmp(cp1, "sbimg") == 0)) ? 1 : 0;
//						fprintf(stderr, "%s: sending image\n", __func__);
						rslt = sendImage(fset, buf_images, exif_enable, suggest_save_images); // verify driver that file pointer did not move
					}
					sent2socket = 1;
					if (rslt < 0) {
						if (sent2socket == 1) out1x1gif();
						else {
							sprintf(errormsg, "sendImage error = %d (%s:line %d)", rslt, __FILE__, __LINE__);
							errorMsgXML(errormsg);
						}
					}
					fflush(stdout); // let's not keep client waiting - anyway we've sent it all even when more commands  maybe left

				// multipart - always last
				} else if ((strncmp(cp1, "mimg", 4) == 0) || (strncmp(cp1, "bmimg", 5) == 0) || (strncmp(cp1, "mbimg", 5) == 0)) {
					if (sent2socket > 0) break;                             // image/xmldata was already sent to socket, ignore
					if (lseek(fset->circbuf_fd, LSEEK_CIRC_READY, SEEK_END) < 0)     // here passes OK, some not ready error is later, in sendimage (make it return different numbers)
						rslt = out1x1gif();
					else {
						buf_images = (strncmp(cp1, "mimg", 4) == 0) ? 0 : 1;
						cp2 = cp1 + (buf_images ? 5 : 4);
						slow = strtol(cp2, NULL, 10);
						if (slow < 1) slow = 1;

						printf("HTTP/1.0 200 OK\r\n");
						printf("Server: Elphel Imgsrv\r\n");
						printf("Access-Control-Allow-Origin: *\r\n");
						printf("Access-Control-Expose-Headers: Content-Disposition\r\n");
						printf("Expires: 0\r\n");
						printf("Pragma: no-cache\r\n");
						printf("Content-Type: multipart/x-mixed-replace;boundary=ElphelMultipartJPEGBoundary\r\n");
						rslt = 0;
						while (rslt >= 0) {
							printf("\r\n--ElphelMultipartJPEGBoundary\r\n");
							rslt = sendImage(fset, buf_images, exif_enable, 0); // verify driver that file pointer did not move
							fflush(stdout);
							if (rslt >= 0) for (skip = 0; skip < slow; skip++) {
								this_p = lseek(fset->circbuf_fd, LSEEK_CIRC_NEXT, SEEK_END);
								// If these "next" is not ready yet - wait, else - use latest image
								if ((lseek(fset->circbuf_fd, LSEEK_CIRC_VALID, SEEK_END) >= 0) &&        // no sense to wait if the pointer is invalid
										(lseek(fset->circbuf_fd, LSEEK_CIRC_READY, SEEK_END) <  0) &&    // or the frame is already ready
										(lseek(fset->circbuf_fd, LSEEK_CIRC_VALID, SEEK_END) >= 0))      // test valid once again, after not ready - it might change
									this_p = lseek(fset->circbuf_fd, LSEEK_CIRC_WAIT, SEEK_END);
								else this_p = lseek(fset->circbuf_fd, LSEEK_CIRC_LAST, SEEK_END);
							}
						}
						_exit(0);
					}
				} else if (strcmp(cp1, "pointers") == 0) {
					if (sent2socket > 0) break;                             // image/xmldata was already sent to socket, ignore
					framePointersXML(fset);                                 // will restore file pointer after itself
					sent2socket = 3;
					fflush(stdout);                                         // let's not keep client waiting - anyway we've sent it all even when more commands  maybe left
				} else if (strcmp(cp1, "meta") == 0) {
					if ((sent2socket > 0) && (sent2socket != 2)) break;     // image/xmldata was already sent to socket, ignore
					metaXML(fset, (sent2socket > 0) ? 1 : 0);               // 0 - new (send headers), 1 - continue, 2 - finish
					sent2socket = 2;
					fflush(stdout);                                         // let's not keep client waiting - anyway we've sent it all even when more commands  maybe left
                } else if (strcmp(cp1, "timestamp_name") == 0) {
                    fset->timestamp_name = 1;
                } else if (strncmp(cp1, "bchn", 4) == 0) {
                	cp2 = cp1 + 4;
                	base_chn = strtol(cp2, NULL, 10);
                    fset->base_chn = base_chn;
				} else if (strcmp(cp1, "noexif") == 0) {
					exif_enable = 0;
				} else if (strcmp(cp1, "exif") == 0) {
					exif_enable = 1;
				} else if (strcmp(cp1, "torp") == 0) {
					this_p = lseek(fset->circbuf_fd, LSEEK_CIRC_TORP, SEEK_END);
				} else if (strcmp(cp1, "towp") == 0) {
					this_p = lseek(fset->circbuf_fd, LSEEK_CIRC_TOWP, SEEK_END);
				} else if (strcmp(cp1, "prev") == 0) {
					this_p = lseek(fset->circbuf_fd, LSEEK_CIRC_PREV, SEEK_END);
				} else if (strcmp(cp1, "next") == 0) {
					this_p = lseek(fset->circbuf_fd, LSEEK_CIRC_NEXT, SEEK_END);
				} else if (strcmp(cp1, "last") == 0) {
					this_p = lseek(fset->circbuf_fd, LSEEK_CIRC_LAST, SEEK_END);
				} else if (strcmp(cp1, "first") == 0) {
					this_p = lseek(fset->circbuf_fd, LSEEK_CIRC_FIRST, SEEK_END);
				} else if (strcmp(cp1, "second") == 0) {
					this_p = lseek(fset->circbuf_fd, LSEEK_CIRC_SCND, SEEK_END);
				} else if (strcmp(cp1, "save") == 0) {
					this_p = lseek(fset->circbuf_fd, LSEEK_CIRC_SETP, SEEK_END);
				} else if (strcmp(cp1, "wait") == 0) {
					if ((lseek(fset->circbuf_fd, LSEEK_CIRC_VALID, SEEK_END) >= 0) &&        // no sense to wait if the pointer is invalid
							(lseek(fset->circbuf_fd, LSEEK_CIRC_READY, SEEK_END) <  0) &&    // or the frame is already ready
							(lseek(fset->circbuf_fd, LSEEK_CIRC_VALID, SEEK_END) >= 0))      // test valid once again, after not ready - it might change
						this_p = lseek(fset->circbuf_fd, LSEEK_CIRC_WAIT, SEEK_END);
				} else if (strcmp(cp1, "trig") == 0) {
                    if (fset->framepars_dev_fd < 0)
                        fset->framepars_dev_fd = open(fset->framepars_dev_name, O_RDWR);
				    lseek(fset->framepars_dev_fd, LSEEK_DMA_INIT, SEEK_END ); // LSEEK_DMA_INIT is currently used as trigger restart in NC393
				    fprintf(stderr, "Retriggering camera : lseek 0x%x, SEEK_END\n", LSEEK_DMA_INIT);
				} else if (strcmp(cp1, "favicon.ico") == 0) {
					// ignore silently - for now, later make an icon?
				} else {
					if (cp1[0] != '_') fprintf(stderr, "Unrecognized URL command: \"%s\" - ignoring\n", cp1);  // allow "&_time=..." be silently ignored - needed for javascript image reload
				}
			} // while ((cp1=strsep(&cp, "/?&")))
			if (sent2socket <= 0) { // Nothing was sent to the client so far and the command line is over. Let's return 1x1 pixel gif
				out1x1gif();
			} else if (sent2socket == 2) {
				metaXML(fset, 2);   // 0 - new (send headers), 1 - continue, 2 - finish
			}

			fflush(stdout); // probably it is not needed anymore, just in case
			_exit(0);
		} // end of child process
		close(fd); // parent
	} // while (1)
}

/**
 * @brief Parse command line options
 * @param[in]       argc   the number of command line arguments; passed from main()
 * @param[in]       argv   array of pointers to command line arguments; passed from main()
 * @param[in,out]   fset   array of #file_set structures; this function will set port
 *  numbers in the structures
 * @param[in]       fset_sz the number of elements in @e fset
 * @return          0 if command line options were successfully processed and
 * -1 in case of an error
 */
int parse_cmd_line(int argc, const char *argv[], struct file_set *fset, int fset_sz)
{
	int port;
	int i;

	if ((argc < 3) || (strcasecmp(argv[1], "-p"))) {
		printf(app_args, argv[0]);
		printf(url_args);
		return -1;
	} else if (argc == 3) {
		port = strtol(argv[2], NULL, 10);
		if (!port) {
			printf("Invalid port number %d\n", port);
			return -1;
		}
		for (int i = 0; i < fset_sz; i++) {
			fset[i].port_num = port + i;
		}
	} else if (argc > 3) {
		i = 0;
		// start parsing from first port number which is second positional parameter in cmd line
		while ((i < fset_sz) && (i + 2 < argc)) {
			port = strtol(argv[i + 2], NULL, 10);
			if (!port) {
				printf("Invalid port number %d\n", port);
				return -1;
			}
			fset[i].port_num = port;
			++i;
		}
		if (i < fset_sz - 1) {
			printf("Wrong ports quantity\n");
			return -1;
		}
	}

	for (i = 0; i < fset_sz; i++) {
		printf("Set port number: %d\n", fset[i].port_num);
	}
	return 0;
}

/**
 * @brief Initialize a file set with predefined values
 * @param[in,out]   fset   file set which should be initialized
 * @param[in]       fset_sz the number of elements in @e fset
 * @return          None
 */
void init_file_set(struct file_set *fset, int fset_sz)
{
	for (int i = 0; i < fset_sz; i++) {
		fset[i].cirbuf_fn = circbuf_fnames[i];
		fset[i].circbuf_fd = -1;
		fset[i].jphead_fn = jhead_fnames[i];
		fset[i].jphead_fd = -1;
		fset[i].exif_dev_name = exif_dev_names[i];
		fset[i].exif_dev_fd = -1;
		fset[i].exifmeta_dev_name = exifmeta_dev_names[i];
		fset[i].exifmeta_dev_fd = -1;
        fset[i].framepars_dev_name = framepars_dev_names[i];
        fset[i].framepars_dev_fd = -1;
		fset[i].port_num = 0;
        fset[i].sensor_port = i;
        fset[i].timestamp_name = 0;
        fset[i].base_chn = 0;
	}
}

/**
 * @brief Set port numbers, fork a separate process for each sensor port and
 * start listening/answering HTTP requests
 */
int main(int argc, char *argv[])
{
	int res = 0;

	init_file_set(files, SENSOR_PORTS);
	res = parse_cmd_line(argc, (const char **)argv, files, SENSOR_PORTS);
	if (res < 0)
		return EXIT_FAILURE;

	// no zombies, please!
	if (signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
		perror(argv[0]);
		return EXIT_FAILURE;
	}

	// spawn a process for each port
	for (int i = 0; i < SENSOR_PORTS; i++) {
		if (fork() == 0) {
			listener_loop(&files[i]);
			_exit(0);
		}
	}

	return EXIT_SUCCESS;
}
