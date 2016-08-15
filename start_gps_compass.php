#!/usr/bin/env php -q
<?php
/*
*! FILE NAME  : start_gps_compass.php
*! DESCRIPTION: Looks for USB GPS (currently Garmin GPS 18 USB) and compass
*!              (currently Ocean Server OS-5000), re-initializes Exif header
*!              and starts the devices if found
*!
*! Copyright (C) 2008 Elphel, Inc
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
*!  $Log: start_gps_compass.php,v $
*!  Revision 1.6  2012/03/14 00:05:41  elphel
*!  8.2.1 - GPS speed, x353.bit file tested on Eyesis
*!
*!  Revision 1.5  2011/08/13 00:55:14  elphel
*!  support for detection 103595 and 103696 boards, programming the logger
*!
*!  Revision 1.4  2011/01/15 23:22:10  elphel
*!  added 19200
*!
*!  Revision 1.3  2009/02/20 21:49:52  elphel
*!  bugfix - was killing daemons after detection, not before (as should)
*!
*!  Revision 1.2  2009/02/19 22:38:37  elphel
*!  8.0.2.3 - added several USB serial adapters, nmea->exif support, detection of NMEA GPS at boot
*!
*!  Revision 1.1.1.1  2008/11/27 20:04:01  elphel
*!
*!
*!  Revision 1.2  2008/11/20 07:03:40  elphel
*!  exit value now encodes detected devices
*!
*!  Revision 1.1  2008/04/07 09:14:47  elphel
*!  Discover/startup GPS and compass
*!
*/
//look uptime, sleep if just started (no USB yet)
//require '/usr/html/includes/i2c.inc';
require 'i2c.inc';
    $config_name="/etc/imu_logger.xml";
    $ids=scanGrandDaughters();
    $b_index=indexGrandDaughters($ids);
//    print_r($ids);
//    print_r($b_index);
    $baud=null;
    if (isset($b_index[103696])) {
       $baud=$ids[$b_index[103696]]['baud'];
       if ($baud===0) $baud=='auto';
    }

//    exit (0);
 $wait_time=20; // wait for $wait_time if both devices were not found, retry
    $compass=null;
    $GPS=null;
    $verbose=true;
    foreach ($_SERVER['argv'] as $param) {
      if ((substr($param,0,2)=="-q") || (substr($param,0,8)=="--silent")){
        $verbose=false;
      }
    }

/// kill those GPS and compass if they are running already
    exec('killall -q garminusb2exif');
    exec('killall -q nmea2exif');
    exec('killall -q compass');
//    find_gps_compass();
    $devs=find_gps_compass($baud);
//    echo "compass: ";var_dump($compass);echo "\n";
//    echo "GPS:     ";var_dump($GPS);echo "\n";
//   if ((!$compass || !$GPS) && ((exec('date +%s')+0) <30)) {
   if ((count($devs)==0) && ((exec('date +%s')+0) <30)) {
          if ($verbose) echo "waiting $wait_time sec for USB devices to start up\n";
          sleep($wait_time);
          if ($verbose) echo "Retrying detection\n";
          $devs=find_gps_compass($baud);
    }
    listSensorDevices($devs,$b_index,$ids);
//var_dump($GPS);
//  if (file_exists ($sensor_state_file)) $sensor_board=parse_ini_file($sensor_state_file);
//    exec ('sync');
    $logger_config=null;
    $write_config=false;
    if (file_exists ($config_name)) {
      if ($verbose) echo 'Reading IMU logger configuration from '. $config_name."\n";
      $config_xml = simplexml_load_file($config_name);
      $logger_config=loggerConfigFromXML($config_xml);
    } else  if (isset($b_index[103696]) || isset($b_index[103696])) {
      if ($verbose) echo "Generating IMU logger configuration\n";
//      $default_config=init_default_config();
//      $logger_config=init_default_config();
      $logger_config=array();
      if (isset($GPS) && isset($b_index[103696])) {
        $logger_config['baud_rate']=$GPS['baud'];
        $logger_config['gps_slot']=$ids[$b_index[103696]]['port'];
        $logger_config['gps_mode']=$ids[$b_index[103696]]['sync'];
      }
      if (isset($b_index[103695])) {
        $logger_config['imu_slot']=$ids[$b_index[103695]]['port'];
        $logger_config['imu_sa']=$b_index[103695];
      }
      $write_config=true; // write configuration to /etc after applying defaults
    }
    if (isset($logger_config)){
//   var_dump($logger_config);
    $logger_config=combineLoggerConfigs($logger_config,init_default_config());
//   var_dump($logger_config);
//   var_dump($default_config);
      $logger_xml=loggerConfigToXML($logger_config);
      $conf_file=fopen('/var/html/logger_config.xml',"w");
      fwrite($conf_file,$logger_xml->asXML());
      fclose($conf_file);
      setup_IMU_logger($logger_config);
      if ($write_config) {
        if ($verbose) echo 'Writing IMU logger configuration to '. $config_name."\n";
        $conf_file=fopen($config_name,"w");
        fwrite($conf_file,$logger_xml->asXML());
        fclose($conf_file);
        exec ('sync');
      }
    }

//    echo "compass: ";var_dump($compass);echo "\n";
//    echo "GPS:     ";var_dump($GPS);echo "\n";
    $noGPS=    ($compass || $GPS)?"" : "noGPS"; 
    $nocompass=($compass)?"" : "nocompass"; 
    $cmd="/usr/html/exif.php init $noGPS $nocompass";
    if ($verbose) echo "Initializing Exif template: $cmd\n";
    exec($cmd);
    if ($GPS) {
      if ($verbose) echo "Starting ".$GPS["name"]. " as ". $GPS["file"]. "\n";
      if (strpos($GPS['name'],'NMEA')!==false) {
        $cmd ="/usr/local/bin/nmea2exif ". $GPS["file"]." &";
      } else {
        $cmd ="/usr/local/bin/garminusb2exif ". $GPS["file"]." &";
      }
      if ($verbose) echo "exec: $cmd \n";
      popen($cmd,"r");
    }
    if ($compass) {
      if ($verbose) echo "Starting ".$compass["name"]. " as ". $compass["file"]. "\n";
      exec ("stty -F ".$compass["file"]." -echo speed 19200");
      $cmd="/usr/local/bin/compass ". $compass["file"]." &";
      if ($verbose) echo "exec: $cmd \n";
      popen($cmd,"r");
    }
    exit ((isset($b_index[103695])?4:0) | ($compass?2:0) | ($GPS?1:0));
function scanGrandDaughters(){
  $ids=array();
  $id="";
  for ($i=2;$i<8;$i++) {
    $id=i2c_read256b(0xa0+2*$i,1);
    $xml=simplexml_load_string($id);
    if (($xml!==false) && ($xml->getName()=='board')) {
        $ids[$i]=array();
        foreach ($xml->children() as $entry) {
          $ids[$i][$entry->getName()]=(string) $entry ;
        }
    }
  }
  return $ids;
}
function indexGrandDaughters($ids) {
  $index=array();
  foreach($ids as $i=>$id) if (isset($id['model'])) {
    $index[$id['model']]=$i;  
  }
  return $index;
}
/// compass/GPS programs should be killed before detection
// baud=null - search USB Garmin and compass
// baud=auto - try to autodetect baud rate
// baud=number - used specified baud rate, do not wait for the GPS response
function find_gps_compass($baud=null,$timeout=5) { 
    global $verbose;
    $ttyOptions=array (
      '-echo speed 115200',
      '-echo speed 57600',
      '-echo speed 56000',
      '-echo speed 38400',
      '-echo speed 19200',  /// maybe other options, like '-echo speed 4800 -ixoff'
      '-echo speed 9600',
      '-echo speed 4800',
      '-echo speed 2400'
      );
    if (isset($baud) && ($baud!='auto') && ($baud!=0)) {
      $ttyOptions=array (
       '-echo speed '.$baud);
    }
//    global $GPS, $compass;
    $devices=array(
              "compass"=>array(array("name"=>"Ocean Server OS-5000","driver"=>"cp2101")),
              "GPS"    =>array(array("name"=>"Garmin GPS 18 USB","driver"=>"garmin_gps")));
    exec("ls /sys/bus/usb-serial/devices",$usb_ser_devs); // ("ttyUSB0","ttyUSB1")
    $devs = new SimpleXMLElement("<?xml version='1.0' standalone='yes'?><USB_serial_devices/>");
    $serialDevices=array();
    $compassPresent=false;
    $gpsPresent=false;
    $driver=array();
//echo "usb_ser_devs"; var_dump($usb_ser_devs);
    foreach ($usb_ser_devs as $dev) {
//echo "dev=$dev\n";
      $arr=split("/",exec("ls /sys/bus/usb-serial/devices/".$dev."/driver -l"));
      $driver[$dev]=$arr[count($arr)-1];
      $serialDevices[$dev]='';
      if (!isset($baud)) { // if baud is set (auto or number - skip compass, it is 103696)
        foreach ($devices["compass"] as $d) {
          if ($d["driver"]==$driver[$dev]) {
            $dd=$devs->addChild ('compass');
            $dd->addChild ('name',$d["name"]);
            $dd->addChild ('driver',$d["driver"]);
            $dd->addChild ('file',"/dev/".$dev);
            $serialDevices[$dev]='compass';
            $compassPresent=true;
          }
        }
      }
      if ($serialDevices[$dev]) continue;
      foreach ($devices["GPS"] as $d) {
        if ($d["driver"]==$driver[$dev]) {
          $dd=$devs->addChild ('GPS');
          $dd->addChild ('name',$d["name"]);
          $dd->addChild ('driver',$d["driver"]);
          $dd->addChild ('file',"/dev/".$dev);
          $serialDevices[$dev]='gps';
          $gpsPresent=true;
        }
      }
    }
/// Do we need to look for a NMEA GPS?
    if (!$gpsPresent) foreach ($serialDevices as $dev=>$type) if (!$type && !$gpsPresent) {
      if ($verbose) printf ("could be $dev\n");
      foreach ($ttyOptions as $ttyOpt) {
        $cmd='stty -F /dev/'.$dev.' '.$ttyOpt;
        if (count($ttyOptions)==1) { /// single baud option, will not wait for confirmation. Has to be only one serial adapter !!
          if ($verbose) echo "Setting:  $cmd\n";
          exec ($cmd);
          $gpsPresent=true;
        } else {
          if ($verbose) echo "Trying:  $cmd\n";
          exec ($cmd);
          unset ($fullOutput);
          exec ('timeout '.$timeout.' cat /dev/'.$dev, &$fullOutput);
//var_dump($fullOutput);
          foreach ($fullOutput as $line) if (strpos ($line, '$GP')===0) {
            $gpsPresent=true;
            break;
          }
        }
        if ($gpsPresent) {
          $dd=$devs->addChild ('GPS');
          $dd->addChild ('name','NMEA 0183 GPS receiver');
          $dd->addChild ('driver',$driver[$dev]);
          $dd->addChild ('file',"/dev/".$dev);
          $arr=split(" ",$ttyOpt);
          $dd->addChild ('baud',$arr[count($arr)-1]);
          $serialDevices[$dev]='gps';
          if ($verbose) echo "\nfound NMEA GPS unit\n";
          break;
        }
// var_dump($fullOutput);
      }
    }
/// list unused USB serial adapters
    foreach ($serialDevices as $dev=>$type) if (!$type) {
          $dd=$devs->addChild ('unused');
          $dd->addChild ('name','USB serial converter');
          $dd->addChild ('driver',$driver[$dev]);
          $dd->addChild ('file',"/dev/".$dev);
    }
    return $devs;

}
function listSensorDevices($devs,$b_index,$ids) {
    global $GPS, $compass;

    foreach ($devs->compass as $a) {
      $compass=array("file"=>(string) $a->file, "name"=> (string) $a->name);
      break; // use first one
    }
    foreach ($devs->GPS as $a) {
      $GPS=array("file"=>(string) $a->file, "name"=> (string) $a->name, "baud"=>(string) $a->baud);
      if (isset($b_index[103696])) {
        $a->addChild ('part_number',$ids[$b_index[103696]]['part']);
        $a->addChild ('port',$ids[$b_index[103696]]['port']);
        $a->addChild ('mode',$ids[$b_index[103696]]['mode']);
      }

      break; // use first one
    }
    $state_file=fopen("/var/html/gps_compass.xml","w");
    fwrite($state_file,$devs->asXML());
    fclose($state_file);
}
function loggerConfigToXML($conf){
    $logger_xml = new SimpleXMLElement("<?xml version='1.0' standalone='yes'?><Logger_configuration/>");
    foreach ($conf as $key=>$value) {
      switch ($key) {
        case 'imu_registers':
          $subtree=$logger_xml->addChild ($key);
           for ($i=0;$i<count($value);$i++) {
              $subtree->addChild ('R_'.sprintf('%02d',$i),sprintf("0x%02x",$value[$i]));
           }
         break;
        case 'nmea':
          $subtree=$logger_xml->addChild ($key);
           for ($i=0;$i<count($value);$i++) {
              $sentence=$subtree->addChild ('S_'.sprintf('%01d',$i));
              $sentence->addChild ('sentence',$value[$i][0]);
              $sentence->addChild ('format',$value[$i][1]);
           }
         break;
        default:
         $logger_xml->addChild ($key,$value);
      }
    }
    return $logger_xml;
}
function loggerConfigFromXML($confXML){
    $conf=array();
//    var_dump($confXML);
    foreach ($confXML as $item) {
//    var_dump($item);
      $key=$item->getName();
      switch ($key) {
        case 'imu_registers':
          $conf[$key]=array();
          foreach ($item as $reg) {
            $rname=substr($reg->getName(),2);
            $conf[$key][$rname+0]=((string) $reg)+0;
          }
         break;
        case 'nmea':
          $conf[$key]=array();
          foreach ($item as $reg) {
            $rname=substr($reg->getName(),2);
            $conf[$key][$rname+0]=array((string) $reg->sentence, (string) $reg->format);
          }
         break;
        case 'imu_period':
         $conf[$key]=(string) $item;
         if ($conf[$key]!='auto') $conf[$key]+=0;
         break;
        case 'message':
         $conf[$key]=(string) $item;
         break;
        default:
//         $conf[$key]=(int) $item;
         $conf[$key]=((string) $item)+0;
      }
    }
    return $conf;
}



function init_default_config(){
  $default_config=array(
  'imu_period'=>'auto',//  0xFFFFFFFF, // 0 - off, >=0xff000000 - "when ready", else - number of SCLK periods
  'sclk_freq'=>5000000, //  $clkr_divr=  8, // 80MHz divisor to get half SCLK rate (defaulkt SCLK=5MHz)
  'stall' => 2, // SPI stall time in usec
  'baud_rate'=>19200, //    $rs232_div=80/2/$baud
  'imu_slot'=>1,
  'gps_slot'=>2,
  'gps_mode'=>2, // 0 - pps pos, 1 - pps neg, 2 - start of first sesntence after pause, 3 start of sentence
  'msg_conf'=>10, // GPIO bit number for external (odometer) input (+16 - invert polarity). Timestamp uses leading edge, software may write to 56-byte buffer before the trailing edge
  'img_sync'=>1, // enable logging image acquisition starts (0 - disable)
  'extra_conf'=>4, // 0 - config_long_sda_en, 1 -config_late_clk, 2 - config_single_wire, should be set for 103695 rev "A"
  'slow_spi'=>0, // set to 1 for slow SPI devices (0 for  ADIS-16375)
  'imu_sa'=>3, // i2c slave address modifier for the 103695A pca9500
  'sleep_busy'=>30000, //microseconds
  'imu_registers'=>array( /// Up to 28 total
    0x10, // x gyro low
    0x12, // x gyro high
    0x14, // y gyro low
    0x16, // y gyro high
    0x18, // z gyro low
    0x1a, // z gyro high

    0x1c, // x accel low
    0x1e, // x accel high
    0x20, // y accel low
    0x22, // y accel high
    0x24, // z accel low
    0x26, // z accel high

    0x40, // x delta angle low
    0x42, // x delta angle high
    0x44, // y delta angle low
    0x46, // y delta angle high
    0x48, // z delta angle low
    0x4a, // z delta angle high

    0x4c, // x delta velocity low
    0x4e, // x delta velocity high
    0x50, // y delta velocity low
    0x52, // y delta velocity high
    0x54, // z delta velocity low
    0x56, // z delta velocity high

    0x0e, // temperature
    0x70, // time m/s
    0x72, // time d/h
    0x74// time y/m
  ),
  'nmea'=>array(
 /// first three letters - sentence to log (letters after "$GP"). next "n"/"b" (up to 24 total) - "n" number (will be encoded 4 digits/byte, follwed by "0xF"
 /// "b" - byte - all but last will have MSB 0 (& 0x7f), the last one - with MSB set (| 0x80). If there are no characters in the field 0xff will be output
    array('RMC','nbnbnbnnnnb'),
    array('GGA','nnbnbnnnnbnbbb'),
    array('GSA','bnnnnnnnnnnnnnnnn'),
    array('VTG','nbnbnbnb')
  ),
  'message'=>'Odometer message' // Message - up to 56 bytes
  );
  return $default_config;
}
/**
Combines $conf and $dflt arrays - all driver parameters are overwritten aftre the call
*/
function combineLoggerConfigs($new_conf,$dflt=null){
  global $default_config;
  $xclk_freq=80000000; // 80 MHz
  $data=array();
  $index=0;
  if (!isset ($dflt)) {
    $dflt=$default_config;
  }
  $conf=$dflt;
  if (isset($new_conf)) foreach ($conf as $key=>$oldvalue) if (isset($new_conf[$key])) $conf[$key]=$new_conf[$key];
/// fix insufficient data from default

  while (count($conf['imu_registers'])<28) $conf['imu_registers'][count($conf['imu_registers'])]=0; // zero pad if less than 28 registers
/// Configure NMEA sentences to log. If there are less than 4 - copy additional (unique) from defaults
  while (count($conf['nmea'])<4) {
    for ($i=0;$i<4;$i++) {
      $new=true;
      for ($j=0;$j<count($conf['nmea']);$j++) if ($conf['nmea'][$j][0]==$dflt['nmea'][$i][0]){ // compare sentence code last 3 letters
         $new=false;
         break;
      }
      if ($new) {
        $conf['nmea'][count($conf['nmea'])]=$dflt['nmea'][$i];
        break;
      }
    }
  }
  for ($i=0;$i<4;$i++) {
    while (strlen($conf['nmea'][$i][0])<3) $conf['nmea'][$i][0].="A"; // just to avoid errors - nonexistent sentences will not hurt, just never logged
  }

  return $conf;
}
function setup_IMU_logger($conf){
  global $verbose;
  $xclk_freq=80000000; // 80 MHz
  $data=array();
  $index=0;
/// program period
  if (strtolower($conf['imu_period'])=='auto') $conf['imu_period']=0xffffffff;
  else $conf['imu_period']+=0;
  $data[$index++]= $conf['imu_period']      & 0xff;
  $data[$index++]=($conf['imu_period']>> 8) & 0xff;
  $data[$index++]=($conf['imu_period']>>16) & 0xff;
  $data[$index++]=($conf['imu_period']>>24) & 0xff;
/// program SCLK divisor and SPI stall time
  $sclk_div=round($xclk_freq/$conf['sclk_freq']/2);
  if      ($sclk_div<1)   $sclk_div=1;
  else if ($sclk_div>255) $sclk_div=255;
  $conf['sclk_freq']=$xclk_freq/$sclk_div/2;
  $stall=(( $conf['stall'] * ( $xclk_freq / $sclk_div )) / 1000000 );
  if      ($stall<  1) $stall=  1; /// does 0 work? need to verify
  else if ($stall>255) $stall=255;
  $conf['stall']=$stall * 1000000 / ($xclk_freq / $sclk_div);
  $data[$index++]= $sclk_div;
  $data[$index++]= $stall;
  $data[$index++]= 0;
  $data[$index++]= 0;
/// Program rs232 baud rate
  $rs232_div=round($xclk_freq /2/$conf['baud_rate']);
  $conf['baud_rate']=$xclk_freq /2/$rs232_div;
  $data[$index++]= $rs232_div      & 0xff;
  $data[$index++]=($rs232_div>> 8) & 0xff;
  $data[$index++]=($rs232_div>>16) & 0xff;
  $data[$index++]=($rs232_div>>24) & 0xff;
/// Program logger configuration (possible to modify only some fields - maybe support it here?)
  $logger_conf=0;
///    #define  IMUCR__IMU_SLOT__BITNM 0    // slot, where 103695 (imu) board is connected: 0 - none, 1 - J9, 2 - J10, 3 - J11)
///    #define  IMUCR__IMU_SLOT__WIDTH 2
  $logger_conf |= (($conf['imu_slot'] & 0x3) | 0x4 )<< 0; /// "4" (bit 2 set)  here means that the data in bits 0,1 will be applied in the FPGA register

///    #define  IMUCR__GPS_CONF__BITNM 3    // slot, where 103695 (imu) bnoard is connected: 0 - none, 1 - J9, 2 - J10, 3 - J11)
///    #define  IMUCR__GPS_CONF__WIDTH 4    // bits 0,1 - slot #, same as for IMU_SLOT, bits 2,3:
                                            // 0 - ext pulse, leading edge,
                                            // 1 - ext pulse, trailing edge
                                            // 2 - start of the first rs232 character after pause
                                            // 3 - start of the last "$" character (start of each NMEA sentence)
  $logger_conf |= ((($conf['gps_slot'] & 0x3) | (($conf['gps_mode'] & 0x3)<<2) ) | 0x10 )<< 3;
///    #define  IMUCR__MSG_CONF__BITNM 8    // source of external pulses to log:
///    #define  IMUCR__MSG_CONF__WIDTH 5    // bits 0-3 - number of fpga GPIO input 0..11 (i.e. 0x0a - external optoisolated sync input (J15)
                                         // 0x0f - disable MSG module
                                         // bit 4 - invert polarity: 0 - timestamp leading edge, log at trailing edge, 1 - opposite
                                         // software may set (up to 56 bytes) log message before trailing end of the pulse
  $logger_conf |= (($conf['msg_conf'] & 0x1f)  | 0x20 )<< 8;
///    #define  IMUCR__SYN_CONF__BITNM 14   // logging frame time stamps (may be synchronized by another camera and have timestamp of that camera)
///    #define  IMUCR__SYN_CONF__WIDTH 1    // 0 - disable, 1 - enable
  $logger_conf |= (($conf['img_sync'] & 0x1)  | 0x2 )<< 14;
///    #define  IMUCR__RST_CONF__BITNM 16   // reset module
///    #define  IMUCR__RST_CONF__WIDTH 1    // 0 - enable, 1 -reset (needs resettimng DMA address in ETRAX also)

///    #define  IMUCR__DBG_CONF__BITNM 18   // several extra IMU configuration bits
///    #define  IMUCR__DBG_CONF__WIDTH 4    // 0 - config_long_sda_en, 1 -config_late_clk, 2 - config_single_wire, should be set for 103695 rev "A" 
  $logger_conf |= (($conf['extra_conf'] & 0xf)  | 0x10 )<< 18;
/// next bits used by the driver only, not the FPGA
///                      ((SLOW_SPI & 1)<<23) | \
///                      (DFLT_SLAVE_ADDR << 24))
  $logger_conf |=  ($conf['slow_spi'] & 0x1)<< 23;
  $logger_conf |=  ($conf['imu_sa']   & 0x7)<< 24;
  $data[$index++]= $logger_conf      & 0xff;
  $data[$index++]=($logger_conf>> 8) & 0xff;
  $data[$index++]=($logger_conf>>16) & 0xff;
  $data[$index++]=($logger_conf>>24) & 0xff;
/// Set time driver will go to sleep if the data is not ready yet (less than full sample in the buffer)
  $data[$index++]= $conf['sleep_busy']      & 0xff;
  $data[$index++]=($conf['sleep_busy']>> 8) & 0xff;
  $data[$index++]=($conf['sleep_busy']>>16) & 0xff;
  $data[$index++]=($conf['sleep_busy']>>24) & 0xff;
/// Set IMU register addresses to logger
//  echo "1\n";
  for ($i=0;$i<28;$i++) $data[$index++]= $conf['imu_registers'][$i]; // truncate to 28
/// Configure NMEA sentences to log. If there are less than 4 - copy additional (unique) from defaults
//  echo "2\n";
  for ($i=0;$i<4;$i++) {
    $d =str_split($conf['nmea'][$i][0]);
//  print_r($d);
    for ($j=0;$j<3;$j++) $data[$index++]=ord($d[$j]);
    $d =str_split($conf['nmea'][$i][1]);
//  print_r($d);
    for ($j=0;$j<29;$j++) $data[$index++]=($j<count($d))?ord($d[$j]):0;
  }
//  echo "3\n";

/// Set default message
  $d =str_split($conf['message']);
  for ($j=0;$j<56;$j++) $data[$index++]=($j<count($d))?ord($d[$j]):0;

// Print the result array as hex data
/*
  print_r($conf);

  echo "\n";
  for ($i=0;$i<count($data);$i++) {
    if (($i & 0xf)==0) printf ("\n%04x:",$i);
    printf (" %02x",$data[$i]);
  }
  echo "\n";
*/
  $bindata="";
  for($i=0;$i<count($data);$i++) $bindata.=chr($data[$i]);
  $dev_imu_ctl  = fopen('/dev/imu_ctl', 'w');
  fwrite($dev_imu_ctl, $bindata, strlen($bindata));
  fseek($dev_imu_ctl,3,SEEK_END); // start IMU
  fclose($dev_imu_ctl);
/// Readback - just testing
  $bd=file_get_contents('/dev/imu_ctl');
  $data =str_split($bd);
  if ($verbose) { 
    echo "\n";
    for ($i=0;$i<count($data);$i++) {
      if (($i & 0xf)==0) printf ("\n%04x:",$i);
      printf (" %02x",ord($data[$i]));
    }
    echo "\n";
  }
}




  
//ls /sys/bus/usb-serial/devices/ttyUSB0/driver -l
//         $xml->addChild ('error','No sync capable board detected, use "role=self" for the onboard timer');
//    $sxml=$xml->asXML();
?>
