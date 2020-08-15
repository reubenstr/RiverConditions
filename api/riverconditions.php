<?php

// Captures river conditions from waterreporter.org and waterservices.usgs.gov
// Data is retreived in JSON, parsed, reduced, and repacked as JSON into a managable size.
// JSON data is cached as files where the file name is the station id.
//
// Aug. 13th 2020
//
// Usage example: www.hostingwebsite.com/api/riverconditions.php?stationId=20209000

set_error_handler("warning_handler", E_WARNING | E_ALL);

$stationId = htmlspecialchars($_GET["stationId"]);


echo get_json($stationId);

// End

function get_json($stationId)
{
    $cacheFile = 'cache' . DIRECTORY_SEPARATOR . $stationId . '.json';

    // Check if json exists in cache and is valid.
    if (file_exists($cacheFile))
    {    
        /*
        if (filemtime($cacheFile) > strtotime('-60 minutes'))
        {
            $fh = fopen($cacheFile, 'r');
            
        if (filesize($cacheFile) != 0)
            {
        return fread($fh, filesize($cacheFile));
            }			            
        }
        */
    }

    // Get json from endpoint then save in cache.
       
	
	$json_source = "USGS";

/*
	if ($json_source == "WR")
	{
		$url = 'http://worldtimeapi.org/api/timezone/Europe';
	}
	else
	{
		 $url = "https://waterservices.usgs.gov/nwis/iv/?format=json&sites={$stationId}&parameterCd=00060,00065";
	}   
    
	$result = get_web_page($url);

    if ($result['errno'] != 0)
    {
        error('curl error: ' . $result['errmsg']);
    }

    if ($result['http_code'] != 200)
    {
        error('http code: ' . $result['http_code']);
    }

    $json = parse_json_from_endpoint($result['content']);
	 $array = json_decode($json);
  */

    // TEMP
    $array = json_decode(file_get_contents('supplementUSGS.json') , true);
	// TEMP
	
	if (json_last_error() != JSON_ERROR_NONE)
	{
		error(json_last_error_msg());
	}
	
    $json = parse_json_from_endpoint($json_source, $array);

    $fh = fopen($cacheFile, 'w');
    fwrite($fh, $json);
    fclose($fh);

    return $json;
}

function parse_json_from_endpoint($json_source, $json)
{

$array = array();

$noData = -9; // Per waterreporter usage.

 $bacteria_threshold_date_time = "";
        $bacteria_threshold_value = $noData;

        $water_temp_c_date_time = "";
        $water_temp_c_value = $noData;

        $e_coli_concentration_date_time = "";
        $e_coli_concentration_value = $noData;
		
		 $stream_flow = "";
		$stream_flow_date_time = $noData;
   
		$gauge_height = "";
		$gauge_height_date_time = $noData;


    if ($json_source == "USGS")
    { 
        $id = $json['value']['timeSeries'][0]['sourceInfo']['siteCode'][0]['value'];
        $name = $json['value']['timeSeries'][0]['sourceInfo']['siteName'];
        $is_active = true;
        $description = "USGS stream monitoring site.";
		$recordTime = date(DateTime::ISO8601);
      
	  // TODO: test if keys exist.
        $variable_count = count($json['value']['timeSeries']);

        for ($i = 0;$i < (int)$variable_count;$i++)
        {
            if ("00060" == $json['value']['timeSeries'][$i]['variable']['variableCode'][0]['value'])
            {
                $stream_flow = $json['value']['timeSeries'][$i]['values'][0]['value'][0]['value'];
                $stream_flow_date_time = $json['value']['timeSeries'][$i]['values'][0]['value'][0]['dateTime'];
            }
            if ("00065" == $json['value']['timeSeries'][$i]['variable']['variableCode'][0]['value'])
            {
                $gauge_height = $json['value']['timeSeries'][$i]['values'][0]['value'][0]['value'];
                $gauge_height_date_time = $json['value']['timeSeries'][$i]['values'][0]['value'][0]['dateTime'];
            }
        }   
    }
    else if ($json_source == "WR")
    {		
        // Get the index of most recent record (records are formatted in an array).
        $bacteria_threshold_recent_index = count($json['sample_idx']['bacteria_threshold']) - 1;
        $water_temp_c_recent_index = count($json['sample_idx']['water_temp_c']) - 1;
        $e_coli_concentration_recent_index = count($json['sample_idx']['e_coli_concentration']) - 1;

        $id = $json['station']['id'];
        $name = $json['station']['name'];
        $is_active = $json['station']['is_active'];
        $description = $json['station']['description'];

        $bacteria_threshold_date_time = $json['sample_idx']['bacteria_threshold'][$bacteria_threshold_recent_index]['date'];
        $bacteria_threshold_value = $json['sample_idx']['bacteria_threshold'][$bacteria_threshold_recent_index]['value'];

        $water_temp_c_date_time = $json['sample_idx']['water_temp_c'][$water_temp_c_recent_index]['date'];
        $water_temp_c_value = $json['sample_idx']['water_temp_c'][$water_temp_c_recent_index]['value'];

        $e_coli_concentration_date_time = $json['sample_idx']['e_coli_concentration'][$e_coli_concentration_recent_index]['date'];
        $e_coli_concentration_value = $json['sample_idx']['e_coli_concentration'][$e_coli_concentration_recent_index]['value'];
    }
	
	// Get static station data from stations.json
	 $stationFile = 'stations.json';
    if (file_exists($stationFile))
    { 
      $stationArray = json_decode(file_get_contents($stationFile) , true); 
	 
		if (!isset($stationArray['stations'][$id]))
		{
			error("Station with stationId of {$id} not found in {$stationFile}");
		}  
	  
		$shortName = $stationArray['stations'][$id]['shortName'];
		$location = $stationArray['stations'][$id]['location'];
    }
	else
	{
		error("File not found: stations.json");
	}	
	
    
    $array['station'] = array(
        'id' => $id,
        'name' => $name,
		'shortName' => $shortName,
		'location' => $location,
        'isActive' => $is_active,
        'description' => $description,
		'recordTime' => $recordTime
		
    );

    $array['data'] = array(
        'bacteriaThreshold' => array(
            'date' => $bacteria_threshold_date_time,
            'value' => $bacteria_threshold_value
        ) ,
        'waterTempC' => array(
            'date' => $water_temp_c_date_time,
            'value' => $water_temp_c_value
        ) ,
        'eColiConcentration' => array(
            'date' => $e_coli_concentration_date_time,
            'value' => $e_coli_concentration_value
        ) ,
        'streamFlow' => array(
            'date' => $stream_flow_date_time,
            'value' => intval($stream_flow)
        ),
		'gaugeHeight' => array(
            'date' => $gauge_height_date_time,
            'value' => intval($gauge_height)
        )
    );	
	
    return json_encode($array, JSON_PRETTY_PRINT); 
}

function error($msg)
{
    $repsonse = new \stdClass();
    $repsonse->error = true;
    $repsonse->message = $msg;
    echo json_encode($repsonse);
    exit();
}

/**
 * Get a web file (HTML, XHTML, XML, image, etc.) from a URL.  Return an
 * array containing the HTTP server response header fields and content.
 */
function get_web_page($url)
{
    $user_agent = 'Mozilla/5.0 (Windows NT 6.1; rv:8.0) Gecko/20100101 Firefox/8.0';

    $options = array(

        CURLOPT_CUSTOMREQUEST => "GET", //set request type post or get
        CURLOPT_POST => false, //set to GET
        CURLOPT_USERAGENT => $user_agent, //set user agent
        CURLOPT_RETURNTRANSFER => true, // return web page
        CURLOPT_HEADER => false, // don't return headers
        CURLOPT_FOLLOWLOCATION => true, // follow redirects
        CURLOPT_ENCODING => "", // handle all encodings
        CURLOPT_AUTOREFERER => true, // set referer on redirect
        CURLOPT_CONNECTTIMEOUT => 120, // timeout on connect
        CURLOPT_TIMEOUT => 120, // timeout on response
        CURLOPT_MAXREDIRS => 10, // stop after 10 redirects
        
    );

    $ch = curl_init($url);
    curl_setopt_array($ch, $options);
    $content = curl_exec($ch);
    $err = curl_errno($ch);
    $errmsg = curl_error($ch);
    $header = curl_getinfo($ch);
    curl_close($ch);

    $header['errno'] = $err;
    $header['errmsg'] = $errmsg;
    $header['content'] = $content;
    return $header;
}

function warning_handler($errno, $errstr)
{
    error('PHP error: ' . $errno . ', ' . $errstr);
}

?>
