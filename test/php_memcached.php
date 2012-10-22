<?php
/**
 * test memcached client
 */
$mc = new Memcached();
$mc -> addServer('127.0.0.1','12345');
$key = "php_memcached";
$n = 10000000;
$e = 0.0001;

$res = $mc -> add($key,"$n|$e");
if(!$res){
	$mc->delete($key);
	$res = $mc -> add($key,"$n|$e");
	if(!$res){
		die("can not add key \n");
	}
}

echo "######### test set 100w ######### \n";
$start_time = microtime_float();
for($i = 0; $i< 1000000;$i++){
	$res = $mc -> set($key,$i);
	if(!$res){
		echo "set $key $i fail\n";
	}else{
		
	}
}

$end_time = microtime_float();
$used_time = $end_time - $start_time;
echo "used_time:$used_time \n";

echo "######### test get 100w ######### \n";
$start_time = microtime_float();
for($i = 0;$i< 1000000;$i++){
	$res = $mc->get("$key|$i");	
}
$end_time = microtime_float();
$used_time = $end_time - $start_time;
echo "used_time:$used_time \n";

echo "####### test multi_get ######3 \n";
for($i = 0;$i <50;$i++){
	$subkey_arr[] = "$key|$i";
}
var_dump($mc->getMulti($subkey_arr));

$mc->delete($key);

//php time funtion
function microtime_float(){
		list($usec, $sec) = explode(" ", microtime());
		return ((float)$usec + (float)$sec);
}
?>
