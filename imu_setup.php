#!/usr/local/sbin/php -q
<?php
/*
*! FILE NAME  : imu_setup.php
*! DESCRIPTION: Handles GPS/compass and IMU/logger. Will replace star-compass_gps.php
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
*!  $Log: imu_setup.php,v $
*!  Revision 1.8  2012/07/03 20:54:36  elphel
*!  bug fix
*!
*!  Revision 1.7  2011/12/22 05:40:36  elphel
*!  missed revision
*!
*!  Revision 1.5  2011/09/06 22:02:49  elphel
*!  added 'list' link
*!
*!  Revision 1.4  2011/09/06 21:56:31  elphel
*!  help typo
*!
*!  Revision 1.3  2011/09/06 21:53:49  elphel
*!  adding to help
*!
*!  Revision 1.2  2011/09/06 21:31:48  elphel
*!  added help
*!
*!  Revision 1.1  2011/09/06 19:07:50  elphel
*!  initial release, based on start_gps_compass.php
*!
*/
require 'i2c.inc';
  $xclk_freq=80000000; // 80 MHz
  $messageOffset=0xb0; // offset to start of "odometer" message in the device driver file
  $config_name="/etc/imu_logger.xml";

  if (isset($_SERVER["SERVER_PROTOCOL"])) {
//    echo "web server\n";
    if (isset($_GET['msg'])) {
      setMessage(urldecode($_GET['msg']));
    } else if (isset($_GET['lmsg'])) {
      setMessage(urldecode($_GET['msg']));
// software toggle input "odometer" line
    }
    /// modify parameters if specified
    $conf=readSettings(init_default_config()); // read current settings
    $apply=0;
//      echo "<pre>\n";
    if (count($_GET)==0) {
       help();
       exit(0);
    }
    if (array_key_exists('source',$_GET)) {
      $source=file_get_contents ($_SERVER['SCRIPT_FILENAME']);
      header("Content-Type: text/php");
      header("Content-Length: ".strlen($source)."\n");
      header("Pragma: no-cache\n");
      echo $source;
      exit (0);
    }

    foreach ($_GET as $key=>$value) {
      $access=split(':',$key);
      if ($value==='') continue;
      if (count($access)<1) continue;
      if (!isset($conf[$access[0]])) continue;
      if (count($access)==1) {
        if (($access[0]=='imu_period') && ($value=='auto')) $value = 0xffffff00;
        if (is_string($conf[$access[0]])) $conf[$access[0]]=$value;
        else  $conf[$access[0]]=$value+0;
        $apply++;
      } else { // count($access)>1
        if (!isset($conf[$access[0]][$access[1]])) continue;
        if (count($access)==2) {
          if (is_string($conf[$access[0]][$access[1]])) $conf[$access[0]][$access[1]]=$value;
          else  $conf[$access[0]][$access[1]]=$value+0;
          $apply++;
        } else { // count($access)>2)
          if (count($access)>3) continue;
          if      ($access[2]=='sentence') $access[2]=0;
          else if ($access[2]=='format')   $access[2]=1;
          if (!isset($conf[$access[0]][$access[1]][$access[2]])) continue;
          if (is_string($conf[$access[0]][$access[1]][$access[2]])) $conf[$access[0]][$access[1]][$access[2]]=$value;
          else  $conf[$access[0]][$access[1]][$access[2]]=$value+0;
          $apply++;
        }
      }
/*
echo 'key='.$key."\n";
print_r($access);
var_dump($value);
*/
    }
    if ($apply>0) {
      echo 'modified '.$apply."parameters<br/>\n";
//         print_r($conf);
      setup_IMU_logger($conf);
    }
    if (isset($_GET['save'])) {
//        if ($verbose) echo 'Writing IMU logger configuration to '. $config_name."\n";
        $conf=readSettings(init_default_config());
        $logger_xml=loggerConfigToXML($conf);
        $conf_file=fopen($config_name,"w");
        fwrite($conf_file,$logger_xml->asXML());
        fclose($conf_file);
        exec ('sync');
    }


    if (isset($_GET['list'])) {
      $conf=readSettings(init_default_config());
      $logger_xml=loggerConfigToXML($conf);
      $logger_xml_text=$logger_xml->asXML();
      header("Content-Type: text/xml");
      header("Content-Length: ".strlen($logger_xml_text)."\n");
      header("Pragma: no-cache\n");
      printf($logger_xml_text);
    } else echo 'OK';
    exit(0);
  } else {
//    echo "CGI\n";
  }
/*
    echo "<!--\n";
    var_dump($_GET);
    var_dump($_SERVER);
    echo "-->";
    echo 'OK';
    exit(0);
*/
//! Here comes if CLI
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
function help(){
   $GP='$GP';
   $GPRMC='$GPRMC';
   $GPGGA='$GPGGA';
   $GPGSA='$GPGSA';
   $GPVTG='$GPVTG';
   $script_name=$_SERVER['SCRIPT_NAME'];
  $prefix_url='http://'.$_SERVER['SERVER_ADDR'].$_SERVER['SCRIPT_NAME'];
//         echo <<<USAGE
//   <h4> Control links:</h4>
//   <ul>
//   <li><a href="?$url_init">Initialization Parameters (i.e. after powerup)</a></li>

    echo <<<USAGE
<head>
  <meta http-equiv="Content-Type" content="text/html; charset=utf-8"/>
  <title>Usage of IMU/GPS logger ($script_name)</title>
</head>
<body>
 <h2>Usage of IMU/GPS logger ($script_name)</h2>
   <p>This script controls IMU/GPS logger. When the camera starts, the script looks for the <i>/etc/imu_logger.xml</i>" and if it is found, the settings from this file are applied to the logger. If there is no such file (i.e. at first boot after reflashing the camera) script looks for the optional extension boards <a href=""http://wiki.elphel.com/index.php?title=103695>103695</a> and <a href="http://wiki.elphel.com/index.php?title=103696">103696</a> and uses the information from their flash memory (not erased during reflash) combined with the defaults to create initial <i>/etc/imu_logger.xml</i> file that can be modified later</p>
   <p>Script receives parameters as HTTP GET request, there are five special parameters:
   <ul>
    <li><b><a href="$prefix_url?source">source</a></b> - returns the source of this script</li>
    <li><b><a href="$prefix_url?list">list</a></b> - returns an XML containing current logger settings, the names of the nodes can be used to modify specific settings</li>
    <li><b>save</b> - overwrite <i>/etc/imu_logger.xml</i> with the current settings - they will be used next time camera will boot up</li>
    <li><b>msg=url-encoded-text-up-to-56-bytes</b> - set the &quot;odometer&quot; message that will be logged next time the external input is toggled </li>
    <li><b>smsg=url-encoded-text-up-to-56-bytes</b> - (not yet implemented) set the &quot;odometer&quot; message and trigger logging immediately</li>
  </ul>
  Other parameters are just <i>name=value</i> pairs with colons separating names and subnames 
   <ul>
    <li><b>imu_period</b> - set the IMU logging period in SPI SCLK periods, maybe set to <i>auto</i> (default value, same as 0xff000000) to log when the IMU data is ready. Value 0 turns the IMU logging off</li>
    <li><b>sclk_freq</b> - set SPI clock frequency, in Hz. Default is 5000000 (5MHz)</li>
    <li><b>stall</b> - set SPI 'stall' time, in microseconds. Default is 2 usec</li>
    <li><b>baud_rate</b> - set baud rate for the serial GPS, default is 19200</li>
    <li><b>imu_slot</b> - specify the IMU board (<a href="http://wiki.elphel.com/index.php?title=103695">103695</a>) connection to the <a href="http://wiki.elphel.com/index.php?title=10369">10369</a> board 0 - none, 1 - J9, 2 - J10, 3 - J11. Default is 1 (J9)</li>
    <li><b>gps_slot</b> - specify the GPS board (<a href="http://wiki.elphel.com/index.php?title=103696">103696</a>) connection to the <a href="http://wiki.elphel.com/index.php?title=10369">10369</a> board 0 - none, 1 - J9, 2 - J10, 3 - J11. Default is 2 (J10)</li>
    <li><b>gps_mode</b> - synchronization mode for the GPS: 0 - pulse-per-sample external input, negative front, 1 - pps input positive front, 2 - start of a first sentence after pause, 3 - start of each individual sentence. Mode 2 is the default, modes 0 and 1 require that GPS has a dedicated synchro output</li>
    <li><b>msg_conf</b> - GPIO number and polarity (+16 - invert polarity) uses to log external events (i.e. odometer pulses from a vehicle wheel sensor). Timestamp uses leading edge, software may write to the 56-byte buffer (see 'msg=' command above) before the trailing edge to attach the message to the event after the event occured. In the case of short external pulses the logged message will be the last written before the event</li>
    <li><b>img_sync</b> - 0 - disable logging of the images, 1 - enable. In the case of the multiple synchronized cameras this provides a way to match external (master) camera timestamps transmitted over the synchronization cable, with the local camera timestamps used by the logger - both are  logged. Default is 1</li>
    <li><b>extra_conf</b> - Development signals (0 - config_long_sda_en, 1 -config_late_clk, 2 - config_single_wire, should be set for 103695 rev A). Default is 4</li>
    <li><b>slow_spi</b> - may be set to 1 for slow SPI devices (other changes may be needed, i.e. changing power from 3.3V to 5.0V selected by a resistor divider). Default is 0 </li>
    <li><b>imu_sa</b> - i2c slave address modifier (matches hardware jumpers). Default is 3</li>
    <li><b>sleep_busy</b> - setting for the driver. It is a sleep time (in microseconds) before the driver will poll the logger buffer if it does not have the full sample record (64 bytes of data) ready</li>
    <li><b>imu_registers</b> - setting the IMU register addresses to be logged (total number of logged registers is 28). You may see the complete list of the registers in the IMU datasheet. The registers are addressed as 'imu_registers:NN', where NN is the register sequnce number, starting with 0. Below are the default values:
      <ul>
        <li><b>imu_registers:0</b>=0x10 - x gyro low</li>
        <li><b>imu_registers:1</b>=0x12 - x gyro high</li>
        <li><b>imu_registers:2</b>=0x14 - y gyro low</li>
        <li><b>imu_registers:3</b>=0x16 - y gyro high</li>
        <li><b>imu_registers:4</b>=0x18 - z gyro low</li>
        <li><b>imu_registers:5</b>=0x1a - z gyro high</li>

        <li><b>imu_registers:6</b>=0x1c - x accel low</li>
        <li><b>imu_registers:7</b>=0x1e - x accel high</li>
        <li><b>imu_registers:8</b>=0x20 - y accel low</li>
        <li><b>imu_registers:9</b>=0x22 - y accel high</li>
        <li><b>imu_registers:10</b>=0x24 - z accel low</li>
        <li><b>imu_registers:11</b>=0x26 - z accel high</li>

        <li><b>imu_registers:12</b>=0x40 - x delta angle low</li>
        <li><b>imu_registers:13</b>=0x42 - x delta angle high</li>
        <li><b>imu_registers:14</b>=0x44 - y delta angle low</li>
        <li><b>imu_registers:15</b>=0x46 - y delta angle high</li>
        <li><b>imu_registers:16</b>=0x48 - z delta angle low</li>
        <li><b>imu_registers:17</b>=0x4a - z delta angle high</li>

        <li><b>imu_registers:18</b>=0x4c - x delta velocity low</li>
        <li><b>imu_registers:19</b>=0x4e - x delta velocity high</li>
        <li><b>imu_registers:20</b>=0x50 - y delta velocity low</li>
        <li><b>imu_registers:21</b>=0x52 - y delta velocity high</li>
        <li><b>imu_registers:22</b>=0x54 - z delta velocity low</li>
        <li><b>imu_registers:23</b>=0x56 - z delta velocity high</li>

        <li><b>imu_registers:24</b>=0x0e - temperature</li>
        <li><b>imu_registers:25</b>=0x70 - time m/s</li>
        <li><b>imu_registers:26</b>=0x72 - time d/h</li>
        <li><b>imu_registers:27</b>=0x74 - time y/m</li>
      </ul></li>
    <li><b>nmea</b> - set format of the four logged NMEA sentences. For each sentence two text fields have to be specified - 'sentence' (the three letters following the comman prefix '$GP'), and 'format' that consists of a sequence of 'n' (for number) and 'b' (for bytes), up to 24 total. Numbers consisting of digits, minus sign and decimal point are encoded with 2 digits per byte (to make sure the longest sentence fits into the 56 byte logger payload buffer), followed by the 0xf nibble (half-byte). Bytes are encoded with MSB cleared ( & 0x7f) for all bytes in a field but the last, the last byte has it set (|0x80). Empty byte field is logged as 0xff. Below are the default settings:
      <ul>
        <li><b>nmea:0:sentence</b>=RMC - sentence $GPRMC</li>
        <li><b>nmea:0:format</b>=nbnbnbnnnnb - format for the $GPRMC</li>
        <li><b>nmea:1:sentence</b>=GGA - sentence $GPGGA</li>
        <li><b>nmea:1:format</b>=nnbnbnnnnbnbbb - format for the $GPGGA</li>
        <li><b>nmea:2:sentence</b>=GSA - sentence $GPGSA</li>
        <li><b>nmea:2:format</b>=bnnnnnnnnnnnnnnnn - format for the $GPGSA</li>
        <li><b>nmea:3:sentence</b>=VTG - sentence $GPVTG</li>
        <li><b>nmea:3:format</b>=nbnbnbnb - format for the $GPVTG</li>
      </ul>
    </li>
  </ul>
</p>
<p>You may record the log to a file (i.e. on external USB stick or SSD/HDD) with teh following command:<br/>
<b>cat /dev/imu &gt;<i>log-file-path</i></b></p>

<p>Test logger and GPS:<br/>
<b>fpcf -imu 10000 | grep &quot;1:&quot;</b><p/>
<p>The fpcf output starts the line (logged  sample) with the source of the sample:
 <ul>
   <li><b>0:</b> - IMU (normally 2560 samples/sec)</li>
   <li><b>1:</b> - GPS </li>
   <li><b>2:</b> - camera images</li>
   <li><b>3:</b> - 'odometer' - external events</li>
 </ul>
'10000' means that fpcf should output 10000 samples, that takes approximately  4 seconds if IMU output is being logged.
</p>
</body>
USAGE;

}
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
  'gps_mode'=>2, // 0 - pps neg, 1 - pps pos, 2 - start of first sentence after pause, 3 start of sentence
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
 /// first three letters - sentence to log (letters after "$GP"). next "n"/"b" (up to 24 total) - "n" number (will be encoded 2 digits/byte, follwed by "0xF"
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
  global $verbose,$xclk_freq;
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
function setMessage($msg) {
  global $messageOffset;
  $data=array();
  $d =str_split($msg);
  $index=0;
  for ($j=0;$j<56;$j++) $data[$index++]=($j<count($d))?ord($d[$j]):0;
  $bindata="";
  for($i=0;$i<count($data);$i++) $bindata.=chr($data[$i]);
  $dev_imu_ctl  = fopen('/dev/imu_ctl', 'w');
  fseek($dev_imu_ctl,$messageOffset,SEEK_SET); // start IMU
  echo ftell($dev_imu_ctl)."<br/>\n";
  fwrite($dev_imu_ctl, $bindata, strlen($bindata));
//  fseek($dev_imu_ctl,3,SEEK_END); // start IMU
  fclose($dev_imu_ctl);
var_dump ( $bindata);

}

function readSettings($conf=null) {
  global   $xclk_freq; // =80000000; // 80 MHz
  $bd=file_get_contents('/dev/imu_ctl');
  $data =str_split($bd);
  for ($i=0;$i<count($data);$i++) $data[$i]=ord($data[$i]);
/*
//  if ($verbose) { 
    echo "\n";
    for ($i=0;$i<count($data);$i++) {
      if (($i & 0xf)==0) printf ("\n%04x:",$i);
      printf (" %02x",$data[$i]);
    }
    echo "\n";
//  }
*/
  if ($conf!=null) {


/// read current period
  $conf['imu_period'] =$data[$index++];
  $conf['imu_period']|=$data[$index++]<<8;
  $conf['imu_period']|=$data[$index++]<<16;
  $conf['imu_period']|=$data[$index++]<<24;
/// read current SCLK divisor and SPI stall time
  $sclk_div =$data[$index++];
  $stall    =$data[$index++];
  $index++;
  $index++;
  $conf['sclk_freq']=$xclk_freq/$sclk_div/2;
  $conf['stall']=$stall * 1000000 / ($xclk_freq / $sclk_div);
/// read current rs232 baud rate
  $rs232_div =$data[$index++];
  $rs232_div|=$data[$index++]<<8;
  $rs232_div|=$data[$index++]<<16;
  $rs232_div|=$data[$index++]<<24;
  $conf['baud_rate']=$xclk_freq /2/$rs232_div;

/// read current logger configuration (possible to modify only some fields - maybe support it here?)
/// assuming set bits are set, otherwise it is impossible to find out the old value
  $logger_conf =$data[$index++];
  $logger_conf|=$data[$index++]<<8;
  $logger_conf|=$data[$index++]<<16;
  $logger_conf|=$data[$index++]<<24;
///    #define  IMUCR__IMU_SLOT__BITNM 0    // slot, where 103695 (imu) board is connected: 0 - none, 1 - J9, 2 - J10, 3 - J11)
///    #define  IMUCR__IMU_SLOT__WIDTH 2
  $conf['imu_slot']= ($logger_conf>>0) & 3;
///    #define  IMUCR__GPS_CONF__BITNM 3    // slot, where 103695 (imu) bnoard is connected: 0 - none, 1 - J9, 2 - J10, 3 - J11)
///    #define  IMUCR__GPS_CONF__WIDTH 4    // bits 0,1 - slot #, same as for IMU_SLOT, bits 2,3:
                                            // 0 - ext pulse, leading edge,
                                            // 1 - ext pulse, trailing edge
                                            // 2 - start of the first rs232 character after pause
                                            // 3 - start of the last "$" character (start of each NMEA sentence)
//  $logger_conf |= ((($conf['gps_slot'] & 0x3) | (($conf['gps_mode'] & 0x3)<<2) ) | 0x10 )<< 3;
  $conf['gps_slot']= ($logger_conf >> 3) & 3;
  $conf['gps_mode']= ($logger_conf >> 5) & 3;

///    #define  IMUCR__MSG_CONF__BITNM 8    // source of external pulses to log:
///    #define  IMUCR__MSG_CONF__WIDTH 5    // bits 0-3 - number of fpga GPIO input 0..11 (i.e. 0x0a - external optoisolated sync input (J15)
                                         // 0x0f - disable MSG module
                                         // bit 4 - invert polarity: 0 - timestamp leading edge, log at trailing edge, 1 - opposite
                                         // software may set (up to 56 bytes) log message before trailing end of the pulse
//  $logger_conf |= (($conf['msg_conf'] & 0x1f)  | 0x20 )<< 8;
  $conf['msg_conf']= ($logger_conf >> 8) & 0x1f;
///    #define  IMUCR__SYN_CONF__BITNM 14   // logging frame time stamps (may be synchronized by another camera and have timestamp of that camera)
///    #define  IMUCR__SYN_CONF__WIDTH 1    // 0 - disable, 1 - enable

//  $logger_conf |= (($conf['img_sync'] & 0x1)  | 0x2 )<< 14;
  $conf['img_sync']= ($logger_conf >> 14) & 0x1;

///    #define  IMUCR__RST_CONF__BITNM 16   // reset module
///    #define  IMUCR__RST_CONF__WIDTH 1    // 0 - enable, 1 -reset (needs resettimng DMA address in ETRAX also)

///    #define  IMUCR__DBG_CONF__BITNM 18   // several extra IMU configuration bits
///    #define  IMUCR__DBG_CONF__WIDTH 4    // 0 - config_long_sda_en, 1 -config_late_clk, 2 - config_single_wire, should be set for 103695 rev "A" 
//  $logger_conf |= (($conf['extra_conf'] & 0xf)  | 0x10 )<< 18;
  $conf['extra_conf']= ($logger_conf >> 18) & 0xf;
/// next bits used by the driver only, not the FPGA
///                      ((SLOW_SPI & 1)<<23) | \
///                      (DFLT_SLAVE_ADDR << 24))
//  $logger_conf |=  ($conf['slow_spi'] & 0x1)<< 23;
//  $logger_conf |=  ($conf['imu_sa']   & 0x7)<< 24;

  $conf['slow_spi']= ($logger_conf >> 23) & 0x1;
  $conf['imu_sa']= ($logger_conf >> 24) & 0x7;

/// read current time driver will go to sleep if the data is not ready yet (less than full sample in the buffer)

  $conf['sleep_busy'] =$data[$index++];
  $conf['sleep_busy']|=$data[$index++]<<8;
  $conf['sleep_busy']|=$data[$index++]<<16;
  $conf['sleep_busy']|=$data[$index++]<<24;


/// read current IMU register addresses to logger
//  echo "1\n";
//  for ($i=0;$i<28;$i++) $data[$index++]= $conf['imu_registers'][$i]; // truncate to 28
  for ($i=0;$i<28;$i++) $conf['imu_registers'][$i]=$data[$index++];

/// read current NMEA sentences to log. If there are less than 4 - copy additional (unique) from defaults
//  echo "2\n";
  $conf['nmea']=array(array("",""),array("",""),array("",""),array("",""));
  for ($i=0;$i<count($conf['nmea']);$i++) {
//    $d =str_split($conf['nmea'][$i][0]);
//    for ($j=0;$j<3;$j++) $data[$index++]=ord($d[$j]);
    for ($j=0;$j<3;$j++) $conf['nmea'][$i][0].=chr($data[$index++]);
//    $d =str_split($conf['nmea'][$i][1]);
//    for ($j=0;$j<29;$j++) $data[$index++]=($j<count($d))?ord($d[$j]):0;
    for ($j=0;$j<29;$j++) {
     $d=$data[$index++];
     if ($d!=0) $conf['nmea'][$i][1].=chr($d);
     else {
       $index+=(29-1-$j);
       break;
     }
    }
  }
/// read current message
//  $conf['message_offset']=$index;
//  $d =str_split($conf['message']);
//  for ($j=0;$j<56;$j++) $data[$index++]=($j<count($d))?ord($d[$j]):0;
  $conf['message']="";
  for ($j=0;$j<56;$j++) {
     $d=$data[$index++];
     if ($d!=0) $conf['message'].=chr($d);
     else {
       $index+=(56-1-$j);
       break;
     }
  }

//  var_dump ($conf);
  return $conf;
  }
}

?>
