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
#include <byteswap.h>

#include <sys/reboot.h>


 // change to 0 when done
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
 * @var file_set::tiff_dev_name
 * The file name of Tiff buffer
 * @var file_set::tiff_dev_fd
 * The file descriptor of Tiff buffer
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
	const char     *tiff_dev_name;
	int            tiff_dev_fd;
	const char     *exifmeta_dev_name;
	int            exifmeta_dev_fd;
    const char     *framepars_dev_name;
    int            framepars_dev_fd;
    int            sensor_port;
    int            timestamp_name;
    int            base_chn;
    // data to convert TIFF to 8-bit uncompressed bmp for quick preview
//    int            tiff_mn;
//    int            tiff_mx;
//    int            tiff_palette;
//    int            tiff_telem; // o - none,>0 - skip first rows, <0 - skip last rows
//    int            tiff_convert;// !=0 - convert TIFF-16 to 8 bits BMP
    // TODO - add calculation of min/max through histograms (OK to apply in next runs)
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
static const char *tiff_dev_names[SENSOR_PORTS] = {
        DEV393_PATH(DEV393_TIFF0),
        DEV393_PATH(DEV393_TIFF1),
        DEV393_PATH(DEV393_TIFF2),
        DEV393_PATH(DEV393_TIFF3)};
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
		"meta -        send XML-formatted data frame metada\n"
		"frame -       return current compressor frame number as plain text\n"
		"sframe -      return current sensor frame number as plain text\n\n"
		"wframe -      wait for the next frame sync, return current sensor frame number as plain text\n\n"
		"xframe -      return current compressor and sensor frame numbers as XML\n"
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
		"bchn[n]- base channel = n, <channel> = n + port number (0-3) - for unique naming of multicamera systems.\n"
		"reboot - reboot system (useful when all php-cgi instances are stuck).\n"
		"\n"
        "tiff_*   commands to preview 16-bit TIFF files as 8-bit indexed BMP ones (always buffered), they should be inserted before /bimg)\n"
        "tiff_convert - enable conversion, replace TIFF with BMP\n"
        "tiff_palette=[value] - select palette: 0 - white hot, 1 - black hot, 2 - colored ('fire')\n"
        "tiff_telem=[value] - remove telemetry line(s): >0 top lines, <0 - bottom lines\n"
        "tiff_mn=[value] - minimal value (all below mapped to output index 0)\n"
        "tiff_mx=[value] - maximal value (all above mapped to output index 255)."
		"\n"
		"Generating statistics for TIFF-16 uncompressed images, providing minimal, maximal, mean and several percentiles as XML file.\n"
		"In this mode tiff_mn and tiff_mx are used for histogram generation (if uncertain may use 0 and 65535), tiff_telem - telemetry.\n"
		"tiff_bin=<power-of-2> specifies bin size as a power of 2: 0 - bin size == 1, 1 - bin size is 2, 2 -> 4, etc.\n"
		"tiff_stats is the command itself, it should be after the required parameters: tiff_mn=<...>/tiff_mx=<...>/tiff_telem=<...>/tiff_bin=<...>/tiff_stats.\n"
		"tiff_auto=[value] may preceed tiff_convert and calculate and set min/max for conversion using tiff_stat internally.\n"
		"          Value is a 2-digit decimal, low digit applies to cut from the lower (cold) values, high digit - to cut from the high (hot) ones:\n"
		"          Value 0 - use asolute min/max values, 1 - 0.1%% to 100.0%%, 10 - 0%% to 99.9%%, 22 - from 0.5%% to 99.5%%, 03 - from 1%% to 100%%,\n"
		"          Value 24 - from 5%% to 99.5%%, 55 - 10%% to 90%%.\n";

// path to file containing serial number
static const char path_to_serial[] = "/sys/devices/soc0/elphel393-init/serial";

int  sendImage(struct file_set *fset, int bufferImageData, int use_Exif, int saveImage,
		       int tiff_convert, int tiff_mn, int tiff_mx, int tiff_palette, int tiff_telem);
int getBmpHeader(uint8_t * buffer, int width, int height, int palette, int telem);
uint8_t * convertImage (uint8_t *dst, uint16_t *src, int *offs, int len, int mn, int mx,
		               int width, int height, int telem);
void sendBuffer(void * buffer, int len);
void listener_loop(struct file_set *fset);
void errorMsgXML(char * msg);
int  framePointersXML(struct file_set *fset);
int  frameNumbersXML(struct file_set *fset);
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
	if (fset->exif_dev_fd < 0)
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

	//TODO: Check .dst_exif > 0, maybe add same for Tiff fields


	// Image Description
	if (exif_dir[Exif_Image_ImageDescription_Index].ltag == Exif_Image_ImageDescription) { // Exif_Image_ImageDescription is present in template
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_Image_ImageDescription_Index].dst_exif,
				SEEK_SET);
		saferead255(fset->exif_dev_fd, val, exif_dir[Exif_Image_ImageDescription_Index].len);
		printf("<ImageDescription>\"%s\"</ImageDescription>\n", val);
	}
	// Exif_Image_ImageNumber_Index           0x13
	if (exif_dir[Exif_Image_ImageNumber_Index].ltag == Exif_Image_ImageNumber) { // Exif_Image_ImageNumber_Index is present in template
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_Image_ImageNumber_Index].dst_exif,
				SEEK_SET);
		read(fset->exif_dev_fd, rational3, 4);
		sprintf(val, "%ld", (long)__cpu_to_be32( rational3[0]));
		printf("<ImageNumber>\"%s\"</ImageNumber>\n", val);
	}

	// Exif_Image_Orientation_Index           0x14
	if (exif_dir[Exif_Image_Orientation_Index].ltag == Exif_Image_Orientation) { // Exif_Image_Orientation_Index is present in template
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_Image_Orientation_Index].dst_exif,
				SEEK_SET);
		rational3[0] = 0;
		read(fset->exif_dev_fd, rational3, 2);
		sprintf(val, "%ld", (long)( rational3[0] >> 8));
		printf("<Orientation>\"%s\"</Orientation>\n", val);
	}

	// Exif_Image_PageNumber
	if (exif_dir[Exif_Image_PageNumber_Index].ltag == Exif_Image_PageNumber) { // Exif_Image_Orientation_Index is present in template
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_Image_PageNumber_Index].dst_exif,
				SEEK_SET);
		rational3[0] = 0;
		read(fset->exif_dev_fd, rational3, 2);
		sprintf(val, "%u", __cpu_to_be16(rational3[0]));
		printf("<SensorNumber>\"%s\"</SensorNumber>\n", val);
	}

	// DateTimeOriginal (with subseconds)
	if (exif_dir[Exif_Photo_DateTimeOriginal_Index].ltag == Exif_Photo_DateTimeOriginal) {
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_Photo_DateTimeOriginal_Index].dst_exif,
				SEEK_SET);
		read(fset->exif_dev_fd, val, 19);
		val[19] = '\0';
		if (exif_dir[Exif_Photo_SubSecTimeOriginal_Index].ltag == Exif_Photo_SubSecTimeOriginal) {
			val[19] = '.';
			lseek(fset->exif_dev_fd,
					exif_page_start + exif_dir[Exif_Photo_SubSecTimeOriginal_Index].dst_exif,
					SEEK_SET);
			read(fset->exif_dev_fd, &val[20], 7);
			val[27] = '\0';
		}
		printf("<DateTimeOriginal>\"%s\"</DateTimeOriginal>\n", val);
	}
	// Exif_Photo_ExposureTime
	if (exif_dir[Exif_Photo_ExposureTime_Index].ltag == Exif_Photo_ExposureTime) { // Exif_Photo_ExposureTime is present in template
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_Photo_ExposureTime_Index].dst_exif,
				SEEK_SET);
		read(fset->exif_dev_fd, rational3, 8);
		exposure = (1.0 * __cpu_to_be32( rational3[0])) / __cpu_to_be32( rational3[1]);
		sprintf(val, "%f", exposure);
		printf("<ExposureTime>\"%s\"</ExposureTime>\n", val);
	}

	// Exif_Photo_MakerNote
	if (exif_dir[Exif_Photo_MakerNote_Index].ltag == Exif_Photo_MakerNote) { // Exif_Photo_MakerNote is present in template
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_Photo_MakerNote_Index].dst_exif,
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
				exif_page_start + exif_dir[Exif_GPSInfo_GPSMeasureMode_Index].dst_exif,
				SEEK_SET);
		read(fset->exif_dev_fd, val, 1);
		val[1] = '\0';
		printf("<GPSMeasureMode>\"%s\"</GPSMeasureMode>\n", val);
	}

	// GPS date/time
	if (exif_dir[Exif_GPSInfo_GPSDateStamp_Index].ltag == Exif_GPSInfo_GPSDateStamp) {
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_GPSInfo_GPSDateStamp_Index].dst_exif,
				SEEK_SET);
		read(fset->exif_dev_fd, val, 10);
		val[10] = '\0';
		if (exif_dir[Exif_GPSInfo_GPSTimeStamp_Index].ltag == Exif_GPSInfo_GPSTimeStamp) {
			lseek(fset->exif_dev_fd,
					exif_page_start + exif_dir[Exif_GPSInfo_GPSTimeStamp_Index].dst_exif,
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
                Exif_GPSInfo_GPSDateStamp, Exif_GPSInfo_GPSDateStamp_Index, exif_dir[Exif_GPSInfo_GPSTimeStamp_Index].dst_exif,
                val[0],val[1],val[2],val[3],val[4],val[5],val[6],val[7],val[8],val[9],val[10],val[11],val[12]);
*/
	}

	// knowing format provided from GPS - degrees and minutes only, no seconds:
	// GPS Longitude
	if (exif_dir[Exif_GPSInfo_GPSLongitude_Index].ltag == Exif_GPSInfo_GPSLongitude) { // Exif_GPSInfo_GPSLongitude is present in template
		lseek(fset->exif_dev_fd,
				exif_page_start + exif_dir[Exif_GPSInfo_GPSLongitude_Index].dst_exif,
				SEEK_SET);
		read(fset->exif_dev_fd, rational3, 24);
		longitude = __cpu_to_be32( rational3[0]) / (1.0 * __cpu_to_be32( rational3[1])) + __cpu_to_be32( rational3[2]) / (60.0 * __cpu_to_be32( rational3[3]));
		if (exif_dir[Exif_GPSInfo_GPSLongitudeRef_Index].ltag == Exif_GPSInfo_GPSLongitudeRef) {
			lseek(fset->exif_dev_fd,
					exif_page_start + exif_dir[Exif_GPSInfo_GPSLongitudeRef_Index].dst_exif,
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
				exif_page_start + exif_dir[Exif_GPSInfo_GPSLatitude_Index].dst_exif,
				SEEK_SET);
		read(fset->exif_dev_fd, rational3, 24);
		latitude = __cpu_to_be32( rational3[0]) / (1.0 * __cpu_to_be32( rational3[1])) + __cpu_to_be32( rational3[2]) / (60.0 * __cpu_to_be32( rational3[3]));
		if (exif_dir[Exif_GPSInfo_GPSLatitudeRef_Index].ltag == Exif_GPSInfo_GPSLatitudeRef) {
			lseek(fset->exif_dev_fd,
					exif_page_start + exif_dir[Exif_GPSInfo_GPSLatitudeRef_Index].dst_exif,
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
				exif_page_start + exif_dir[Exif_GPSInfo_GPSAltitude_Index].dst_exif,
				SEEK_SET);
		read(fset->exif_dev_fd, rational3, 8);
		altitude = (1.0 * __cpu_to_be32( rational3[0])) / __cpu_to_be32( rational3[1]);

		if (exif_dir[Exif_GPSInfo_GPSAltitudeRef_Index].ltag == Exif_GPSInfo_GPSAltitudeRef) {
			lseek(fset->exif_dev_fd,
					exif_page_start + exif_dir[Exif_GPSInfo_GPSAltitudeRef_Index].dst_exif,
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
				exif_page_start + exif_dir[Exif_GPSInfo_CompassDirection_Index].dst_exif,
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
				exif_page_start + exif_dir[Exif_GPSInfo_CompassRoll_Index].dst_exif,
				SEEK_SET);
		read(fset->exif_dev_fd, rational3, 8);
		roll = (1.0 * __cpu_to_be32( rational3[0])) / __cpu_to_be32( rational3[1]);

		if (exif_dir[Exif_GPSInfo_CompassRollRef_Index].ltag == Exif_GPSInfo_CompassRollRef) {
			lseek(fset->exif_dev_fd,
					exif_page_start + exif_dir[Exif_GPSInfo_CompassRollRef_Index].dst_exif,
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
				exif_page_start + exif_dir[Exif_GPSInfo_CompassPitch_Index].dst_exif,
				SEEK_SET);
		read(fset->exif_dev_fd, rational3, 8);
		pitch = (1.0 * __cpu_to_be32( rational3[0])) / __cpu_to_be32( rational3[1]);

		if (exif_dir[Exif_GPSInfo_CompassPitchRef_Index].ltag == Exif_GPSInfo_CompassPitchRef) {
			lseek(fset->exif_dev_fd,
					exif_page_start + exif_dir[Exif_GPSInfo_CompassPitchRef_Index].dst_exif,
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
	// very rarely exceeds 512 - now it does !!!
// when imgsrv is launched from the command line (imgsrv -p 2323) it outputs:
//	*** buffer overflow detected ***: imgsrv terminated
//	char s[528]; // 512 // 341;
	char s[600]; // 512 // 341;
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

int TiffStats(struct file_set * fset,
		      int    tiff_auto,   // -1 - return XML, >=0 set tiff_min and tiff_max (if non-NULL)
			  int *  tiff_min,
			  int *  tiff_max,
			  int    bin_shift,   // binsize = 1 << bin_shift
			  int    hist_size,   // size of the histogram = ( 1 << size_shift)
			  int    hist_min,   // subtract from value before binning
			  int    tiff_telem)
{
	int frameParamPointer = 0;
	struct interframe_params_t frame_params;
	int buff_size;
	int jpeg_len;   // bytes
	int jpeg_start; // bytes
	int l, i, j;
	int color_mode;
	int timestamp_start;
	int width;
	int height;
	int bin_size =  1 << bin_shift;
	int hist16_size = sizeof(int) * hist_size;
	int * hist16 = 0; //	memset((char*)&sock, 0, sizeof(sock));
	int tiff_offs;
	int tiff_auto_high = -1;
	int tiff_auto_low =  -1;

	char *out_str; // 1024
	char *out_strp;
	char perc_name[64];
	int out_size = 4096; // 15 percentiles - 827, should be sufficient to fit XML

//	const float percentiles[] = {0.1,1.0,10,50.0,90.0,99.0,99.9};
//	const int num_percentiles = sizeof(percentiles)/sizeof(float);
	const char * spercentiles[] = {"0.1","0.5","1","5","10","20","30","40","50","60","70","80","90","95","99","99.5","99.9"};
	const int num_percentiles = sizeof(spercentiles)/sizeof(char *);
	float percentile_values[num_percentiles];
	float mean;
	int min_val = 0xffff;
	int max_val = 0;
	long long sum_val = 0;
	int num_pix;
	int ibin;
	int iperc;
	int prev_bin, this_bin;
	float perc_val;
#if ELPHEL_DEBUG_THIS
	float perc_val0, perc_val1, perc_diff; // just debug
#endif
	if (tiff_auto >= 0) { // first digit high side, second - low side
		tiff_auto_high = tiff_auto/10;
		tiff_auto_low =  tiff_auto - 10* tiff_auto_high;

		while ((tiff_auto_high + tiff_auto_low) > num_percentiles){
			if (tiff_auto_high > 0) tiff_auto_high--;
			if (tiff_auto_low > 0) tiff_auto_low--;
		}
	}
	D(fprintf(stderr, "TiffStats(): tiff_auto=%d, tiff_auto_high=%d, tiff_auto_low=%d, tiff_min = %p, tiff_max = %p\n",
			tiff_auto,tiff_auto_high,tiff_auto_low,tiff_min,tiff_max));

	jpeg_start = lseek(fset->circbuf_fd, 0, SEEK_CUR);     //get the current read pointer

	/* find total buffer length (it is in defines, actually in c313a.h */
	buff_size = lseek(fset->circbuf_fd, 0, SEEK_END); // it is supposed to be open?
	/* restore file poinetr after lseek-ing the end */
	lseek(fset->circbuf_fd, jpeg_start, SEEK_SET);
	D(fprintf(stderr, "position (longs) = 0x%x\n", (int)lseek(fset->circbuf_fd, 0, SEEK_CUR)));
	/* now let's try mmap itself */
	frameParamPointer = jpeg_start - sizeof(struct interframe_params_t) + 4;
	if (frameParamPointer < 0)
		frameParamPointer += buff_size;
	memcpy(&frame_params, (unsigned long*)&ccam_dma_buf[frameParamPointer >> 2], sizeof(struct interframe_params_t) - 4);
	jpeg_len = frame_params.frame_length;
	color_mode = frame_params.color;
	width = frame_params.width;
	height = frame_params.height;
	D(fprintf(stderr, "color_mode = 0x%x\n", color_mode));
	if (color_mode == COLORMODE_RAW) { // should check bits too?
		//		return -1; // provide meaningful errno?
		//	}
		if (frame_params.signffff != 0xffff) {
			fprintf(stderr, "wrong signature signff = 0x%x \n", (int)frame_params.signffff);
			for (i=0; i< sizeof(struct interframe_params_t)/4;i++){
				fprintf(stderr, "%08lx ",ccam_dma_buf[(frameParamPointer >> 2) + i]);
				if (!((i+1) & 0x7)){
					fprintf(stderr, "\n ");
				}
			}
			close(fset->jphead_fd);
			fset->jphead_fd = -1;
			return -4;
		}
		// Copy timestamp (goes after the image data)
		timestamp_start=jpeg_start+((jpeg_len+CCAM_MMAP_META+3) & (~0x1f)) + 32 - CCAM_MMAP_META_SEC; // magic shift - should index first byte of the time stamp
		if (timestamp_start >= buff_size) timestamp_start-=buff_size;
		memcpy (&(frame_params.timestamp_sec), (unsigned long * ) &ccam_dma_buf[timestamp_start>>2],8);
		D(fprintf(stderr, "TiffStats(): color_mode=0x%x, tiff_telem=%d, hist_size=%d, hist16_size=%d\n",color_mode, tiff_telem, hist_size, hist16_size));

		// Allocate histogram array
		if ((tiff_auto_high != 0) || (tiff_auto_low != 0)) { // tiff_auto==0 - only calculate min, max (and sum/average - not used)
			hist16 = malloc(hist16_size);
			if (!hist16) {
				D(fprintf(stderr, "Malloc error allocating hist16 %d (0x%x) bytes\n",hist16_size, hist16_size));
				syslog(LOG_ERR, "%s:%d malloc (%d) failed", __FILE__, __LINE__, hist16_size);
				exit(1);
			}
			memset((char*)hist16, 0, hist16_size);
		}
		D(fprintf(stderr, "TiffStats(), sum_val=%lld\n",sum_val));

		tiff_offs = 0;
		num_pix = 0;
		/* JPEG image data may be split in two segments (rolled over buffer end) - process both variants */
		if ((jpeg_start + jpeg_len) > buff_size) { // two segments
			D(fprintf(stderr, "Two-part image data, sum_val=%lld\n",sum_val));
			// first part
			num_pix += cumulHistImage (
					(uint16_t *) &ccam_dma_buf[jpeg_start >> 2], // uint16_t * src,
					&tiff_offs,                                  // int *      offs,
					(buff_size - jpeg_start)/2,                  // int        len,
					width,                                       // int        width,
					height,                                      // int        height,
					tiff_telem,                                  // int        telem);
					bin_shift,                                   // int        bin_shift,
					hist_size,                                   // int        hist_size,
					hist_min,                                    // int        hist_min,
					hist16,                                      // int *      hist16,
					&sum_val,                                    // long long* sum_val,
					&min_val,                                    // int *      mn,
					&max_val);                                   // int *      mx

			//		int min_val = 0xffff;
			//		int max_val = 0;

			// second part
			l += buff_size - jpeg_start;
			num_pix += cumulHistImage (
					(uint16_t *) &ccam_dma_buf[0],               // uint16_t * src, //  // , jpeg_len - (buff_size - jpeg_start)),
					&tiff_offs,                                  // int *      offs,
					(jpeg_len - (buff_size - jpeg_start))/2,     // int        len,
					width,                                       // int        width,
					height,                                      // int        height,
					tiff_telem,                                  // int        telem);
					bin_shift,                                   // int        bin_shift,
					hist_size,                                   // int        hist_size,
					hist_min,                                    // int        hist_min,
					hist16,                                      // int *      hist16
					&sum_val,                                    // long long* sum_val,
					&min_val,                                    // int *      mn,
					&max_val);                                   // int *      mx
			D(fprintf(stderr, "Two parts (buffered), jpeg_start (long) = 0x%x\n", jpeg_start));
		} else { /* single segment */
			D(fprintf(stderr, "One-part image data, l=0x%x, sum_val=%lld\n",l,sum_val));
			num_pix += cumulHistImage (
					(uint16_t *) &ccam_dma_buf[jpeg_start >> 2], // uint16_t * src,
					&tiff_offs,                                  // int *      offs,
					(jpeg_len)/2,                                // int        len,
					width,                                       // int        width,
					height,                                      // int        height,
					tiff_telem,                                  // int        telem);
					bin_shift,                                   // int        bin_shift,
					hist_size,                                   // int        hist_size,
					hist_min,                                    // int        hist_min,
					hist16,                                      // int *      hist16
					&sum_val,                                    // long long* sum_val,
					&min_val,                                    // int *      mn,
					&max_val);                                   // int *      mx
			D(fprintf(stderr, "One part (buffered), jpeg_start (long) = 0x%x, buff_size = 0x%x\n", jpeg_start, buff_size));
		}
		D(fprintf(stderr, "TiffStats() - after cumulHistImage()\n")); // got here
		// convert histogram to cumulative histogram
		D(fprintf(stderr, "TiffStats(): num_pix = %d, min_val=%d, max_val=%d\n",num_pix, min_val, max_val));

		if (tiff_auto_low == 0){ // absolute min
			D(fprintf(stderr, "TiffStats(): tiff_auto_low=%d, *tiff_min=%d\n",tiff_auto_low, *tiff_min));
			*tiff_min = min_val;
		}
		if (tiff_auto_high == 0){ // absolute max
			D(fprintf(stderr, "TiffStats(): tiff_auto_high=%d, *tiff_max=%d\n",tiff_auto_low, *tiff_max));
			*tiff_max = max_val;
		}

		if ((tiff_auto_low == 0) && (tiff_auto_high == 0)){ // absolute min/max
			D(fprintf(stderr, "TiffStats(): tiff_auto=%d, *tiff_min=%d, *tiff_max=%d\n",tiff_auto, *tiff_min, *tiff_max));
			return 0; // no need to process more
		}

//		num_pix = hist16[hist_size - 1];
		mean = 1.0* sum_val / num_pix;
		D(fprintf(stderr, "TiffStats(): num_pix = %d, mean = %f, min_val=%d, max_val=%d\n",num_pix, mean, min_val, max_val));

		// Allocate output string
		out_str = malloc(out_size);
		if (!out_str) {
			D(fprintf(stderr, "Malloc error allocating hist16 %d (0x%x) bytes\n",out_size, out_size));
			syslog(LOG_ERR, "%s:%d malloc (%d) failed", __FILE__, __LINE__, out_size);
			free(hist16);
			exit(1);
		}
		D(fprintf(stderr, "TiffStats(): malloc %d bytes OK\n",out_size));
		ibin=0;
		D(fprintf(stderr, "TiffStats(): hist_min=%d, bin_size=%d, hist_size=%d\n",hist_min, bin_size, hist_size));

		// make histogram cumulative
		for (i = 1; i < hist_size; i++){
			hist16[i] += hist16[i-1];
		}

		for (i = 0; i < num_percentiles; i++){
			iperc = num_pix * (0.01*strtod(spercentiles[i],NULL)); // should be ordered and <100%
			D(fprintf(stderr, "TiffStats(): i=%d, iperc=%d\n",i, iperc));
			if (iperc > num_pix) iperc = num_pix;
			// find first bin with cumulative pixels >= iperc;
			for (; hist16[ibin] < iperc; ibin++); // should always be (ibin < hist_size)
			prev_bin = 0;
			this_bin = hist16[ibin];
			if (ibin > 0) prev_bin = hist16[ibin-1];

			// linear interpolate float
			perc_val =  ((iperc - prev_bin)* (1.0 / (this_bin - prev_bin)) + ibin - 0.5) * bin_size + hist_min ;
#if ELPHEL_DEBUG_THIS
			perc_val0 = (ibin - 1 + 0.5) * bin_size + hist_min ;
			perc_val1 = (ibin     + 0.5) * bin_size + hist_min ;
			perc_diff = ((iperc - prev_bin)* (1.0 / (this_bin - prev_bin))) * bin_size;
			D(fprintf(stderr, "TiffStats(): perc_val0=%f, perc_val=%f, perc_val1=%f, perc_diff=%f\n",perc_val0,perc_val,perc_val1,perc_diff));
#endif
			D(fprintf(stderr, "TiffStats(): i=%d, iperc=%d, ibin=%d, bin_size=%d, perc_val=%f\n",i, iperc,ibin,bin_size,perc_val));
			percentile_values[i] = perc_val;
			D(fprintf(stderr, "TiffStats(): i=%d, prev_bin=%d, this_bin=%d, perc_val=%f\n",i, prev_bin,this_bin,perc_val));
			/*
			if (tiff_auto > 0) {
				if (i == (tiff_auto-1)){
					*tiff_min = (int) perc_val;
				} else if (i == (num_percentiles - tiff_auto)){
					*tiff_max = (int) perc_val;
				}
			}
			*/
			if ((tiff_auto_low > 0) && (i == (tiff_auto_low-1))) {
				*tiff_min = (int) perc_val;
			} else if ((tiff_auto_high > 0) && (i == (num_percentiles - tiff_auto_high))) {
				*tiff_max = (int) perc_val;
			}
		}
		if (tiff_auto >= 0) {
			D(fprintf(stderr, "TiffStats(): tiff_auto=%d,  tiff_auto_low=%d, tiff_auto_high=%d, *tiff_min=%d, *tiff_max=%d\n",
					tiff_auto, tiff_auto_low, tiff_auto_high, *tiff_min, *tiff_max));
			return 0;
		}
		out_strp =  out_str + sprintf(out_str,
				"<?xml version=\"1.0\"?>\n" \
				"<tiff_stats>\n" \
				"  <min>%d</min>\n" \
				"  <max>%d</max>\n" \
				"  <mean>%f</mean>\n" \
				"  <points>%d</points>\n",
				min_val,
				max_val,
				mean,
				num_pix);
		D(fprintf(stderr, "TiffStats(): out_str=\n%s\n, num_percentiles=%d\n",out_str,num_percentiles));
		for (i = 0; i < num_percentiles; i++){
			out_strp += sprintf(out_strp,"  <percentile_%s>%f</percentile_%s>\n", spercentiles[i], percentile_values[i], spercentiles[i]);
		}
		D(fprintf(stderr, "TiffStats(): strlen(out_str)=%d\n",strlen(out_str)));

		out_strp += sprintf(out_strp,"</tiff_stats>\n");
	} else { // incompatible color mode
		D(fprintf(stderr, "Reporting incompatible color_mode \n"));
		out_str =  "<?xml version=\"1.0\"?>\n" \
				"<tiff_stats>\n" \
				"  <error>This functionality is defined only for 16-bit uncompressed data</error>\n"
				"</tiff_stats>\n";
	}
	D(fprintf(stderr, "TiffStats(): sending response %d bytes long\n",strlen(out_str)));

	printf("HTTP/1.0 200 OK\r\n");
	printf("Server: Elphel Imgsrv\r\n");
	printf("Access-Control-Allow-Origin: *\r\n");
	printf("Access-Control-Expose-Headers: Content-Disposition\r\n");
	printf("Content-Length: %d\r\n", strlen(out_str));
	printf("Content-Type: text/xml\r\n");
	printf("Pragma: no-cache\r\n");
	printf("\r\n");
	printf("%s",out_str);
	D(fprintf(stderr, "TiffStats(): sending response %d bytes long\n",strlen(out_str)));
	if (color_mode == COLORMODE_RAW) { // only then allocated
		D(fprintf(stderr, "TiffStats(): hist16 = %p\n",hist16));
		if (hist16) free(hist16); // not used if tiff_auto == 0
		free(out_str);
	}
	D(fprintf(stderr, "TiffStats(): All Done, return 0\n"));
	return 0;
}

int frameNumbersXML(struct file_set *fset)
{
	// very rarely exceeds 512
	char s[528]; // 512 // 341;// VERIFY size!)
	int p; //, wp, rp;
	int nf = 0;
	int nfl = 0;
	int frame_number, compressed_frame_number;
	int frame16, sensor_state, compressor_state;
	char *cp_sensor_state, *cp_compressor_state;
	//Need to mmap just one port
    struct framepars_all_t   *frameParsAll;
    struct framepars_t       *framePars;
    unsigned long            *globalPars;

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

//->port_num
	// Read current sensor state - defined in c313a.h
	frame_number =            lseek(fset->framepars_dev_fd, 0, SEEK_CUR );
	compressed_frame_number = lseek(fset->circbuf_fd, LSEEK_CIRC_GETFRAME, SEEK_END );


	// Read current sensor state - defined in c313a.h
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



	sprintf(s, "<?xml version=\"1.0\"?>\n" \
			"<frames>\n" \
			"  <frame>%d</frame>\n"	\
            "  <frameHex>0x%x</frameHex>\n" \
            "  <compressedFrame>%d</compressedFrame>\n" \
            "  <compressedFrameHex>0x%x</compressedFrameHex>\n" \
			"  <sensor_state>\"%s\"</sensor_state>\n" \
			"  <compressor_state>\"%s\"</compressor_state>\n" \
			"</frames>\n",
			frame_number,
            frame_number,
			compressed_frame_number,
            compressed_frame_number,
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
 * Generate bmp header
 * @param buffer  pointer to the output budder, or null. If null - will only calculate the buffer size in bytes
 * @param width   image width  (should be multiple of 4 )!
 * @param height  image height (should be multiple of 4 )!
 * @param palette 0 - white hot, 1 - black hot, 2 - color
 * @param telem   >0 top telemetry lines, <0 - bottom telemetry lines
 * @return header length
 */
int getBmpHeader(
		uint8_t * buffer,
		int       width,
		int       height,
		int       palette,
		int       telem
		)
{
	int bitmap_size = 0;
	int filesize;
	const unsigned int palette012[][256]={ // msb is 0: 0x00rrggbb
		   {0x000000,0x010101,0x020202,0x030303,0x040404,0x050505,0x060606,0x070707,
			0x080808,0x090909,0x0a0a0a,0x0b0b0b,0x0c0c0c,0x0d0d0d,0x0e0e0e,0x0f0f0f,
			0x101010,0x111111,0x121212,0x131313,0x141414,0x151515,0x161616,0x171717,
			0x181818,0x191919,0x1a1a1a,0x1b1b1b,0x1c1c1c,0x1d1d1d,0x1e1e1e,0x1f1f1f,
			0x202020,0x212121,0x222222,0x232323,0x242424,0x252525,0x262626,0x272727,
			0x282828,0x292929,0x2a2a2a,0x2b2b2b,0x2c2c2c,0x2d2d2d,0x2e2e2e,0x2f2f2f,
			0x303030,0x313131,0x323232,0x333333,0x343434,0x353535,0x363636,0x373737,
			0x383838,0x393939,0x3a3a3a,0x3b3b3b,0x3c3c3c,0x3d3d3d,0x3e3e3e,0x3f3f3f,
			0x404040,0x414141,0x424242,0x434343,0x444444,0x454545,0x464646,0x474747,
			0x484848,0x494949,0x4a4a4a,0x4b4b4b,0x4c4c4c,0x4d4d4d,0x4e4e4e,0x4f4f4f,
			0x505050,0x515151,0x525252,0x535353,0x545454,0x555555,0x565656,0x575757,
			0x585858,0x595959,0x5a5a5a,0x5b5b5b,0x5c5c5c,0x5d5d5d,0x5e5e5e,0x5f5f5f,
			0x606060,0x616161,0x626262,0x636363,0x646464,0x656565,0x666666,0x676767,
			0x686868,0x696969,0x6a6a6a,0x6b6b6b,0x6c6c6c,0x6d6d6d,0x6e6e6e,0x6f6f6f,
			0x707070,0x717171,0x727272,0x737373,0x747474,0x757575,0x767676,0x777777,
			0x787878,0x797979,0x7a7a7a,0x7b7b7b,0x7c7c7c,0x7d7d7d,0x7e7e7e,0x7f7f7f,
			0x808080,0x818181,0x828282,0x838383,0x848484,0x858585,0x868686,0x878787,
			0x888888,0x898989,0x8a8a8a,0x8b8b8b,0x8c8c8c,0x8d8d8d,0x8e8e8e,0x8f8f8f,
			0x909090,0x919191,0x929292,0x939393,0x949494,0x959595,0x969696,0x979797,
			0x989898,0x999999,0x9a9a9a,0x9b9b9b,0x9c9c9c,0x9d9d9d,0x9e9e9e,0x9f9f9f,
			0xa0a0a0,0xa1a1a1,0xa2a2a2,0xa3a3a3,0xa4a4a4,0xa5a5a5,0xa6a6a6,0xa7a7a7,
			0xa8a8a8,0xa9a9a9,0xaaaaaa,0xababab,0xacacac,0xadadad,0xaeaeae,0xafafaf,
			0xb0b0b0,0xb1b1b1,0xb2b2b2,0xb3b3b3,0xb4b4b4,0xb5b5b5,0xb6b6b6,0xb7b7b7,
			0xb8b8b8,0xb9b9b9,0xbababa,0xbbbbbb,0xbcbcbc,0xbdbdbd,0xbebebe,0xbfbfbf,
			0xc0c0c0,0xc1c1c1,0xc2c2c2,0xc3c3c3,0xc4c4c4,0xc5c5c5,0xc6c6c6,0xc7c7c7,
			0xc8c8c8,0xc9c9c9,0xcacaca,0xcbcbcb,0xcccccc,0xcdcdcd,0xcecece,0xcfcfcf,
			0xd0d0d0,0xd1d1d1,0xd2d2d2,0xd3d3d3,0xd4d4d4,0xd5d5d5,0xd6d6d6,0xd7d7d7,
			0xd8d8d8,0xd9d9d9,0xdadada,0xdbdbdb,0xdcdcdc,0xdddddd,0xdedede,0xdfdfdf,
			0xe0e0e0,0xe1e1e1,0xe2e2e2,0xe3e3e3,0xe4e4e4,0xe5e5e5,0xe6e6e6,0xe7e7e7,
			0xe8e8e8,0xe9e9e9,0xeaeaea,0xebebeb,0xececec,0xededed,0xeeeeee,0xefefef,
			0xf0f0f0,0xf1f1f1,0xf2f2f2,0xf3f3f3,0xf4f4f4,0xf5f5f5,0xf6f6f6,0xf7f7f7,
			0xf8f8f8,0xf9f9f9,0xfafafa,0xfbfbfb,0xfcfcfc,0xfdfdfd,0xfefefe,0xffffff},
	       {0xffffff,0xfefefe,0xfdfdfd,0xfcfcfc,0xfbfbfb,0xfafafa,0xf9f9f9,0xf8f8f8,
			0xf7f7f7,0xf6f6f6,0xf5f5f5,0xf4f4f4,0xf3f3f3,0xf2f2f2,0xf1f1f1,0xf0f0f0,
			0xefefef,0xeeeeee,0xededed,0xececec,0xebebeb,0xeaeaea,0xe9e9e9,0xe8e8e8,
			0xe7e7e7,0xe6e6e6,0xe5e5e5,0xe4e4e4,0xe3e3e3,0xe2e2e2,0xe1e1e1,0xe0e0e0,
			0xdfdfdf,0xdedede,0xdddddd,0xdcdcdc,0xdbdbdb,0xdadada,0xd9d9d9,0xd8d8d8,
			0xd7d7d7,0xd6d6d6,0xd5d5d5,0xd4d4d4,0xd3d3d3,0xd2d2d2,0xd1d1d1,0xd0d0d0,
			0xcfcfcf,0xcecece,0xcdcdcd,0xcccccc,0xcbcbcb,0xcacaca,0xc9c9c9,0xc8c8c8,
			0xc7c7c7,0xc6c6c6,0xc5c5c5,0xc4c4c4,0xc3c3c3,0xc2c2c2,0xc1c1c1,0xc0c0c0,
			0xbfbfbf,0xbebebe,0xbdbdbd,0xbcbcbc,0xbbbbbb,0xbababa,0xb9b9b9,0xb8b8b8,
			0xb7b7b7,0xb6b6b6,0xb5b5b5,0xb4b4b4,0xb3b3b3,0xb2b2b2,0xb1b1b1,0xb0b0b0,
			0xafafaf,0xaeaeae,0xadadad,0xacacac,0xababab,0xaaaaaa,0xa9a9a9,0xa8a8a8,
			0xa7a7a7,0xa6a6a6,0xa5a5a5,0xa4a4a4,0xa3a3a3,0xa2a2a2,0xa1a1a1,0xa0a0a0,
			0x9f9f9f,0x9e9e9e,0x9d9d9d,0x9c9c9c,0x9b9b9b,0x9a9a9a,0x999999,0x989898,
			0x979797,0x969696,0x959595,0x949494,0x939393,0x929292,0x919191,0x909090,
			0x8f8f8f,0x8e8e8e,0x8d8d8d,0x8c8c8c,0x8b8b8b,0x8a8a8a,0x898989,0x888888,
			0x878787,0x868686,0x858585,0x848484,0x838383,0x828282,0x818181,0x808080,
			0x7f7f7f,0x7e7e7e,0x7d7d7d,0x7c7c7c,0x7b7b7b,0x7a7a7a,0x797979,0x787878,
			0x777777,0x767676,0x757575,0x747474,0x737373,0x727272,0x717171,0x707070,
			0x6f6f6f,0x6e6e6e,0x6d6d6d,0x6c6c6c,0x6b6b6b,0x6a6a6a,0x696969,0x686868,
			0x676767,0x666666,0x656565,0x646464,0x636363,0x626262,0x616161,0x606060,
			0x5f5f5f,0x5e5e5e,0x5d5d5d,0x5c5c5c,0x5b5b5b,0x5a5a5a,0x595959,0x585858,
			0x575757,0x565656,0x555555,0x545454,0x535353,0x525252,0x515151,0x505050,
			0x4f4f4f,0x4e4e4e,0x4d4d4d,0x4c4c4c,0x4b4b4b,0x4a4a4a,0x494949,0x484848,
			0x474747,0x464646,0x454545,0x444444,0x434343,0x424242,0x414141,0x404040,
			0x3f3f3f,0x3e3e3e,0x3d3d3d,0x3c3c3c,0x3b3b3b,0x3a3a3a,0x393939,0x383838,
			0x373737,0x363636,0x353535,0x343434,0x333333,0x323232,0x313131,0x303030,
			0x2f2f2f,0x2e2e2e,0x2d2d2d,0x2c2c2c,0x2b2b2b,0x2a2a2a,0x292929,0x282828,
			0x272727,0x262626,0x252525,0x242424,0x232323,0x222222,0x212121,0x202020,
			0x1f1f1f,0x1e1e1e,0x1d1d1d,0x1c1c1c,0x1b1b1b,0x1a1a1a,0x191919,0x181818,
			0x171717,0x161616,0x151515,0x141414,0x131313,0x121212,0x111111,0x101010,
			0x0f0f0f,0x0e0e0e,0x0d0d0d,0x0c0c0c,0x0b0b0b,0x0a0a0a,0x090909,0x080808,
			0x070707,0x060606,0x050505,0x040404,0x030303,0x020202,0x010101,0x000000},
		   {0x000000,0x000011,0x000021,0x00002a,0x000031,0x000038,0x00003f,0x000046,
			0x00004d,0x000053,0x010057,0x02005b,0x03005f,0x040063,0x060067,0x07006a,
			0x09006e,0x0b0073,0x0d0075,0x0d0076,0x100078,0x13007a,0x16007c,0x19007e,
			0x1c0081,0x1f0083,0x220085,0x260087,0x290089,0x2c008a,0x30008c,0x33008e,
			0x37008e,0x390090,0x3c0092,0x3e0093,0x410094,0x440095,0x460096,0x490096,
			0x4c0097,0x4f0097,0x510097,0x540098,0x570099,0x5b0099,0x5d009a,0x61009b,
			0x64009b,0x66009b,0x6a009b,0x6d009c,0x6f009c,0x71009d,0x74009d,0x77009d,
			0x7a009d,0x7e009d,0x80009d,0x83009d,0x86009d,0x88009d,0x8a009d,0x8d009d,
			0x90009c,0x94009c,0x96009b,0x99009b,0x9b009b,0x9d009b,0xa0009b,0xa3009b,
			0xa5009b,0xa7009a,0xa90099,0xaa0099,0xad0099,0xaf0198,0xb00198,0xb10197,
			0xb30196,0xb50295,0xb60295,0xb80395,0xba0495,0xba0494,0xbc0593,0xbe0692,
			0xbf0692,0xc00791,0xc10890,0xc20a8f,0xc30a8e,0xc40c8d,0xc60d8b,0xc60e8a,
			0xc81088,0xca1286,0xca1385,0xcb1484,0xcd1681,0xce187f,0xcf187c,0xd01a79,
			0xd11c77,0xd21c75,0xd31e72,0xd42170,0xd4226d,0xd52469,0xd72665,0xd82763,
			0xd92a60,0xda2c5c,0xdb2f58,0xdc2f53,0xdd314e,0xde3348,0xdf3443,0xdf363d,
			0xe03838,0xe03932,0xe23b2d,0xe33d27,0xe43f21,0xe4411d,0xe5431b,0xe54518,
			0xe64616,0xe74814,0xe84a12,0xe84c10,0xe94d0e,0xea4e0c,0xeb500b,0xeb510a,
			0xeb5309,0xec5508,0xec5708,0xed5907,0xed5b06,0xee5c06,0xee5d05,0xef5f04,
			0xef6104,0xef6204,0xf06403,0xf16603,0xf16603,0xf16803,0xf16a02,0xf16b02,
			0xf26c01,0xf26e01,0xf36f01,0xf37101,0xf47300,0xf47500,0xf47600,0xf47800,
			0xf57b00,0xf57d00,0xf57f00,0xf68100,0xf68200,0xf78400,0xf78600,0xf88800,
			0xf88800,0xf88a00,0xf88c00,0xf98d00,0xf98e00,0xf99000,0xf99100,0xf99300,
			0xfa9500,0xfb9700,0xfb9900,0xfb9c00,0xfc9e00,0xfca000,0xfda200,0xfda400,
			0xfda600,0xfda800,0xfdab00,0xfdad00,0xfdae00,0xfeb000,0xfeb200,0xfeb300,
			0xfeb500,0xfeb700,0xfeb900,0xfeba00,0xfebc00,0xfebe00,0xfec000,0xfec200,
			0xfec400,0xfec500,0xfec700,0xfec901,0xfeca01,0xfecb01,0xfecd02,0xfece03,
			0xfecf04,0xfed106,0xfed409,0xfed50a,0xfed70b,0xfed90d,0xffda0e,0xffdb10,
			0xffdc14,0xffdd17,0xffde1c,0xffe020,0xffe223,0xffe227,0xffe42b,0xffe530,
			0xffe636,0xffe73c,0xffe942,0xffea47,0xffeb4d,0xffed53,0xffee59,0xffee60,
			0xffef67,0xfff06d,0xfff174,0xfff17b,0xfff284,0xfff28c,0xfff493,0xfff499,
			0xfff5a0,0xfff5a7,0xfff6af,0xfff7b5,0xfff8bb,0xfff8c1,0xfff9c6,0xfff9cb,
			0xfffad1,0xfffbd7,0xfffcdd,0xfffde3,0xfffde8,0xfffeed,0xfffef2,0xffffff}
	};

	const char bmp_template[] =
			{'B', 'M',               // 0x00: (2 bytes) "BM"
			 0x00, 0x00, 0x00, 0x00, // 0x02: (4 bytes) size of the BMP file
			 0x00, 0x00,             // 0x06: (2 bytes) Reserved, can be 0
			 0x00, 0x00,             // 0x08: (2 bytes) Reserved, can be 0
			 0x7a, 0x04, 0x00, 0x00, // 0x0a: (4 bytes) offset to the bitmap (here fixed size 0x47a)
			 0x6c, 0x00, 0x00, 0x00, // 0x0e: (4 bytes) DIB header 108 bytes (BITMAPV4HEADER)
			 0x00, 0x00, 0x00, 0x00, // 0x12: (4 bytes) image width,  signed (negative - right-to-left)
			 0x00, 0x00, 0x00, 0x00, // 0x16: (4 bytes) image height, signed (negative - top-to-bottom)
			 0x01, 0x00,             // 0x1a: (2 bytes) number of color planes == 1
			 0x08, 0x00,             // 0x1c: (2 bytes) number of bits per pixel = 8
			 0x00, 0x00, 0x00, 0x00, // 0x1e: (4 bytes) compression method
			 0x00, 0x00, 0x00, 0x00, // 0x22: (4 bytes) image size (bitmap length 0x50000 for 640x512)
			 0x13, 0x0b, 0x00, 0x00, // 0x26: (4 bytes) horizontal pixels per meter (was 72 ppi)
			 0x13, 0x0b, 0x00, 0x00, // 0x2a: (4 bytes) vertical pixels per meter (was 72 ppi)
			 0x00, 0x01, 0x00, 0x00, // 0x2e: (4 bytes) number of colors
			 0x00, 0x01, 0x00, 0x00, // 0x32: (4 bytes) number of important colors
			 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // was some junk I did not understand,
			 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // probably a shorter (core) header may be used
			 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			 0x00, 0x00, 0x00, 0x00};
	if (buffer) {
		if (palette < 0) {
			palette = 0;
		} else if (palette > 2){
			palette = 2;
		}
		if (telem){
			if (telem > 0){
				height -= telem;
			} else {
				height += telem;
			}
		}

		D(fprintf(stderr, "Filling BMP header: %p, palette=%d\n",buffer, palette));
		bitmap_size = width * height;
		filesize = bitmap_size + sizeof(bmp_template) + sizeof(palette012[0]);
		height = -height; // change to normal direction
		D(fprintf(stderr, "bitmap_size = %d (0x%x), filesize = %d (0x%x), height= %d(0x%x)\n",bitmap_size,bitmap_size,filesize,filesize,height,height ));
		memcpy(buffer,                      bmp_template, sizeof(bmp_template));
		D(fprintf(stderr, "buffer[0..3]=0x%x 0x%x 0x%x 0x%x\n",buffer[0],buffer[1],buffer[2],buffer[3]));
		D(fprintf(stderr, "buffer+sizeof(bmp_template)=%p, palette012[palette] = %p, sizeof(palette012[0])=%d, sizeof(palette012) = %d\n",
				(buffer+sizeof(bmp_template)), palette012[palette], sizeof(palette012[0]), sizeof(palette012[0])));
		memcpy(buffer+sizeof(bmp_template), palette012[palette], sizeof(palette012[0]));
		D(fprintf(stderr, "buffer[palette...]=0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x\n",
				buffer[sizeof(bmp_template)+0],buffer[sizeof(bmp_template)+1],buffer[sizeof(bmp_template)+2],buffer[sizeof(bmp_template)+3],
				buffer[sizeof(bmp_template)+4],buffer[sizeof(bmp_template)+5],buffer[sizeof(bmp_template)+6],buffer[sizeof(bmp_template)+7]));
		memcpy(buffer + 0x02,               &filesize, 4);
		memcpy(buffer + 0x22,               &bitmap_size, 4);
		memcpy(buffer + 0x12,               &width,  4);
		memcpy(buffer + 0x16,               &height, 4);
		D(fprintf(stderr, "buffer[2..5]=0x%x 0x%x 0x%x 0x%x\n",buffer[2],buffer[3],buffer[4],buffer[5]));
	}
	D(fprintf(stderr, "BMP header size: %d (0x%x) bytes\n",
			          sizeof(bmp_template) + sizeof(palette012[0]),
					  sizeof(bmp_template) + sizeof(palette012[0])));
	return sizeof(bmp_template) + sizeof(palette012[0]);
}

/**
 * Convert 16-bit big-endian tiff to 8-bit by mapping [mn,mx] range to [0,255] skipping
 * telemetry (top lines if >0 or bottom lines if < 0. Offset is modified to process two
 * halves of the file when wrapped around in the cirbuf.
 * @param dst pointer to the destination buffer (byte per entry)
 * @param src pointer to the source buffer (in circbuf, big-endian u16 per entry)
 * @param offs offset (in pixels) in file for split images
 * @param len number of pixels to copy
 * @param mn minimal input pixel value to be mapped to 0
 * @param mx maximal input pixel value to be mapped to 255
 * @param width - image width in pixels
 * @param height - full image height, including telemetry
 * @param telem telemetry lines (>0 - top, <0 - bottom)
 * @return pointer in the output buffer after the last copied
 */
uint8_t * convertImage (
		uint8_t *  dst,
		uint16_t * src,
		int *      offs,
		int        len,
		int        mn,
		int        mx,
		int        width,
		int        height,
		int        telem){
	uint16_t pix16;
	int ipix;
	uint8_t * pix8;
	int iscale;
	int start_pix = 0;    // index in src to start copying
	int end_pix =   len;  // index in src to end copying
	if (telem > 0) {
		if (*offs < telem * width) {
			start_pix =  telem * width - *offs;
		}

	} else if (telem < 0){
		if (*offs +len > width * (height+telem)){
			end_pix = width * (height+telem) - *offs;
		}
	}
	D(fprintf(stderr, "convertImage(): telem= %d, start_pix=%d, end_pix=%d \n",	telem,start_pix,end_pix));

	iscale = 0x7f800000/(mx - mn);
	D(fprintf(stderr, "convertImage(): *offs= %d, len=%d, mn=%d, mx=%d, width=%d, height=%d,telem=%d, iscale=%d(0x%x)\n",
			*offs, len,mn,mx,width,height,telem,iscale,iscale));

	for (; start_pix < end_pix; start_pix++) {

//		pix16 = src[start_pix];
//		__bswap_16 (pix16);

//		ipix = pix16;
		ipix = __bswap_16 (src[start_pix]);

		pix8 = 0;
		if (ipix >= mx){
			pix8 = 255;
		} else if (ipix >= mn){
			pix8 = (((ipix - mn) * iscale) >> 23) & 0xff;
		}
#if 0//ELPHEL_DEBUG_THIS
		if ((start_pix >=164160) && (start_pix < 164164)){
			D(fprintf(stderr, "start_pix=%d(0x%x), src[start_pix] = %d(0x%x), ipix=%d(0x%x), ((ipix - mn) * iscale)=%d(0x%x), pix8= %d(0x%x)\n",
					start_pix,start_pix,src[start_pix],src[start_pix], ipix,ipix, ((ipix - mn) * iscale), ((ipix - mn) * iscale), pix8,pix8));
		}
#endif
		*dst = pix8;
		dst++;
	}
    *offs = end_pix; // index in the whole image (including telem)
	D(fprintf(stderr, "convertImage()->: *offs= %d\n",*offs));
	return dst;
}

int cumulHistImage ( // returns number of pixels
		uint16_t * src,
		int *      offs,
		int        len,
		int        width,
		int        height,
		int        telem,
		int        bin_shift,
		int        hist_size,
		int        hist_min,
		int *      hist16, // if null, will only calculate *sum_val, *mn and *mx
		long long* sum_val,
		int *      mn,
		int *      mx
		)
{
//	uint16_t pix16;
	int ibin;
	int start_pix = 0;    // index in src to start copying
	int end_pix =   len;  // index in src to end copying
	if (telem > 0) {
		if (*offs < telem * width) {
			start_pix =  telem * width - *offs;
		}

	} else if (telem < 0){
		if (*offs +len > width * (height+telem)){
			end_pix = width * (height+telem) - *offs;
		}
	}
	D(fprintf(stderr, "histImage(): telem= %d, start_pix=%d, end_pix=%d, *sum_val=%lld, size of (long) = %d\n",	telem, start_pix, end_pix, *sum_val, sizeof(long)));
	D(fprintf(stderr, "histImage(): *offs= %d, len=%d, width=%d, height=%d,telem=%d\n",	*offs, len, width,height,telem));
	D(fprintf(stderr, "histImage(): bin_shift= %d, hist_size=%d, hist_min=%d \n",	bin_shift, hist_size, hist_min));

	int max_bin = 0;
	int min_bin = hist_size;
	int num_pix = end_pix-start_pix;
	D(fprintf(stderr, "src[start_pix] = %p, start_pix=%d, len=%d\n",&src[start_pix], start_pix, end_pix-start_pix));

	for (; start_pix < end_pix; start_pix++) {
		ibin = __bswap_16 (src[start_pix]);
		*sum_val +=  ibin;
		if (ibin < *mn) *mn = ibin;
		if (ibin > *mx) *mx = ibin;
		if (hist16)  { // only calculate sum (for average, min and max if hist16== null
			ibin -= hist_min;
			if (ibin < 0) ibin = 0;
			ibin >>= bin_shift;
			if (ibin >= hist_size) ibin = hist_size - 1;
			if (ibin < min_bin) min_bin = ibin;
			if (ibin > max_bin) max_bin = ibin;
			hist16[ibin] ++;
		}
	}
    *offs = end_pix; // index in the whole image (including telem)
	D(fprintf(stderr, "histImage(): min_bin= %d, max_bin=%d\n",min_bin, max_bin));
	if (hist16) {
		D(fprintf(stderr, "histImage(): hist16[min...]= %d %d %d %d %d %d %d %d\n",
				hist16[min_bin+0],hist16[min_bin+1],hist16[min_bin+2],hist16[min_bin+3],hist16[min_bin+4],hist16[min_bin+5],hist16[min_bin+6],hist16[min_bin+7]));
		D(fprintf(stderr, "histImage(): hist16[...max]= %d %d %d %d %d %d %d %d\n",
				hist16[max_bin-7],hist16[max_bin-6],hist16[max_bin-5],hist16[max_bin-4],hist16[max_bin-3],hist16[max_bin-2],hist16[max_bin-1],hist16[max_bin-0]));
	}
	D(fprintf(stderr, "histImage(): *mn= %d, *mx=%d, *sum_val=%lld\n",*mn, *mx, *sum_val));
	D(fprintf(stderr, "histImage()->: *offs= %d, num_pix=%d\n",*offs,num_pix));
	return num_pix;
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
 * @param[in]   tiff_convert    convert image to 8-bit BMP
 * @param[in]   tiff_mn    		value to map to output 0
 * @param[in]   tiff_mx    		value to map to output 255
 * @param[in]   tiff_palette    0 - hot white, 1 - hot black, 2 - color
 * @param[in]   tiff_telem    	0 - convert all lines,>0 skip first lines, < 0 - skip last lines
 *
 * @return      0 if file was successfully sent and negative error code otherwise
 */
int  sendImage(struct file_set *fset,
		       int              bufferImageData,
			   int              use_Exif,
			   int              saveImage,
			   int              tiff_convert,
			   int              tiff_mn,
			   int              tiff_mx,
			   int              tiff_palette,
			   int              tiff_telem)

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
	int width;
	int height;
	uint8_t * out_ptr; // output pointer between the two-part BMP generation from the 16-bit TIFF
	int tiff_offs;
	jpeg_start = lseek(fset->circbuf_fd, 0, SEEK_CUR);     //get the current read pointer
	D(fprintf(stderr, "jpeg_start (long) = 0x%x, bufferImageData=%d\n", jpeg_start,bufferImageData));

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
	width = frame_params.width;
	height = frame_params.height;
	if (tiff_convert){
		bufferImageData = 1; // OK even if tiff_convert will be disabled below
	}

	// disable convert if not COLORMODE_RAW
	if (color_mode != COLORMODE_RAW) {
		tiff_convert = 0;
	}


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

	if (color_mode == COLORMODE_RAW) {
		head_size = 0;
	} else { // some compressed mode
		if (fset->jphead_fd<0)
			fset->jphead_fd = open(fset->jphead_fn, O_RDWR);
		if (fset->jphead_fd < 0) { // check control OK
			fprintf(stderr, "Error opening %s\n", fset->jphead_fn);
			return -1;
		}
		lseek(fset->jphead_fd, jpeg_start + 1, SEEK_END);              // create JPEG header, find out it's size
		head_size = lseek(fset->jphead_fd, 0, SEEK_END);
		if (head_size > JPEG_HEADER_MAXSIZE) {
			fprintf(stderr, "%s:%d: Too big JPEG header (%d > %d)", __FILE__, __LINE__, head_size, JPEG_HEADER_MAXSIZE );
			close(fset->jphead_fd);
			fset->jphead_fd = -1;
			return -2;
		}
	}

    // Copy timestamp (goes after the image data)
    timestamp_start=jpeg_start+((jpeg_len+CCAM_MMAP_META+3) & (~0x1f)) + 32 - CCAM_MMAP_META_SEC; // magic shift - should index first byte of the time stamp
    if (timestamp_start >= buff_size) timestamp_start-=buff_size;
    memcpy (&(frame_params.timestamp_sec), (unsigned long * ) &ccam_dma_buf[timestamp_start>>2],8);

	D(fprintf(stderr, "sendImage(): tiff_convert=0x%x, color_mode=0x%x, bufferImageData=0x%x\n",tiff_convert,color_mode,bufferImageData));
	D(fprintf(stderr, "sendImage(): tiff_palette=%d, tiff_mn=%d, tiff_mx=%d, tiff_telem=%d\n",tiff_palette, tiff_mn, tiff_mx, tiff_telem));
	if (tiff_convert) {
		exifDataSize = getBmpHeader( // bitmap header and palette
				NULL,         // void * buffer,
				width,        // int    width,
				height,       // int    height,
				tiff_palette,
				tiff_telem);
		D(fprintf(stderr, "exifDataSize=0x%x\n",exifDataSize));
	} else {
		if (color_mode == COLORMODE_RAW) {
			if (fset->tiff_dev_fd <0)
				fset->tiff_dev_fd = open(fset->tiff_dev_name, O_RDONLY);
			D(fprintf(stderr, "open(%s) -> %x\n",fset->tiff_dev_name,fset->tiff_dev_fd));

			if (fset->tiff_dev_fd < 0) {                                 // check control OK
				fprintf(stderr, "Error opening %s\n", fset->tiff_dev_name);
				close(fset->jphead_fd);
				fset->jphead_fd = -1;
				return -5;
			}
			exifDataSize = lseek(fset->tiff_dev_fd, 1, SEEK_END);  // at the beginning of page 1 - position == page length
			if (exifDataSize < 0) exifDataSize = 0;                // error from lseek;
			if (!exifDataSize){
				close(fset->tiff_dev_fd);
				fset->tiff_dev_fd = -1;
			}
		} else if (use_Exif) {
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
	}
	// Maybe make buffer that will fit both Exif and JPEG?
	// Get metadata, update Exif and JFIF headers if ep and ed pointers are non-zero (NULL will generate files with JFIF-only headers)
	// Now we always malloc buffer, before - only for bimg, using fixed-size header buffer - was it faster?

	jpeg_full_size = (tiff_convert? (jpeg_len/2): jpeg_len) + head_size + exifDataSize + ((head_size > 0) ? 2 : 0);

	// jpeg_len is the FPGA-generated image data, in convert mode number of pixels is half that

//	fprintf(stderr, "jpeg_len = 0x%x, head_size = 0x%x, exifDataSize = 0x%x, jpeg_full_size = 0x%x\n",
//			jpeg_len, head_size, exifDataSize, jpeg_full_size);

	if (bufferImageData) jpeg_this_size = jpeg_full_size;             // header+frame always with tiff_convert
	else                 jpeg_this_size = head_size + exifDataSize;   // only header
	D(fprintf(stderr, "jpeg_full_size=%d (0x%x)\n",jpeg_full_size,jpeg_full_size));
	D(fprintf(stderr, "jpeg_this_size=%d (0x%x)\n",jpeg_this_size,jpeg_this_size));

	jpeg_copy = malloc(jpeg_this_size);
	if (!jpeg_copy) {
		D(fprintf(stderr, "Malloc error allocating %d (0x%x) bytes\n",jpeg_this_size,jpeg_this_size));
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

	D(fprintf(stderr, "Allocated %d (0x%x) bytes to buffer image, tiff_convert=%d\n",jpeg_this_size,jpeg_this_size, tiff_convert));
	// copy header
	if (tiff_convert) { // BMP mode
		getBmpHeader( // bitmap header and palette
				(uint8_t *) &jpeg_copy[0], // uint8_t * buffer,
				width,                     // int    width,
				height,                    // int    height,
				tiff_palette,             // int    palette,
				tiff_telem);
		D(fprintf(stderr, "BMP mode: exifDataSize-> %d jpeg_copy[0..3]=0x%02x 0x%02x 0x%02x 0x%02x\n",exifDataSize, jpeg_copy[0], jpeg_copy[1], jpeg_copy[2], jpeg_copy[3]));
	} else if (color_mode == COLORMODE_RAW) {
		int ipos, iread;
		ipos=lseek(fset->tiff_dev_fd, frame_params.meta_index,SEEK_END);   // select meta page to use (matching frame)
		iread=read (fset->tiff_dev_fd, &jpeg_copy[0], exifDataSize);        // Read Tiff header
		close(fset->tiff_dev_fd);
		D(fprintf(stderr, "fset->tiff_dev_fd = 0x%x, ipos=0x%x, iread=0x%x, meta_index=0x%x\n",fset->tiff_dev_fd, ipos, iread, frame_params.meta_index));
		fset->tiff_dev_fd = -1;
		D(fprintf(stderr, "TIFF mode: exifDataSize-> %d jpeg_copy[0..3]=0x%02x 0x%02x 0x%02x 0x%02x\n",exifDataSize, jpeg_copy[0], jpeg_copy[1], jpeg_copy[2], jpeg_copy[3]));
	} else {
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
	}
	switch (color_mode) {
	//   case COLORMODE_MONO6:    //! monochrome, (4:2:0),
	//   case COLORMODE_COLOR:    //! color, 4:2:0, 18x18(old)
	case COLORMODE_RAW:     // jp4, original (4:2:0)
		if (tiff_convert) {
			mime_type = "bmp";
			extension = "bmp";
		} else {
			mime_type = "tiff";
			extension = "tiff";
		}
		break;
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
	default:
		mime_type = "jpeg";
		extension = "jpeg";

	}
	D(fprintf(stderr, "MIME = '%s', extension = '%s',bufferImageData=%df\n",mime_type, extension,bufferImageData));
	printf("Content-Type: image/%s\r\n", mime_type);
    if (saveImage) {
        printf("Content-Disposition: attachment; filename=\"%s%ld_%06d_%d.%s\"\r\n",      // does not open, asks for filename to save
                fset->timestamp_name?"":"elphelimg_", frame_params.timestamp_sec,  frame_params.timestamp_usec, fset->sensor_port + fset->base_chn, extension);
    } else {
        printf("Content-Disposition: inline; filename=\"%s%ld_%06d_%d.%s\"\r\n",                    // opens in browser, asks to save on right-click
                fset->timestamp_name?"":"elphelimg_", frame_params.timestamp_sec,  frame_params.timestamp_usec, fset->sensor_port + fset->base_chn, extension);
    }
    // bufferImageData - always with tiff_convert
	if (bufferImageData) { // Buffer the whole file before sending over the network to make sure it will not be corrupted if the circular buffer will be overrun)
#if ELPHEL_DEBUG_THIS
		unsigned long start_time, end_time;
		start_time = lseek(fset->circbuf_fd, LSEEK_CIRC_UTIME, SEEK_END);
#endif
		l = head_size + exifDataSize;
		tiff_offs = 0;
		/* JPEG image data may be split in two segments (rolled over buffer end) - process both variants */
		if ((jpeg_start + jpeg_len) > buff_size) { // two segments
			D(fprintf(stderr, "Two-part image, l=%0x%x, tiff_convert=%d\n",l,tiff_convert));
			if (tiff_convert) {
				// first part
				out_ptr = convertImage (
					(uint8_t * ) &jpeg_copy[l],                  // uint8_t *  dst,
					(uint16_t *) &ccam_dma_buf[jpeg_start >> 2], //   src,
					&tiff_offs,                                  // int *      offs,
					(buff_size - jpeg_start)/2,                  // int        len,// in pixels - number of pixels to copy
					tiff_mn,                                     // int        mn,
					tiff_mx,                                     // int        mx,
					width,                                       // int        width,
					height,                                      // int        height,
					tiff_telem);                                 // int        telem);
				// second part
				l += buff_size - jpeg_start;
				convertImage (
					out_ptr,                                     // uint8_t *  dst, continue from where stopped
					(uint16_t *) &ccam_dma_buf[0],               // jpeg_len - (buff_size - jpeg_start)), //   src,
					&tiff_offs,                                  // int *      offs,
					(jpeg_len - (buff_size - jpeg_start))/2,     // int        len,// in pixels - number of pixels to copy
					tiff_mn,                                     // int        mn,
					tiff_mx,                                     // int        mx,
					width,                                       // int        width,
					height,                                      // int        height,
					tiff_telem);                                 // int        telem);
			} else {
				memcpy(&jpeg_copy[l], (unsigned long* )&ccam_dma_buf[jpeg_start >> 2], buff_size - jpeg_start);
				D(fprintf(stderr, "memcpy(%p,%p,%d)\n",&jpeg_copy[l],&ccam_dma_buf[jpeg_start >> 2], buff_size - jpeg_start));

				l += buff_size - jpeg_start;
				memcpy(&jpeg_copy[l], (unsigned long* )&ccam_dma_buf[0], jpeg_len - (buff_size - jpeg_start));
				D(fprintf(stderr, "memcpy(%p,%p,%d)\n",&jpeg_copy[l],&ccam_dma_buf[0], jpeg_len - (buff_size - jpeg_start)));
			}
			D1(fprintf(stderr, "Two parts (buffered), jpeg_start (long) = 0x%x\n", jpeg_start));
		} else { /* single segment */
			D(fprintf(stderr, "One-part image, l=%0x%x, tiff_convert=%d\n",l,tiff_convert));
			if (tiff_convert) {
			   convertImage (
					(uint8_t * ) &jpeg_copy[l],                  // uint8_t *  dst,
					(uint16_t *) &ccam_dma_buf[jpeg_start >> 2], //   src,
					&tiff_offs,                                  // int *      offs,
					jpeg_len/2,                                  // int        len,// in pixels - number of pixels to copy
					tiff_mn,                                     // int        mn,
					tiff_mx,                                     // int        mx,
					width,                                       // int        width,
					height,                                      // int        height,
					tiff_telem);                                 // int        telem);
			} else {
				memcpy(&jpeg_copy[l], (unsigned long* )&ccam_dma_buf[jpeg_start >> 2], jpeg_len);
			}
			D1(fprintf(stderr, "One part (buffered), jpeg_start (long) = 0x%x, buff_size = 0x%x\n", jpeg_start, buff_size));
		}
#if ELPHEL_DEBUG_THIS
		end_time = lseek(fset->circbuf_fd, LSEEK_CIRC_UTIME, SEEK_END);
		D(fprintf(stderr, "memcpy time = %lu\n", end_time - start_time));
#endif
		if (color_mode != COLORMODE_RAW) memcpy(&jpeg_copy[jpeg_len + head_size + exifDataSize], trailer, 2); // Only for JPEG flavors
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
		if (color_mode != COLORMODE_RAW) sendBuffer(trailer, 2); // Only for JPEG flavors
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
	char * cp, *cp1, *cp2, *cpeq;
	int slow, skip;         // reduce frame rate by slow
	int base_chn;
	int sent2socket = -1;   // 1 - img, 2 - meta, 3 - pointers
	struct sockaddr_in sock;
	int res;
	int one = 1;
	int buff_size;
    int frame_number,compressed_frame_number;
    int iv;
    int tiff_convert=0 ; // - convert tiff to bmp for preview
    int tiff_mn= 0; // 20000;
    int tiff_mx= 0xffff; // 22000;
    int tiff_palette = 0;
    int tiff_telem = 0; // 0 - none,>0 - skip first rows, <0 - skip last rows
    int tiff_bin_shift =   3; // histogram bin size is 1<<3 == 8
    int tiff_bin = 1;
    int tiff_auto = -1;


//	  int    bin_shift,   // binsize = 1 << bin_shift
	int    tiff_hist_size;   // size of the histogram = ( 1 << size_shift)
	int    tiff_hist_min;   // subtract from value before binning


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
			tiff_convert = 0;
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
			if ((strncmp(cp, "frame", 5) == 0) || (strncmp(cp, "wframe", 6) == 0) || (strncmp(cp, "sframe", 6) == 0)	) {
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
			D(fprintf(stderr, "\n"));
			D(fprintf(stderr, "%s: current frame pointer this_p (in bytes): 0x%x\n", __func__, this_p));
			// continue with iterating through the commands
			while ((cp1 = strsep(&cp, "/?&"))) {
				// if the first character is a digit, it is a file pointer
				if (strchr("0123456789", cp1[0])) {
					this_p = lseek(fset->circbuf_fd, strtol(cp1, NULL, 10), SEEK_SET);
					// should simg &sbimg be always buffered???
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
						rslt = sendImage(fset, buf_images, exif_enable, suggest_save_images, // verify driver that file pointer did not move
								tiff_convert, tiff_mn, tiff_mx, tiff_palette, tiff_telem);
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
							rslt = sendImage(fset, buf_images, exif_enable, 0, // verify driver that file pointer did not move
									         tiff_convert, tiff_mn, tiff_mx, tiff_palette, tiff_telem);
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
				} else if (strcmp(cp1, "xframe") == 0) {
					if (sent2socket > 0) break;                             // image/xmldata was already sent to socket, ignore
					frameNumbersXML(fset);
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
                } else if (strncmp(cp1, "tiff_", 5) == 0) { // combined several parameters starting with "tiff_"
                	// all these commands end with "=<value>"
                	cpeq = strchr(cp1,'='); // char *
                	D(fprintf(stderr, "got 'tiff_' in '%s'\n",cp1));
                	if (cpeq) { // ignore if there is no "=" in the token
                    	iv = strtol(cpeq+1, NULL, 10);
                    	D(fprintf(stderr, "iv=%d\n",iv));
                		if        (strncmp(cp1, "tiff_mn", 7) == 0) {
                			tiff_mn = iv;
                        	D(fprintf(stderr, "tiff_mn=%d'\n",tiff_mn));
                		} else if (strncmp(cp1, "tiff_mx", 7) == 0) {
                    		tiff_mx = iv;
                        	D(fprintf(stderr, "tiff_mx=%d'\n",tiff_mx));
                		} else if (strncmp(cp1, "tiff_palette", 12) == 0) {
                    		tiff_palette = iv;
                        	D(fprintf(stderr, "tiff_palette=%d'\n",tiff_palette));

                		} else if (strncmp(cp1, "tiff_telem", 10) == 0) {
                    		tiff_telem = iv;
                        	D(fprintf(stderr, "tiff_telem=%d'\n",tiff_telem));

                		} else if (strncmp(cp1, "tiff_bin", 8) == 0) {
                			tiff_bin_shift = iv;
                        	D(fprintf(stderr, "tiff_bin=%d'\n",tiff_bin_shift)); // bin_shift
                    	} else 	if (strncmp(cp1, "tiff_auto", 9) == 0) { // it is a command, not a parameter
                			tiff_auto = iv;
                        	D(fprintf(stderr, "tiff_auto=%d'\n",tiff_auto));
        					if (sent2socket > 0) break;  // image/xmldata was already sent to socket, ignore
        					// use tiff_min, tiff_max, tiff_bin to calculate histogram parameters;
        					tiff_bin = 1 << tiff_bin_shift;
        					tiff_hist_size = (tiff_mx - tiff_mn)/ tiff_bin;
        					if (tiff_mn + tiff_bin * tiff_hist_size < tiff_mx) tiff_hist_size++;
        					tiff_hist_size ++;
        					tiff_hist_min = tiff_mn - tiff_bin / 2;
        					if (tiff_hist_min < 0) tiff_hist_min = 0;
                        	D(fprintf(stderr, "TiffStats(auto=%d): tiff_bin_shift=%d, tiff_hist_size=%d, tiff_hist_min = %d, tiff_telem = %d\n",
                        			tiff_auto, tiff_bin_shift, tiff_hist_size, tiff_hist_min, tiff_telem));
        					iv = TiffStats(
        							fset,           // struct file_set fset,
        							tiff_auto,      // int    tiff_auto,   // -1 - return XML, >=0 set tiff_min and tiff_max (if non-NULL)
        							&tiff_mn,       // int *  tiff_min,
									&tiff_mx,       // int *  tiff_max,
    								tiff_bin_shift, // int    bin_shift,   // binsize = 1 << bin_shift
    								tiff_hist_size, // int    hist_size,   // size of the histogram = ( 1 << size_shift)
    								tiff_hist_min,  // int    hist_min,   // subtract from value before binning
    								tiff_telem);    // int    tiff_telem);
        					D(fprintf(stderr, "tiff_auto command: DONE: tiff_auto=%d, tiff_mn=%d, tiff_mx=%d\n",tiff_auto, tiff_mn, tiff_mx));
                		} else {
                        	D(fprintf(stderr, "unrecognized tiff_$ command: %s'\n",cp1)); // bin_shift
                		}
                	}
                	if (strncmp(cp1, "tiff_convert", 12) == 0) {
                		tiff_convert = 1;
                    	D(fprintf(stderr, "tiff_convert=%d'\n",tiff_convert));
                	} else 	if (strncmp(cp1, "tiff_stats", 10) == 0) { // it is a command, not a parameter
                    	D(fprintf(stderr, "tiff_stats command\n"));
    					if (sent2socket > 0) break;  // image/xmldata was already sent to socket, ignore
    					// use tiff_min, tiff_max, tiff_bin to calculate histogram parameters;
    					tiff_bin = 1 << tiff_bin_shift;
    					tiff_hist_size = (tiff_mx - tiff_mn)/ tiff_bin;
    					if (tiff_mn + tiff_bin * tiff_hist_size < tiff_mx) tiff_hist_size++;
    					tiff_hist_size ++;
    					tiff_hist_min = tiff_mn - tiff_bin / 2;
    					if (tiff_hist_min < 0) tiff_hist_min = 0;
                    	D(fprintf(stderr, "TiffStats(): tiff_bin_shift=%d, tiff_hist_size=%d, tiff_hist_min = %d, tiff_telem = %d\n",
                    			tiff_bin_shift, tiff_hist_size, tiff_hist_min, tiff_telem));

    					TiffStats(
    							fset,           // struct file_set fset,
    						    -1,             //   int    tiff_auto,   // -1 - return XML, >=0 set tiff_min and tiff_max (if non-NULL)
    							NULL,           //  int *  tiff_min,
								NULL,           //  int *  tiff_max,
								tiff_bin_shift, // 	  int    bin_shift,   // binsize = 1 << bin_shift
								tiff_hist_size, // 	  int    hist_size,   // size of the histogram = ( 1 << size_shift)
								tiff_hist_min,  // 	  int    hist_min,   // subtract from value before binning
								tiff_telem);    // 	  int    tiff_telem);
                    	D(fprintf(stderr, "tiff_stats command: DONE, sent2socket=%d\n", sent2socket)); // gets here
    					sent2socket = 3;
    					fflush(stdout);         // let's not keep client waiting - anyway we've sent it all even when more commands  maybe left
//                    	_exit(0);
                	}
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
				} else if (strcmp(cp1, "reboot") == 0) {
					// Try to send response before dying
					if (sent2socket <= 0) { // Nothing was sent to the client so far and the command line is over. Let's return 1x1 pixel gif
						metaXML(fset, 0);
					}
					metaXML(fset, 2);   // 0 - new (send headers), 1 - continue, 2 - finish
					fflush(stdout); // probably it is not needed anymore, just in case
					sync();
					reboot(RB_AUTOBOOT);
				} else {
					if (cp1[0] != '_') fprintf(stderr, "Unrecognized URL command: \"%s\" - ignoring\n", cp1);  // allow "&_time=..." be silently ignored - needed for javascript image reload
				}
            	D(fprintf(stderr, "End of loop while ((cp1=strsep(&cp, \"/?&\"))), cp1 = '%s'\n",cp1));
			} // while ((cp1=strsep(&cp, "/?&")))
        	D(fprintf(stderr, "Loop  while ((cp1=strsep(&cp, \"/?&\" is over, sent2socket=%d\n", sent2socket)); // gets here
			if (sent2socket <= 0) { // Nothing was sent to the client so far and the command line is over. Let's return 1x1 pixel gif
				out1x1gif();
			} else if (sent2socket == 2) {
				metaXML(fset, 2);   // 0 - new (send headers), 1 - continue, 2 - finish
			}
        	D(fprintf(stderr, "Flushing stdout ...\n"));
			fflush(stdout); // probably it is not needed anymore, just in case
        	D(fprintf(stderr, "... and exiting\n"));
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
		fset[i].tiff_dev_name = tiff_dev_names[i];
		fset[i].tiff_dev_fd = -1;
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
