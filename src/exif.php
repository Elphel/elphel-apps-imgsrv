#!/usr/bin/env php
<?php
/*!
 *! FILE NAME  : exif.php
 *! DESCRIPTION: This program collects information about the camera (version, software, sensor)
 *! and combines it with the Exif template (default is /etc/Exif_template.xml) to prepare generation
 *! of Exif headers.
 *! It works in a command line with a single optional parameter - location of the template file and
 *! trough the CGI interface. In that case it accepts the following parameter
 *! init           - program Exif with default /etc/Exif_template.xml
 *! init=path      - program Exif with alternative file
 *! noGPS          - don't include GPS-related fields
 *! nocompass      - don't include compass-related fields
 *! template       - print currently loaded template data (hex dump)
 *! metadir        - print currently loaded meta directory that matches variable Exif fields with the template
 *! exif=0         - print current Exif page (updated in real time)
 *! exif=NNN       - print one of the Exif pages in the buffer (debug feature, current buffer pointer is not known here)
 *! chn=0..3       - sensor channel
 *!
 *! Copyright (C) 2007-2016 Elphel, Inc
 *! -----------------------------------------------------------------------------**
 *!
 *!  This program is free software: you can redistribute it and/or modify
 *!  it under the terms of the GNU General Public License as published by
 *!  the Free Software Foundation, either version 3 of the License, or
 *!  (at your option) any later version.
 *!
 *!  This program is distributed in the hope that it will be useful,
 *!  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *!  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *!  GNU General Public License for more details.
 *!
 *!  You should have received a copy of the GNU General Public License
 *!  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *! -----------------------------------------------------------------------------**
 *!  $Log: exif.php,v $
 *!  Revision 1.2  2010/08/10 21:14:31  elphel
 *!  8.0.8.39 - added EEPROM support for multiplexor and sensor boards, so autocampars.php uses application-specific defaults. Exif Orientation tag support, camera Model reflects application and optional mode (i.e. camera number in Eyesis)
 *!
 *!  Revision 1.1.1.1  2008/11/27 20:04:01  elphel
 *!
 *!
 *!  Revision 1.2  2008/08/11 19:11:32  elphel
 *!  comments
 *!
 *!  Revision 1.1  2008/04/07 09:12:14  elphel
 *!  New Exif template generation
 *!
 *!  $Log: exif.php,v $
 *!  Revision 1.2  2010/08/10 21:14:31  elphel
 *!  8.0.8.39 - added EEPROM support for multiplexor and sensor boards, so autocampars.php uses application-specific defaults. Exif Orientation tag support, camera Model reflects application and optional mode (i.e. camera number in Eyesis)
 *!
 *!  Revision 1.1.1.1  2008/11/27 20:04:01  elphel
 *!
 *!
 *!  Revision 1.2  2008/08/11 19:11:32  elphel
 *!  comments
 *!
 *!  Revision 1.1  2008/04/07 09:12:14  elphel
 *!  New Exif template generation
 *!
 *!  Revision 1.1.1.1  2007/10/03 19:05:53  elphel
 *!  This is a fresh tree based on elphel353-2.10
 *!
 *!  Revision 1.6  2007/10/03 19:05:53  elphel
 *!  cosmetic
 *!
 *!  Revision 1.5  2007/10/03 18:37:53  elphel
 *!  Made nice html output to see the result data
 *!
 *!  Revision 1.4  2007/10/03 06:33:31  elphel
 *!  fixed wrong copyright year
 *!
 *!  Revision 1.3  2007/10/02 19:44:03  elphel
 *!  minor bug fixes, added "manufacturer note" field for raw frame metadata (36 bytes - /include/asm-cris/c313.h frame_params_t)
 *!
 *!  Revision 1.2  2007/09/25 23:34:02  elphel
 *!  Fixed time strings to the right length
 *!
 *!  Revision 1.1  2007/09/24 07:27:57  elphel
 *!  Started php script that prepares Exif header information for serving images by imgsrv
 *!
 */
//TODO: Decide with the start sequence, more data is availble after the sensor is initialized.
// Or leave it to imgsrv (it still needs sensor to start first), just put placeholders
// use http://www.exiv2.org/tags.html  for reference of the Exif fields if you need to add modify
// the data below

$chn = 0;

$ExifDeviceTemplateFilename="/dev/exif_template";
$ExifDeviceMetadirFilename= "/dev/exif_metadir";
$TiffDeviceTemplateFilename="/dev/tiff_template";
$ExifDeviceExifFilename=    "/dev/exif_exif";
$TiffDeviceTiffFilename=    "/dev/tiff_tiff";
$ExifDeviceMetaFilename=    "/dev/exif_meta";


///$ExifDeviceTemplateFilename="/dev/null";
///$ExifDeviceMetadirFilename= "/dev/null";
///$TiffDeviceTemplateFilename="/dev/null";
///$ExifDeviceExifFilename=    "/dev/null";
///$ExifDeviceMetaFilename=    "/dev/null";



$DeviceSNFilename = "/sys/devices/soc0/elphel393-init/serial";
$DeviceRevisionFilename = "/sys/devices/soc0/elphel393-init/revision";
$DeviceBrand = "Elphel";
$DeviceModel = "Elphel393";

//! if called from the command line - accepts just one parameter - configuration file,
//! through CGI - accepts more parameters

/// $ExifXMLName="/etc/Exif_template.xml";
$ExifXMLName="./Exif_template.xml";

$init=false;
if ($_SERVER['REQUEST_METHOD']=="GET") {
	if ($_GET["init"]!==NULL) {
		if ($_GET["init"]) $ExifXMLName=$_GET["init"];
		$init=true; // in any case - filename specified or not
		$noGPS=     ($_GET["noGPS"]!==NULL);
		$nocompass= ($_GET["nocompass"]!==NULL);
	}
	if (isset($_GET['chn'])) $chn = $_GET['chn'];
	$ExifDeviceExifFilename .= $chn;
    $ExifDeviceMetaFilename .= $chn;
    $TiffDeviceTiffFilename .= $chn;
} else {
	foreach ($_SERVER['argv'] as $param) if (substr($param,0,4)=="init") {
		$param=substr($param,5);
		if (strlen($param)>0) $ExifXMLName=$param;
		$init=true; //
		break;
	}
	if ($init) {
		$noGPS=     in_array  ('noGPS'    , $_SERVER['argv']);
		$nocompass= in_array  ('nocompass', $_SERVER['argv']);
	} else {
		echo <<<USAGE

Usage: {$_SERVER['argv'][0]} [init[=filename.xml] [noGPS] [nocompass]]


USAGE;
		exit (0);
	}
}

define("EXIF_BYTE",      1);
define("EXIF_ASCII",     2);
define("EXIF_SHORT",     3);
define("EXIF_LONG",      4);
define("EXIF_RATIONAL",  5);
define("EXIF_SBYTE",     6);
define("EXIF_UNDEFINED", 7);
define("EXIF_SSHORT",    8); //obsolete?
define("EXIF_SLONG",     9);
define("EXIF_SRATIONAL",10);

define("EXIF_LSEEK_ENABLE", 2); // rebuild buffer

/// ===== init/init= CGI parameters or command line mode =======
if ($init) { // configure Exif data
	$exif_head= array (
		0xff, 0xe1, // APP1 marker at byte 2 of the file
		// offset=2 - start of Exif header
		0x00, 0x00, // - length of the Exif header (including this length bytes) - we'll fill it later, we do not know it yet
		0x45,0x78,0x69,0x66,0x00,0x00); // Exif header
	$Exif_length_offset= 2; // put total length here (big endian, 2 bytes)

	$exif_data= array (// start of TIFF Header, data offsets will match indexes in this array
		0x4d,0x4d, // (MM) Big endian, MSB goes first in multi-byte data
		0x00,0x2a,  // Tag Mark
		0x00,0x00,0x00,0x08); //offset to first IDF (from the beginning of the TIFF header, so 8 is minimum)
	$tiff_data= array (// start of TIFF Header, data offsets will match indexes in this array
		0x4d,0x4d, // (MM) Big endian, MSB goes first in multi-byte data
		0x00,0x2a,  // Tag Mark
		0x00,0x00,0x00,0x08); //offset to first IDF (from the beginning of the TIFF header, so 8 is minimum)
	$xml_both = simplexml_load_file($ExifXMLName); // include tags for either Exif or Tiff
	if ($xml_both->GPSInfo) {
		/// remove all tags named "Compass..."
		if ($nocompass) {
			$tounset=array();
			foreach ($xml_both->GPSInfo->children() as $entry) if (strpos  ($entry->getName()  , "Compass" )!==false) $tounset[]=$entry->getName();
			foreach ($tounset as $entry) unset ($xml_both->GPSInfo->{$entry});
		}
		if ($noGPS) {
			unset($xml_both->GPSInfo);
			unset($xml_both->Image->GPSTag);
		}
	}
	// now copy filtered $xml_both to $xml_exif and $xml_tiff to filter them separately
	$xml_exif = simplexml_load_string ( $xml_both->asXML());
	foreach ($xml_exif->children() as $group){
	    $tounset=array();
	    foreach ($group->children() as $entry) if ($entry->attributes()['mode'] == 'T') $tounset[]=$entry->getName(); // keep if no mode, mode =='E' or mode == 'B' 
	    foreach ($tounset as $entry) unset ($group->{$entry});
	}
	
	$xml_tiff = simplexml_load_string ( $xml_both->asXML());
	
	foreach ($xml_tiff->children() as $group){
	    $tounset=array();
	    foreach ($group->children() as $entry) if ($entry->attributes()['mode'] == 'E') $tounset[]=$entry->getName(); // keep if no mode, mode =='E' or mode == 'B'
	    foreach ($tounset as $entry) unset ($group->{$entry});
	}
	
	$IFD_offset_exif=      count($exif_data);                                         // 8
	$SUB_IFD_offset_exif=  12*count($xml_exif->Image->children())+2+4+$IFD_offset_exif;    // length of Image (main) IFD including length and offset to next
	$GPSInfo_offset_exif=  12*count($xml_exif->Photo->children())+2+4+$SUB_IFD_offset_exif;// length of Photo IFD including length and offset to next
	// Include or skip GPS IFD
	$data_offset_exif=     $GPSInfo_offset_exif+(($xml_exif->GPSInfo)?(12*count($xml_exif->GPSInfo->children())+2+4):0); //$GPSInfo is optional

	$IFD_offset_tiff=      count($tiff_data);                                         // 8
	$SUB_IFD_offset_tiff=  12*count($xml_tiff->Image->children())+2+4+$IFD_offset_tiff;    // length of Image (main) IFD including length and offset to next
	$GPSInfo_offset_tiff=  12*count($xml_tiff->Photo->children())+2+4+$SUB_IFD_offset_tiff;// length of Photo IFD including length and offset to next
	// Include or skip GPS IFD
	$data_offset_tiff=     $GPSInfo_offset_tiff+(($xml_tiff->GPSInfo)?(12*count($xml_tiff->GPSInfo->children())+2+4):0); //$GPSInfo is optional
	
	
	if ($_SERVER['REQUEST_METHOD']) {
		echo "<pre>";
		printf ("IFD_offset_exif=0x%x\n",$IFD_offset_exif);
		printf ("SUB_IFD_offset_exif=0x%x\n",$SUB_IFD_offset_exif);
		printf ("GPSInfo_offset_exif=0x%x\n",$GPSInfo_offset_exif);
		printf ("data_offset_exif=0x%x\n",$data_offset_exif);
		
		printf ("IFD_offset_tiff=0x%x\n",$IFD_offset_tiff);
		printf ("SUB_IFD_offset_tiff=0x%x\n",$SUB_IFD_offset_tiff);
		printf ("GPSInfo_offset_tiff=0x%x\n",$GPSInfo_offset_tiff);
		printf ("data_offset_tiff=0x%x\n",$data_offset_tiff);
	}
/*
IFD_offset=0x8
SUB_IFD_offset=0x86
GPSInfo_offset=0xbc
data_offset=0xbc
 */	
//	printf("\n\n1 - exif_head:\n"); print_r($exif_head);
//	printf("\n\n1 - exif_data\n");  print_r($exif_data);
//	printf("\n\n1 - tiff_data\n");  print_r($tiff_data);
	
	
	
	/// Now modify variable fields substituting values. Has to be separate, as it uses addresses calculated from remaining tags ($SUB_IFD_offset_*,$GPSInfo_offset_*
	// for ---- Exif ----
	foreach ($xml_exif->Image->children()   as $entry) substitute_value($entry, $SUB_IFD_offset_exif, $GPSInfo_offset_exif);
	foreach ($xml_exif->Photo->children()   as $entry) substitute_value($entry, $SUB_IFD_offset_exif, $GPSInfo_offset_exif);
	if ($xml_exif->GPSInfo) {
	    foreach ($xml_exif->GPSInfo->children() as $entry) substitute_value($entry, $SUB_IFD_offset_exif, $GPSInfo_offset_exif);
	}
	$ifd_pointer_exif=$IFD_offset_exif;
	$data_pointer_exif=$data_offset_exif;
//	printf("ifd_pointer_exif= % d, data_pointer_exif = %d, count(xml_exif->Image->children())=%d\n",$ifd_pointer_exif, $data_pointer_exif, count($xml_exif->Image->children()));
	
	start_ifd(count($xml_exif->Image->children()), $exif_data, $ifd_pointer_exif);
	
//	printf("ifd_pointer_exif= % d, data_pointer_exif = %d\n",$ifd_pointer_exif, $data_pointer_exif);
//	printf("\n\n0 - exif_data\n");  print_r($exif_data);
	
	foreach ($xml_exif->Image->children() as $entry) process_ifd_entry($entry, 0, $exif_data, $ifd_pointer_exif, $data_pointer_exif, $SUB_IFD_offset_exif, $GPSInfo_offset_exif);
	finish_ifd($exif_data, $ifd_pointer_exif);

	$ifd_pointer_exif=$SUB_IFD_offset_exif;
	start_ifd(count($xml_exif->Photo->children()), $exif_data, $ifd_pointer_exif);
	foreach ($xml_exif->Photo->children() as $entry) process_ifd_entry($entry, 1, $exif_data, $ifd_pointer_exif, $data_pointer_exif, $SUB_IFD_offset_exif, $GPSInfo_offset_exif);
	finish_ifd($exif_data, $ifd_pointer_exif);

	if ($xml_exif->GPSInfo) {
		$ifd_pointer_exif=$GPSInfo_offset_exif;
		start_ifd(count($xml_exif->GPSInfo->children()), $exif_data, $ifd_pointer_exif);
		foreach ($xml_exif->GPSInfo->children() as $entry) process_ifd_entry($entry, 2, $exif_data, $ifd_pointer_exif, $data_pointer_exif, $SUB_IFD_offset_exif, $GPSInfo_offset_exif);
		finish_ifd($exif_data, $ifd_pointer_exif);
	}
	$exif_len=count($exif_head)+count($exif_data)-$Exif_length_offset;
//	printf("\nexif_len = %d, Exif_length_offset = %d\n",$exif_len, $Exif_length_offset);
	$exif_head[$Exif_length_offset]=  ($exif_len >> 8) & 0xff;
	$exif_head[$Exif_length_offset+1]= $exif_len       & 0xff;
	
//	printf("\n\n2 - exif_head:\n"); print_r($exif_head);
//	printf("\n\n2 - exif_data\n");  print_r($exif_data);
	
	// ---- for Tiff ---- Need to catch $STRIPOFFSETS
	
	foreach ($xml_tiff->Image->children()   as $entry) substitute_value($entry, $SUB_IFD_offset_tiff, $GPSInfo_offset_tiff);
	foreach ($xml_tiff->Photo->children()   as $entry) substitute_value($entry, $SUB_IFD_offset_tiff, $GPSInfo_offset_tiff);
	if ($xml_tiff->GPSInfo) {
	    foreach ($xml_tiff->GPSInfo->children() as $entry) substitute_value($entry, $SUB_IFD_offset_tiff, $GPSInfo_offset_tiff);
	}
	$ifd_pointer_tiff=$IFD_offset_tiff;
	$data_pointer_tiff=$data_offset_tiff;
	
	start_ifd(count($xml_tiff->Image->children()), $tiff_data, $ifd_pointer_tiff);
	foreach ($xml_tiff->Image->children() as $entry) {
	    if ($entry->getName() == "StripOffsets") {
	        $StripOffsets_ifd_pointer =         $ifd_pointer_tiff; // put STRIPOFFSET long here when known
	        $StripOffsets_data_pointer =        $data_pointer_tiff; // put STRIPOFFSET long here when known
	        $StripOffsets_entry =               $entry;
	    }
	    process_ifd_entry($entry, 0, $tiff_data, $ifd_pointer_tiff, $data_pointer_tiff, $SUB_IFD_offset_tiff, $GPSInfo_offset_tiff);
	}
	finish_ifd($tiff_data, $ifd_pointer_tiff);
	
	$ifd_pointer_tiff=$SUB_IFD_offset_tiff;
	start_ifd(count($xml_tiff->Photo->children()), $tiff_data, $ifd_pointer_tiff);
	foreach ($xml_tiff->Photo->children() as $entry) process_ifd_entry($entry, 1, $tiff_data, $ifd_pointer_tiff, $data_pointer_tiff, $SUB_IFD_offset_tiff, $GPSInfo_offset_tiff);
	finish_ifd($tiff_data, $ifd_pointer_tiff);

	
	if ($xml_tiff->GPSInfo) {
	    $ifd_pointer_tiff=$GPSInfo_offset_tiff;
	    start_ifd(count($xml_tiff->GPSInfo->children()), $tiff_data, $ifd_pointer_tiff);
	    foreach ($xml_tiff->GPSInfo->children() as $entry) process_ifd_entry($entry, 2, $tiff_data, $ifd_pointer_tiff, $data_pointer_tiff, $SUB_IFD_offset_tiff, $GPSInfo_offset_tiff);
	    finish_ifd($tiff_data, $ifd_pointer_tiff);
	}
	
//	printf("\n\nbefore STRIPOFFSETS - tiff_data\n");  print_r($tiff_data);
//	printf("StripOffsets_ifd_pointer= 0x%x (%d)\n", $StripOffsets_ifd_pointer, $StripOffsets_ifd_pointer);
//	printf("StripOffsets_data_pointer=0x%x (%d)\n", $StripOffsets_data_pointer, $StripOffsets_data_pointer);
//	printf("count(tiff_data)=0x%x (%d)\n", count($tiff_data), count($tiff_data));
	if ($StripOffsets_ifd_pointer) {
//    	echo "StripOffsets_entry:";
//    	echo $StripOffsets_entry->getName();
    	$StripOffsets_entry->addChild('value', count($tiff_data));
    	// re-process
//    	process_ifd_entry($StripOffsets_entry, 0, $tiff_data, $StripOffsets_ifd_pointer, $StripOffsets_data_pointer, $SUB_IFD_offset_tiff, $GPSInfo_offset_tiff, 1);
    	process_ifd_entry($StripOffsets_entry, 0, $tiff_data, $StripOffsets_ifd_pointer, $StripOffsets_data_pointer, $SUB_IFD_offset_tiff, $GPSInfo_offset_tiff);
	}
	$Exif_str="";
	for ($i=0; $i<count($exif_head);$i++) $Exif_str.= chr ($exif_head[$i]);
	for ($i=0; $i<count($exif_data);$i++) $Exif_str.= chr ($exif_data[$i]);
//	printf("\ - Exif_str\n");  hexdump($Exif_str);

	$Tiff_str="";
	for ($i=0; $i<count($tiff_data);$i++) $Tiff_str.= chr ($tiff_data[$i]);
//	printf("\ - Tiff_str\n");  hexdump($Tiff_str);


	$Exif_file  = fopen($ExifDeviceTemplateFilename, 'w');
	fwrite ($Exif_file,$Exif_str); /// will disable and invalidate Exif data
	fclose($Exif_file);

	$Tiff_file  = fopen($TiffDeviceTemplateFilename, 'w');
	fwrite ($Tiff_file,$Tiff_str); /// will disable and invalidate Exif data
	fclose($Tiff_file);

	$dir_entries_exif=array();
	$exif_head_length = count($exif_head);
	foreach ($xml_exif->Image->children()   as $entry) addDirEntry($entry, $dir_entries_exif, $exif_head_length);
	foreach ($xml_exif->Photo->children()   as $entry) addDirEntry($entry, $dir_entries_exif, $exif_head_length);
	if ($xml_exif->GPSInfo) {
	    foreach ($xml_exif->GPSInfo->children() as $entry) addDirEntry($entry, $dir_entries_exif, $exif_head_length);
	}
//	printf("\ndir_entries_exif:\n"); print_r($dir_entries_exif);
	ksort ($dir_entries_exif);
//	printf("\ndir_entries_exif sorted:\n"); print_r($dir_entries_exif);
	
	$dir_entries_tiff=array();
	foreach ($xml_tiff->Image->children()   as $entry) addDirEntry($entry, $dir_entries_tiff, 0);
	foreach ($xml_tiff->Photo->children()   as $entry) addDirEntry($entry, $dir_entries_tiff, 0);
	if ($xml_tiff->GPSInfo) {
	    foreach ($xml_tiff->GPSInfo->children() as $entry) addDirEntry($entry, $dir_entries_tiff, 0);
	}
//	printf("\ndir_entries_tiff:\n"); print_r($dir_entries_tiff);
	ksort ($dir_entries_tiff);
//	printf("\ndir_entries_tiff sorted:\n"); print_r($dir_entries_tiff);
	
	// combine directories
	$dir_entries=array();
	foreach ($dir_entries_exif as $key => $value){
	    $dir_entries[$key] = array("ltag"=>$value['ltag'],"dst_exif"=>$value['dst'],"dst_tiff"=>0,"len"=>$value['len']);
	}
	foreach ($dir_entries_tiff as $key => $value){
	    if (array_key_exists($key, $dir_entries)){
	        $dir_entries[$key]["dst_tiff"] = $value['dst'];
	    } else {
	        $dir_entries[$key] = array("ltag"=>$value['ltag'],"dst_exif"=>0,"dst_tiff"=>$value['dst'],"len"=>$value['len']);
	    }
	}
	ksort ($dir_entries);
	// get rid of keys:
	$dir_entries_val=array();
	foreach ($dir_entries as $value)$dir_entries_val[] = $value;
	$dir_entries = $dir_entries_val;

	$frame_meta_size=0;
	for ($i=0;$i<count($dir_entries);$i++) {
		$dir_entries[$i]["src"]=$frame_meta_size;
		$frame_meta_size+=$dir_entries[$i]["len"];
	}
//	printf("\ndir_entries sorted:\n"); print_r($dir_entries);
	
	$Exif_str="";
	foreach ($dir_entries as $entry) $Exif_str.=pack("V*",$entry["ltag"],$entry["len"],$entry["src"],($entry["dst_exif"] +($entry["dst_tiff"] << 16)));
//	printf("\ - Exif_str (directory)\n");  hexdump($Exif_str);
	
	$Exif_meta_file  = fopen($ExifDeviceMetadirFilename, 'w');
	fwrite ($Exif_meta_file,$Exif_str); /// will disable and invalidate Exif data
	fclose($Exif_meta_file);
	
	///Rebuild buffer and enable Exif generation/output:

	$Exif_file  = fopen($ExifDeviceTemplateFilename, 'w');
	fseek ($Exif_file, EXIF_LSEEK_ENABLE, SEEK_END) ;
	fclose($Exif_file);

	if ($_SERVER['REQUEST_METHOD']) {
		echo "</pre>";
	}
	if ($_SERVER['REQUEST_METHOD']) {
		echo "<hr/>\n";
		test_print_header($exif_head, $exif_data);
		echo "<hr/>\n";
		$tiff_head=array(); // empty
		test_print_header($tiff_head, $tiff_data);
		echo "<hr/>\n";
		test_print_directory($dir_entries,$frame_meta_size);
	}
	
} //if ($init) // configure Exif data
/// ===== processing optional parameters =======
/// ===== read template =======
if ($_GET["description"]!==NULL) {

	/// Read metadir to find the length of the description field

	$Exif_file  = fopen($ExifDeviceMetadirFilename, 'r');
	fseek ($Exif_file, 0, SEEK_END) ;
	fseek ($Exif_file, 0, SEEK_SET) ;
	$metadir=fread ($Exif_file, 4096);
	fclose($Exif_file);
	$dir_entries=array();
	for ($i=0; $i<strlen($metadir);$i+=16) {
		$dir_entries[]=unpack("V*",substr($metadir,$i,16));
	}
	foreach ($dir_entries as $entry) 
		if ($entry[1]==0x010e) {
			$descr=$_GET["description"];
			$Exif_file  = fopen($ExifDeviceMetaFilename, 'w+');
			fseek ($Exif_file, $entry[3], SEEK_SET) ;
			$descr_was=fread ($Exif_file, $entry[2]);
			$zero=strpos($descr_was,chr(0));
			if ($zero!==false) $descr_was=substr($descr_was,0, $zero);
			if ($descr) {
				$descr= str_pad($descr, $entry[2], chr(0));
				fseek ($Exif_file, $entry[3], SEEK_SET) ;
				fwrite($Exif_file, $descr,$entry[2]);
			}
			fclose($Exif_file);
			var_dump($descr_was); echo "<br/>\n";
			break;
		}
}
/// ===== read template =======
if ($_GET["template"]!==NULL) {
	$Exif_file  = fopen($ExifDeviceTemplateFilename, 'r');
	fseek ($Exif_file, 0, SEEK_END) ;
	echo "<hr/>\n";
	echo "ftell()=".ftell($Exif_file).", ";
	fseek ($Exif_file, 0, SEEK_SET) ;
	$template=fread ($Exif_file, 4096);
	fclose($Exif_file);
	echo "read ".strlen($template)." bytes<br/>\n";
	hexdump($template);
}
if ($_GET["template_tiff"]!==NULL) {
    $Exif_file  = fopen($TiffDeviceTemplateFilename, 'r');
    fseek ($Exif_file, 0, SEEK_END) ;
    echo "<hr/>\n";
    echo "ftell()=".ftell($Exif_file).", ";
    fseek ($Exif_file, 0, SEEK_SET) ;
    $template=fread ($Exif_file, 4096);
    fclose($Exif_file);
    echo "read ".strlen($template)." bytes<br/>\n";
    hexdump($template);
}


/// ===== read meta directory =======
if ($_GET["metadir"]!==NULL) {
	$Exif_file  = fopen($ExifDeviceMetadirFilename, 'r');
	fseek ($Exif_file, 0, SEEK_END) ;
	echo "<hr/>\n";
	echo "ftell()=".ftell($Exif_file).", ";
	fseek ($Exif_file, 0, SEEK_SET) ;
	$metadir=fread ($Exif_file, 4096);
	fclose($Exif_file);
	echo "read ".strlen($metadir)." bytes<br/>\n";
	$dir_entries=array();
	for ($i=0; $i<strlen($metadir);$i+=16) {
		$dir_entries[]=unpack("V*",substr($metadir,$i,16));
	}
	print_directory($dir_entries);
}
/// ===== read one of the Exif pages (0 - current, 1..512 - buffer) =======
if ($_GET["exif"]!==NULL) {
	$frame=$_GET["exif"]+0;
	echo "<hr/>\n";
	printf ("Reading frame %d, ",$frame);
	$Exif_file  = fopen($ExifDeviceExifFilename, 'r');
	fseek ($Exif_file, 1, SEEK_END) ;
	$exif_size=ftell($Exif_file);
	if ($frame) fseek ($Exif_file, $frame, SEEK_END) ;
	else        fseek ($Exif_file, 0, SEEK_SET) ;
	echo "ftell()=".ftell($Exif_file).", ";
	$exif_data=fread ($Exif_file, $exif_size);
	fclose($Exif_file);
	echo "read ".strlen($exif_data)." bytes<br/>\n";
	hexdump($exif_data);
}
if ($_GET["tiff"]!==NULL) {
    $frame=$_GET["tiff"]+0;
    echo "<hr/>\n";
    printf ("Reading frame %d from %s, ",$frame,$ExifDeviceTiffFilename);
    $Tiff_file  = fopen($TiffDeviceTiffFilename, 'r');
    fseek ($Tiff_file, 1, SEEK_END) ;
    $tiff_size=ftell($Tiff_file);
    if ($frame) fseek ($Tiff_file, $frame, SEEK_END) ;
    else        fseek ($Tiff_file, 0, SEEK_SET) ;
    echo "ftell()=".ftell($Tiff_file).", ";
    $tiff_data=fread ($Tiff_file, $tiff_size);
    fclose($Tiff_file);
    echo "read ".strlen($tiff_data)." bytes<br/>\n";
    hexdump($tiff_data);
}
exit(0);
/// ======================================= Functions ======================================
function hexdump(&$data) {
//	global $exif_head, $exif_data;
	$l=strlen($data);
	printf ("<h2>Data size=%d bytes</h2>\n",$l);
	printf ("<table border=\"0\">\n");
	for ($i=0; $i<$l;$i=$i+16) {
		printf("<tr><td>%03x</td><td>|</td>\n",$i);
		for ($j=$i; $j<$i+16;$j++) {
			printf("<td>");
			if ($j<$l) {
				$d=ord($data[$j]);
				printf(" %02x",$d);
			} else printf ("   ");
			printf("</td>");
		}
		printf("<td>|</td>");
		for ($j=$i; $j< ($i+16);$j++) {
			printf("<td>");
			if ($j<$l) {
				$d=ord($data[$j]);
				if ($d<32 or $d>126) printf(".");
				else printf ("%c",$d);
			} else printf (" ");
			printf("</td>");

		}
		printf("</tr>\n");
	}
	printf ("</table>");
}

function print_directory($dir_entries) {
	$meta_size=0;
	foreach ($dir_entries as $entry)  if (($entry[3]+$entry[2])>$meta_size) $meta_size=$entry[3]+$entry[2];
	printf ("<h2>Frame meta data size=%d bytes</h2>\n",$meta_size);
	printf ("<table border=\"1\">\n");
	printf ("<tr><th>ltag</th><th>meta offset</th><th>Exif offset</th><th>Tiff offset</th><th>length</th></tr>\n");
	foreach ($dir_entries as $entry) {
	    $exif_offset = $entry[4] & 0xfff;
	    $tiff_offset = $entry[4] >> 16;
	    printf ("<tr><td>0x%x</td><td>0x%x</td><td>0x%x</td><td>0x%x</td><td>0x%x</td></tr>\n",$entry[1],$entry[3],$exif_offset,$tiff_offset,$entry[2]);
	}
	printf ("</table>");
}

function test_print_header(&$exif_head, &$exif_data) {
//	global $exif_head, $exif_data;
    $lh = 0;
    if ($exif_head) $lh=count($exif_head);
	$ld=count($exif_data);
	printf ("<h2>Exif/Tiff size=%d bytes (head=%d, data=%d)</h2>\n",$lh+$ld,$lh,$ld);
	printf ("<table border=\"0\">\n");
	for ($i=0; $i<$lh+$ld;$i=$i+16) {
		printf("<tr><td>%03x</td><td>|</td>\n",$i);
		for ($j=$i; $j<$i+16;$j++) {
			printf("<td>");
			if ($j<($lh+$ld)) {
				$d=($j<$lh)?$exif_head[$j]:$exif_data[$j-$lh];
				printf(" %02x",$d);
			} else printf ("   ");
			printf("</td>");
		}
		printf("<td>|</td>");
		for ($j=$i; $j< ($i+16);$j++) {
			printf("<td>");
			if ($j<($lh+$ld)) {
				$d=($j<$lh)?$exif_head[$j]:$exif_data[$j-$lh];
				if ($d<32 or $d>126) printf(".");
				else printf ("%c",$d);
			} else printf (" ");
			printf("</td>");

		}
		printf("</tr>\n");
	}
	printf ("</table>");
}

function test_print_directory(&$dir_entries, &$frame_meta_size) {
//	global $dir_entries,$frame_meta_size;
	printf ("<h2>Frame meta data size=%d bytes</h2>\n", $frame_meta_size);
	printf ("<table border=\"1\">\n");
	printf ("<tr><th>ltag</th><th>meta offset</th><th>Exif offset</th><th>Tiff offset</th><th>length</th></tr>\n");
	foreach ($dir_entries as $entry)
	    printf ("<tr><td>0x%x</td><td>0x%x</td><td>0x%x</td><td>0x%x</td><td>0x%x</td></tr>\n",$entry["ltag"],$entry["src"],$entry["dst_exif"],$entry["dst_tiff"],$entry["len"]);
	printf ("</table>");
}




function start_ifd($count, &$exif_data, &$ifd_pointer) {
//	global $exif_data, $ifd_pointer;
	// printf("start_ifd: ifd_pointer=0x%04x \n", $ifd_pointer);
	$exif_data[$ifd_pointer++]= ($count >> 8) & 0xff;
	$exif_data[$ifd_pointer++]= $count & 0xff; // may apply & 0xff in the end to all elements
	
//	printf("++++start_ifd(): ifd_pointer= %d, exif_data=\n",$ifd_pointer); print_r($exif_data);
	
}
function finish_ifd(&$exif_data, &$ifd_pointer) { // we do not have additional IFDs
//	global $exif_data, $ifd_pointer;
	// printf("finish_ifd: ifd_pointer=0x%04x \n", $ifd_pointer);
	$exif_data[$ifd_pointer++]=0;
	$exif_data[$ifd_pointer++]=0;
	$exif_data[$ifd_pointer++]=0;
	$exif_data[$ifd_pointer++]=0;
}


//pass2 - building map from frame meta to Exif template
function addDirEntry0($ifd_entry, &$dir_sequence, &$dir_entries, $lh) {
//	global $dir_sequence,$dir_entries,$exif_head;
//	$lh=count($exif_head);

	$attrs = $ifd_entry->attributes();
	//  var_dump($attrs);
	//  if  (array_key_exists  ( "seq"  , $attrs  )) {
	if  ($attrs["seq"]) {
		//     echo $attrs["seq"].;
		$dir_sequence[]=((string) $attrs["seq"])+0;
		$len= (integer) $ifd_entry->value_length;
		$offs=$lh+(integer) $ifd_entry->value_offest;
		//     if (array_key_exists  ( "dlen"  , $attrs  )) 
		if  ($attrs["dlen"])  $len=min($len,((string) $attrs["dlen"])+0);
		$dir_entries[]=array("ltag"=>((integer)$ifd_entry->ltag),"dst"=>$offs,"len"=>$len);
	}
}

function addDirEntry($ifd_entry, &$dir_entries, $lh) {
    //	global $dir_sequence,$dir_entries,$exif_head;
    //	$lh=count($exif_head);
    
    $attrs = $ifd_entry->attributes();
    //  var_dump($attrs);
    //  if  (array_key_exists  ( "seq"  , $attrs  )) {
    if  ($attrs["seq"]) {
        //     echo $attrs["seq"].;
        $key=((string) $attrs["seq"])+0;
        $len= (integer) $ifd_entry->value_length;
        $offs=$lh+(integer) $ifd_entry->value_offest;
        //     if (array_key_exists  ( "dlen"  , $attrs  ))
        if  ($attrs["dlen"])  $len=min($len,((string) $attrs["dlen"])+0);
        $dir_entries[$key]=array("ltag"=>((integer)$ifd_entry->ltag),"dst"=>$offs,"len"=>$len);
    }
}
        



function substitute_value($ifd_entry, $SUB_IFD_offset, $GPSInfo_offset) {
//	global $SUB_IFD_offset,$GPSInfo_offset;
	global $DeviceSNFilename, $DeviceRevisionFilename, $DeviceBrand, $DeviceModel; // keep these globals
	$attrs = $ifd_entry->attributes();

	switch ($attrs["function"]) {
	case "BRAND":
		$ifd_entry->addChild('value', $DeviceBrand);
		break;
	case "MODEL":
		$camera_state_path = "/var/volatile/state/camera";
		if (file_exists ($camera_state_path)) {
			$pars= parse_ini_file($camera_state_path);
			$model = $pars['application']."_".$pars['mode'];
		} else {
			$model = $DeviceModel;
		}
		$ifd_entry->addChild ('value',$model);
		break;
	case "SOFTWARE":
		/* TODO: 
		if (file_exists("/usr/html/docs/")) {
			$ifd_entry->addChild ('value',exec("ls /usr/html/docs/")); // filter
		}
		*/
		$ifd_entry->addChild ('value',"https://git.elphel.com/Elphel/elphel393");
		break;
	case "SERIAL":
		$s = "";
		if (file_exists($DeviceSNFilename)) {
			$s = exec('cat '.$DeviceSNFilename);
		}
		$ifd_entry->addChild ('value',substr($s,0,2).":".substr($s,2,2).":".substr($s,4,2).":".substr($s,6,2).":".substr($s,8,2).":".substr($s,10,2));
		break;
	case "EXIFTAG":
		$ifd_entry->addChild ('value',$SUB_IFD_offset);
		break;
	case "GPSTAG":
		$ifd_entry->addChild ('value',$GPSInfo_offset);
		break;
//	case "STRIPOFFSETS": // temporary - maybe remove this function
//	    $ifd_entry->addChild ('value',$GPSInfo_offset);
//	    break;
	    
		//STRIPOFFSETS
	}
}


function process_ifd_entry($ifd_entry, $group, &$exif_data, &$ifd_pointer, &$data_pointer, $SUB_IFD_offset, $GPSInfo_offset, $debug=0) {
//	global $exif_data, $ifd_pointer, $data_pointer,$SUB_IFD_offset,$GPSInfo_offset;
	$attrs = $ifd_entry->attributes();
	$ifd_tag=   ((string) $attrs["tag"])+0;
	$ifd_format=constant("EXIF_".$attrs["format"]);
	$ifd_count= $attrs["count"];
	if ($debug){
	    echo "\nifd_tag "; echo $ifd_tag; echo "\n";
	    echo "ifd_format "; echo $ifd_format; echo "\n";
	    echo "ifd_count "; echo $ifd_count; echo "\n";
	
	    echo "\nifd_tag=$ifd_tag, entry=";print_r($ifd_entry);
	    echo "\nifd_count=$ifd_count";
	    echo "\nifd_bytes:";var_dump($ifd_bytes);
	}
	if (!$ifd_count) {
		if($ifd_format==EXIF_ASCII) $ifd_count=strlen($ifd_entry->value)+1;
		else $ifd_count=1 ; /// may change?
	}
	//echo "\nifd_count=$ifd_count";
	$exif_data[$ifd_pointer++]= ($ifd_tag    >>  8)  & 0xff;
	$exif_data[$ifd_pointer++]=  $ifd_tag            & 0xff;
	$exif_data[$ifd_pointer++]= ($ifd_format >>  8 ) & 0xff;
	$exif_data[$ifd_pointer++]=  $ifd_format         & 0xff;
	$exif_data[$ifd_pointer++]= ($ifd_count >> 24)  & 0xff;
	$exif_data[$ifd_pointer++]= ($ifd_count >> 16)  & 0xff;
	$exif_data[$ifd_pointer++]= ($ifd_count >>  8)   & 0xff;
	$exif_data[$ifd_pointer++]=  $ifd_count          & 0xff;

	$ifd_bytes=0;
	switch ($ifd_format) {
	case EXIF_SHORT:
	case EXIF_SSHORT:    $ifd_bytes=2; break;
	case EXIF_LONG:
	case EXIF_SLONG:     $ifd_bytes=4; break;
	case EXIF_RATIONAL:
	case EXIF_SRATIONAL: $ifd_bytes=8; break;
	default: $ifd_bytes=1; //1,2,6,7
	}
	$ifd_bytes=$ifd_bytes*$ifd_count;
	// now prepare ifd_data - array of bytes
	switch ($ifd_format) {
	case EXIF_BYTE:
	case EXIF_SBYTE:
		$ifd_data= array ();
		foreach ($ifd_entry->value as $a) $ifd_data[]= $a & 0xff;
		break;
	case EXIF_ASCII:
		$ifd_data= str_split($ifd_entry->value);
		foreach($ifd_data as &$d) $d=ord($d);
		break;
	case EXIF_SHORT:
	case EXIF_SSHORT:
		$ifd_data= array ();
		foreach ($ifd_entry->value as $a) $ifd_data=array_merge($ifd_data,array(($a >> 8) & 0xff, $a & 0xff));
		break;
	case EXIF_LONG:
	case EXIF_SLONG:
		$ifd_data= array ();
		foreach ($ifd_entry->value as $a) $ifd_data=array_merge($ifd_data,array(($a >> 24) & 0xff,($a >> 16) & 0xff,($a >> 8) & 0xff, $a & 0xff));
		break;
	case EXIF_RATIONAL:
	case EXIF_SRATIONAL:
		$nom= array ();
		foreach ($ifd_entry->nominator as $a)   $nom[]=  array(($a >> 24) & 0xff,($a >> 16) & 0xff,($a >> 8) & 0xff, $a & 0xff);
		$denom= array ();
		foreach ($ifd_entry->denominator as $a) $denom[]=array(($a >> 24) & 0xff,($a >> 16) & 0xff,($a >> 8) & 0xff, $a & 0xff);
		$ifd_data= array ();
/*
var_dump($nom);
echo "\n";
var_dump($denom);
echo "\n";
//exit(0);
 */
		for ($i=0;$i<count($nom);$i++) {
			//            echo "i=$i\n";
			//          echo "\nnom=";var_dump($nom[$i]);
			//        echo "\ndenom=";var_dump($denom[$i]);
			$ifd_data=array_merge($ifd_data,$nom[$i],$denom[$i]);
		}
		break; // rational, (un)signed

	case EXIF_UNDEFINED: // undefined
		$ifd_data= array_fill(0,$ifd_bytes,0); // will just fill with "0"-s
		break; 
	}
	// echo "\nifd_tag=$ifd_tag, entry=";print_r($i=$ifd_entry->value);
	// echo "\nifd_data:";var_dump($ifd_data);
	// echo "\nifd_bytes:";var_dump($ifd_bytes);

	$ifd_data=array_pad($ifd_data,$ifd_bytes,0); 

	$ifd_entry->addChild ('value_length',count($ifd_data));
	// if  (array_key_exists  ( "ltag"  , $attrs  )) $ltag= ((string) $attrs["ltag"])+0;
	if  ($attr["ltag"]) $ltag= ((string) $attrs["ltag"])+0;
	else  $ltag= $ifd_tag+($group<<16) ;
	$ifd_entry->addChild ("ltag",$ltag );
	if (count($ifd_data) <=4) {
		$ifd_entry->addChild ('value_offest',$ifd_pointer);
		$ifd_data= array_pad($ifd_data,-4,0); // add leading zeroes if <4 bytes
		for ($i=0;$i<4;$i++)   $exif_data[$ifd_pointer++]=$ifd_data[$i];
	} else { //pointer, not data
		$ifd_entry->addChild ('value_offest',$data_pointer);
		$exif_data[$ifd_pointer++]= ($data_pointer >> 24)  & 0xff;
		$exif_data[$ifd_pointer++]= ($data_pointer >> 16)  & 0xff;
		$exif_data[$ifd_pointer++]= ($data_pointer >>  8)  & 0xff;
		$exif_data[$ifd_pointer++]=  $data_pointer         & 0xff;
		for ($i=0;$i<count($ifd_data);$i++)  {
			$exif_data[$data_pointer++]=$ifd_data[$i];
		}
	}
}

?> 
