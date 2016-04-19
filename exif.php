#!/usr/local/sbin/php -q
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
*!
*! Copyright (C) 2007-2008 Elphel, Inc
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

$ExifDeviceTemplateFilename="/dev/exif_template";
$ExifDeviceMetadirFilename= "/dev/exif_metadir";
$ExifDeviceExifFilename=    "/dev/exif_exif";
$ExifDeviceMetaFilename=    "/dev/exif_meta";


//! if called from the command line - accepts just one parameter - configuration file,
//! through CGI - accepts more parameters

$ExifXMLName="/etc/Exif_template.xml";
$init=false;
if ($_SERVER['REQUEST_METHOD']=="GET") {
  if ($_GET["init"]!==NULL) {
    if ($_GET["init"]) $ExifXMLName=$_GET["init"];
    $init=true; // in any case - filename specified or not
    $noGPS=     ($_GET["noGPS"]!==NULL);
    $nocompass= ($_GET["nocompass"]!==NULL);
  }
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
          0xff, 0xe1, // APP1 marker
// offset=2 - start of Exif header
          0x00, 0x00, // - length of the Exif header (including this length bytes) - we'll fill it later, we do not know it yet
          0x45,0x78,0x69,0x66,0x00,0x00); // Exif header
   $Exif_length_offset= 2; // put total length here (big endian, 2 bytes)

   $exif_data= array (// start of TIFF Header, data offsets will match indexes in this array
          0x4d,0x4d, // (MM) Big endian, MSB goes first in multi-byte data
          0x00,0x2a,  // Tag Mark
          0x00,0x00,0x00,0x08); //offset to first IDF (from the beginning of the TIFF header, so 8 is minimum)
   $xml_exif = simplexml_load_file($ExifXMLName);
   if ($xml_exif->GPSInfo) {
/// remove all tags named "Compass..."
     if ($nocompass) {
        $tounset=array();
        foreach ($xml_exif->GPSInfo->children() as $entry) if (strpos  ($entry->getName()  , "Compass" )!==false) $tounset[]=$entry->getName();
        foreach ($tounset as $entry) unset ($xml_exif->GPSInfo->{$entry});
     }
     if ($noGPS) {
       unset($xml_exif->GPSInfo);
       unset($xml_exif->Image->GPSTag);
     }
   }

   $IFD_offset=      count($exif_data);
   $SUB_IFD_offset=  12*count($xml_exif->Image->children())+2+4+$IFD_offset;
   $GPSInfo_offset=  12*count($xml_exif->Photo->children())+2+4+$SUB_IFD_offset;
   $data_offset=     $GPSInfo_offset+(($xml_exif->GPSInfo)?(12*count($xml_exif->GPSInfo->children())+2+4):0); //$GPSInfo is optional
   if ($_SERVER['REQUEST_METHOD']) {
      echo "<pre>";
      printf ("IFD_offset=0x%x\n",$IFD_offset);
      printf ("SUB_IFD_offset=0x%x\n",$SUB_IFD_offset);
      printf ("GPSInfo_offset=0x%x\n",$GPSInfo_offset);
      printf ("data_offset=0x%x\n",$data_offset);
   }

/// Now modify variable fields substituting values
   foreach ($xml_exif->Image->children()   as $entry) substitute_value($entry);
   foreach ($xml_exif->Photo->children()   as $entry) substitute_value($entry);
   if ($xml_exif->GPSInfo) {
     foreach ($xml_exif->GPSInfo->children() as $entry) substitute_value($entry);
   }
   $ifd_pointer=$IFD_offset;
   $data_pointer=$data_offset;
   start_ifd(count($xml_exif->Image->children()));
   foreach ($xml_exif->Image->children() as $entry) process_ifd_entry($entry,0);
   finish_ifd();

   $ifd_pointer=$SUB_IFD_offset;
   start_ifd(count($xml_exif->Photo->children()));
   foreach ($xml_exif->Photo->children() as $entry) process_ifd_entry($entry,1);
   finish_ifd();

   if ($xml_exif->GPSInfo) {
     $ifd_pointer=$GPSInfo_offset;
     start_ifd(count($xml_exif->GPSInfo->children()));
     foreach ($xml_exif->GPSInfo->children() as $entry) process_ifd_entry($entry,2);
     finish_ifd();
   }
   $exif_len=count($exif_head)+count($exif_data)-$Exif_length_offset;
   $exif_head[$Exif_length_offset]=  ($exif_len >> 8) & 0xff;
   $exif_head[$Exif_length_offset+1]= $exif_len       & 0xff;

   $Exif_str="";
   for ($i=0; $i<count($exif_head);$i++) $Exif_str.= chr ($exif_head[$i]);
   for ($i=0; $i<count($exif_data);$i++) $Exif_str.= chr ($exif_data[$i]);

   $Exif_file  = fopen($ExifDeviceTemplateFilename, 'w');
   fwrite ($Exif_file,$Exif_str); /// will disable and invalidate Exif data
   fclose($Exif_file);

///Exif template is done, now we need a directory to map frame meta data to fields in the template.
   $dir_sequence=array();
   $dir_entries=array();

   foreach ($xml_exif->Image->children()   as $entry) addDirEntry($entry);
   foreach ($xml_exif->Photo->children()   as $entry) addDirEntry($entry);
   if ($xml_exif->GPSInfo) {
     foreach ($xml_exif->GPSInfo->children() as $entry) addDirEntry($entry);
   }
   array_multisort($dir_sequence,$dir_entries);

   $frame_meta_size=0;
   for ($i=0;$i<count($dir_entries);$i++) {
     $dir_entries[$i]["src"]=$frame_meta_size;
     $frame_meta_size+=$dir_entries[$i]["len"];
   }

   $Exif_str="";
   foreach ($dir_entries as $entry) $Exif_str.=pack("V*",$entry["ltag"],$entry["len"],$entry["src"],$entry["dst"]);
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
     test_print_header();
     echo "<hr/>\n";
     test_print_directory();
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
 exit(0);
/// ======================================= Functions ======================================
function hexdump($data) {
 global $exif_head, $exif_data;
   $l=strlen($data);
   printf ("<h2>Exif size=%d bytes</h2>\n",$l);
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
   printf ("<tr><td>ltag</td><td>meta offset</td><td>Exif offset</td><td>length</td></tr>\n");
   foreach ($dir_entries as $entry) {
     printf ("<tr><td>0x%x</td><td>0x%x</td><td>0x%x</td><td>0x%x</td></tr>\n",$entry[1],$entry[3],$entry[4],$entry[2]);
   }
   printf ("</table>");
}

function test_print_header() {
 global $exif_head, $exif_data;
    $lh=count($exif_head);
    $ld=count($exif_data);
   printf ("<h2>Exif size=%d bytes (head=%d, data=%d)</h2>\n",$lh+$ld,$lh,$ld);
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

function test_print_directory() {
  global $dir_entries,$frame_meta_size;
   printf ("<h2>Frame meta data size=%d bytes</h2>\n",$frame_meta_size);
   printf ("<table border=\"1\">\n");
   printf ("<tr><td>ltag</td><td>meta offset</td><td>Exif offset</td><td>length</td></tr>\n");
   foreach ($dir_entries as $entry)
     printf ("<tr><td>0x%x</td><td>0x%x</td><td>0x%x</td><td>0x%x</td></tr>\n",$entry["ltag"],$entry["src"],$entry["dst"],$entry["len"]);
   printf ("</table>");
}




function start_ifd($count) {
 global $exif_data, $ifd_pointer;
// printf("start_ifd: ifd_pointer=0x%04x \n", $ifd_pointer);
 $exif_data[$ifd_pointer++]= ($count >> 8) & 0xff;
 $exif_data[$ifd_pointer++]= $count & 0xff; // may apply & 0xff in the end to all elements
}
function finish_ifd() { // we do not have additional IFDs
 global $exif_data, $ifd_pointer;
// printf("finish_ifd: ifd_pointer=0x%04x \n", $ifd_pointer);
   $exif_data[$ifd_pointer++]=0;
   $exif_data[$ifd_pointer++]=0;
   $exif_data[$ifd_pointer++]=0;
   $exif_data[$ifd_pointer++]=0;
}


//pass2 - building map from frame meta to Exif template
function addDirEntry($ifd_entry) {
  global $dir_sequence,$dir_entries,$exif_head;
  $lh=count($exif_head);

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

function substitute_value($ifd_entry) {
 global $SUB_IFD_offset,$GPSInfo_offset;
 $attrs = $ifd_entry->attributes();
 
 switch ($attrs["function"]) {
   case "BRAND":
      $ifd_entry->addChild ('value',exec("bootblocktool -x BRAND"));
     break;
   case "MODEL":
     if (file_exists ('/var/state/APPLICATION')) {
       $model= file_get_contents('/var/state/APPLICATION');
       if (file_exists ('/var/state/APPLICATION_MODE')) {
       $model.=' CHN'.file_get_contents('/var/state/APPLICATION_MODE');
       }
     } else {
       $model= exec("bootblocktool -x MODEL").exec("bootblocktool -x REVISION");
    }
///      $ifd_entry->addChild ('value',exec("bootblocktool -x MODEL").exec("bootblocktool -x REVISION"));
     $ifd_entry->addChild ('value',$model);

     break;
   case "SOFTWARE":
      $ifd_entry->addChild ('value',exec("ls /usr/html/docs/")); // filter
     break;
   case "SERIAL":
      $s=exec("bootblocktool -x SERNO");
      $ifd_entry->addChild ('value',substr($s,0,2).":".substr($s,2,2).":".substr($s,4,2).":".substr($s,6,2).":".substr($s,8,2).":".substr($s,10,2));
     break;
   case "EXIFTAG":
      $ifd_entry->addChild ('value',$SUB_IFD_offset);
     break;
   case "GPSTAG":
      $ifd_entry->addChild ('value',$GPSInfo_offset);
    break;
 }
}


function process_ifd_entry($ifd_entry, $group) {
 global $exif_data, $ifd_pointer, $data_pointer,$SUB_IFD_offset,$GPSInfo_offset;
 $attrs = $ifd_entry->attributes();
 $ifd_tag=   ((string) $attrs["tag"])+0;
 $ifd_format=constant("EXIF_".$attrs["format"]);
 $ifd_count= $attrs["count"];
// echo "\nifd_tag=$ifd_tag, entry=";print_r($ifd_entry);
// echo "\nifd_count=$ifd_count";
// echo "\nifd_bytes:";var_dump($ifd_bytes);
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
