<?php
/*
	Captures river conditions from waterreporter.org and waterservices.usgs.gov
	Data is retreived in JSON, parsed, reduced, and repacked as JSON into a managable size.
	JSON data is cached as files where the file name is the station id.

	Sep. 5th 2020
	
	Usage example where hostingwebsite is your website: 
		www.hostingwebsite.com/api/riverconditions?stationId=02029000,8863
*/
 

//set_error_handler("warning_handler", E_WARNING | E_ALL);

// Check for present and valid station parameters.
if (isset($_GET['stationId']))
{
    $stationIdRaw = htmlspecialchars($_GET["stationId"]);
}
else
{
    error("stationId parameter required.");
}

$stationIdArray = explode(",", $stationIdRaw);



// Set station IDs for API endpoints.
$usgsId = null;
$wrId = null;
foreach ($stationIdArray as $sId)
{
	if (!is_numeric($sId))
	{
		error("Invalid stationId parameter: {$sId}");
	}
	
    if (strlen($sId) == 8)
    {
        $usgsId = $sId;
    }
    else
    {
        $wrId = $sId;
    }
}

// Get json.
if ($usgsId != null)
{
    $usgsJson = get_json("USGS", $usgsId);
    //$usgsJson = file_get_contents('usgsTestData.json'); // TEMP    
}
else
{
    $usgsJson = null;
}

if ($wrId != null)
{
    $wrJson = get_json("WR", $wrId);
    //$wrJson = file_get_contents('wrTestData.json'); // TEMP    
}
else
{
    $wrJson = null;
}

$jsonComplete = parse_station_json($usgsJson, $wrJson);

echo $jsonComplete;

// End script.


function get_json($stationType, $stationId)
{    
	$cacheFile = 'cache' . DIRECTORY_SEPARATOR . $stationId . '.json';

    /**/
    // Check if json exists in cache and is valid.
    if (file_exists($cacheFile))
    {
        if (filemtime($cacheFile) > strtotime('-720 minutes'))
        {
            $fh = fopen($cacheFile, 'r');

            if (filesize($cacheFile) != 0)
            {
                return fread($fh, filesize($cacheFile));
            }
        }
    }	

    // Get json from endpoint, save json into cache file.
    if ($stationType == "USGS")
    {
        $url = "https://waterservices.usgs.gov/nwis/iv/?format=json&variable=00060,00065&sites={$stationId}";
    }
    else if ($stationType == "WR")
    {
        $url = "https://stations.waterreporter.org/{$stationId}/supplement.json";
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


    // Cache station json to file system.
    $fh = fopen($cacheFile, 'w');
    fwrite($fh, $result['content']);
    fclose($fh);

    return $result['content'];
}

function parse_station_json($usgsJson, $wrJson)
{

    // Init variables.
    $array = array();
	$noData = "N.A.";
	$locationStatus = "";

    $usgsId = "";
    $wrId = "";
    $usgsName = "";
    $wrName = "";
    $wrIsActive = "";
    $usgsDescription = "";
    $wrDescription = "";
    $recordTime = date(DateTime::ISO8601);

    
    $bacteria_threshold_date_time = "";
    $bacteria_threshold_value = $noData;
	$bacteria_threshold_safety = $noData;

    $water_temp_c_date_time = "";
    $water_temp_c_value = $noData;
	$water_temp_c_safety = $noData;

    $e_coli_concentration_date_time = "";
    $e_coli_concentration_value = $noData;
	$e_coli_concentration_safety = $noData;
   
    $stream_flow_date_time = "";
	 $stream_flow_value = $noData;
	 $stream_flow_safety  = $noData;
   
    $gauge_height_date_time = "";
	 $gauge_height_value = $noData;
	 $gauge_height_safety  = $noData;
	
	// Process USGS JSON data.
    if ($usgsJson != null)
    {
        $usgsArray = json_decode($usgsJson, true);

        if (json_last_error() != JSON_ERROR_NONE)
        {
            error("USGS json error: " . json_last_error_msg());
        }

        $usgsId = $usgsArray['value']['timeSeries'][0]['sourceInfo']['siteCode'][0]['value'];
        $usgsName = $usgsArray['value']['timeSeries'][0]['sourceInfo']['siteName'];
        $usgsDescription = "USGS stream monitoring site.";

        $variable_count = count($usgsArray['value']['timeSeries']);

        for ($i = 0;$i < (int)$variable_count;$i++)
        {
            if ("00060" == $usgsArray['value']['timeSeries'][$i]['variable']['variableCode'][0]['value'])
            {
                $stream_flow_value = $usgsArray['value']['timeSeries'][$i]['values'][0]['value'][0]['value'];
                $stream_flow_date_time = $usgsArray['value']['timeSeries'][$i]['values'][0]['value'][0]['dateTime'];
				// TODO: determine if there is an offical recommondation for stream flow safety.
				if ($stream_flow_value > 5000)
				{
					$stream_flow_safety = "Danger";
				}
				else
				{
					$stream_flow_safety = "Fair";
				}
            }
            if ("00065" == $usgsArray['value']['timeSeries'][$i]['variable']['variableCode'][0]['value'])
            {
                $gauge_height_value = $usgsArray['value']['timeSeries'][$i]['values'][0]['value'][0]['value'];
                $gauge_height_date_time = $usgsArray['value']['timeSeries'][$i]['values'][0]['value'][0]['dateTime'];
				// TODO: gauge height safety appears to be determined by a site by site basis
				/*
				if ($gauge_height_value > 8)
				{
					$gauge_height_safety = "Danger";				
					}
				else
				{
					$gauge_height_safety = "Fair";
				}
				*/
            }
        }
    }
    
	// Process WR JSON data.
	if ($wrJson != null)
    {
        $wrArray = json_decode($wrJson, true);

        if (json_last_error() != JSON_ERROR_NONE)
        {
            error("WR json error: " . json_last_error_msg());
        }

        // Get the index of most recent record (records are formatted in an array).
        $bacteria_threshold_recent_index = count($wrArray['sample_idx']['bacteria_threshold']) - 1;
        $water_temp_c_recent_index = count($wrArray['sample_idx']['water_temp_c']) - 1;
        $e_coli_concentration_recent_index = count($wrArray['sample_idx']['e_coli_concentration']) - 1;

        $wrId = strval($wrArray['station']['id']);
        $wrName = $wrArray['station']['name'];
        $wrIsActive = $wrArray['station']['is_active'];
        $wrDescription = $wrArray['station']['description'];

        $bacteria_threshold_date_time = $wrArray['sample_idx']['bacteria_threshold'][$bacteria_threshold_recent_index]['date'];
        $bacteria_threshold_value = $wrArray['sample_idx']['bacteria_threshold'][$bacteria_threshold_recent_index]['value'];

        $water_temp_c_date_time = $wrArray['sample_idx']['water_temp_c'][$water_temp_c_recent_index]['date'];
        $water_temp_c_value = $wrArray['sample_idx']['water_temp_c'][$water_temp_c_recent_index]['value'];

        $e_coli_concentration_date_time = $wrArray['sample_idx']['e_coli_concentration'][$e_coli_concentration_recent_index]['date'];
        $e_coli_concentration_value = $wrArray['sample_idx']['e_coli_concentration'][$e_coli_concentration_recent_index]['value'];
				
		// Determine safety values.
		$bacteria_threshold_safety = $bacteria_threshold_value == 0 ? "Fair" : "Danger";  
		$water_temp_c_safety = $water_temp_c_value < 20 ? "Danger" : $water_temp_c_value < 32 ? "Caution" : "Fair";
		//$e_coli_concentration_safety = $e_coli_concentration_value < 104  ? "Fair" : "Danger";
		$e_coli_concentration_safety = $bacteria_threshold_safety;
}

	// determin station status (displayed on dashboard's LEDs)	
	// TODO: investigate how USGS and WR determines fair/caution/danger thresholds.
	if ($bacteria_threshold_safety == "Danger")
	{
		$locationStatus = "Danger";
	}
	else
	{
	    $locationStatus = "Fair";
	}

	// Build JSON data.
    $array['station'] = array(
        'usgsId' => $usgsId,
        'wrId' => $wrId,
        'usgsName' => $usgsName,
        'wrName' => $wrName,
        'wrIsActive' => $wrIsActive,
        'usgsDescription' => $usgsDescription,
        'wrDescription' => $wrDescription,
        'recordTime' => $recordTime,
		'locationStatus' => $locationStatus
    );	
	
    $array['data'] = array(
        'bacteriaThreshold' => array(
            'date' => $bacteria_threshold_date_time,
			 'value' => wr_variable_to_text($bacteria_threshold_value),
            'safety' =>   $bacteria_threshold_safety
        ) ,
        'waterTempC' => array(
            'date' => $water_temp_c_date_time,
            'value' => wr_variable_to_text($water_temp_c_value),
			'safety' =>   $water_temp_c_safety
        ) ,
        'eColiConcentration' => array(
            'date' => $e_coli_concentration_date_time,
            'value' => wr_variable_to_text($e_coli_concentration_value),
			'safety' =>  $e_coli_concentration_safety
        ) ,
        'streamFlow' => array(
            'date' => $stream_flow_date_time,
            'value' => strval($stream_flow_value),
			'safety' => $stream_flow_safety
        ) ,
        'gaugeHeight' => array(
            'date' => $gauge_height_date_time,
            'value' => strval($gauge_height_value),
			'safety' => $gauge_height_safety
        )
    );

    return json_encode($array, JSON_PRETTY_PRINT);
}

function error($msg)
{
    $repsonse = new \stdClass();
    $repsonse->error = true;
	$repsonse->date = date(DateTime::ISO8601);
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


function wr_variable_to_text($var)
{
	if (!is_numeric($var))
	{
		return $var;
	}
	
	if ($var == -9) // -9 per waterreporter json specifications.
	{
			return  "N.A.";
	}
		else
	{
		return strval($var);
	}	
}

function warning_handler($errno, $errstr)
{
    error('PHP error: ' . $errno . ', ' . $errstr);
}

?>
