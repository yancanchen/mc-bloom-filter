<?php
/**
 * test php memcache client 
 * the auto test php script
 * @heyue
 */

define('BLOOM_HOST','127.0.0.1');
define('BLOOM_PORT','12345');
define('BLOOM_KEY','1234567890123456789012345678901');
define('DEBUG',1);

//config
$key_arr = array('|'=>false,'|||'=>false,'a|b'=>false,'\sdf12|sdfsdf'=>false,'dddddddddddddddddddddddddddddddd'=>false,'a'=>true,'0'=>true,'中文'=>true,'ddddddddddddddddddddddddddddddd'=>true);//bloom name array
$n_arr = array(-1=>false,0=>false,1=>true,100000=>true,1000000=>true,10000000=>true);//target_size array
$e_arr = array(-1=>false,'0'=>false,1=>false,'0.1'=>true,'0.001'=>true,'0.0001'=>true,'0.0003'=>true,'0.00001'=>true,'0.000001'=>true); //false positive rate
$subkey_arr = array(
		''=>false,
		' '=>false,
		'1'=>true,
		'a'=>true,
		'abc'=>true,
		'中中文中文中文中文中文中文中文中文中文中文中文中文中文中文中文中文中文'=>true,
		'中中文中文中文中文中文中文中文中文中文中文中文中文中文中文中文中文中文中文'=>true,
		'中中文中文中文中文中文中文中文中文中文中文中文中文中文中文中文中文中文中文中文'=>true,
		'中中文中文中文中文中文中文中文中文中文中文中文中文中文中文中文中文中文中文中文中文'=>true,
		'sadfasdfsdfasdfasdfasdfasdf23%!@$!@$)!*@$)!@*)$!@)$@!)dsfasdfasfasfdasfasdfafasfasfafasfasfasfsafasfasdfasdfasdfasdfasdfasdfasfdasdfasdfasdfasdfasdfasfasfasfasdfasdfasdfasdfasdfasdfasdfasfasdfasdfadsfasdfasdfasdfasd'=>true,
		'sadfasdfsdfasdfasdfasdfasdf23%!@$!@$)!*@$)!@*)$!@)$@!)dsfasdfasfasfdasfasdfafasfasfafasfasfasfsafasfasdfasdfasdfasdfasdfasdfasfdasdfasdfasdfasdfasdfasfasfasfasdfasdfasdfasdfasdfasdfasdfasfasdfasdfadsfasdfasdfasdfasd1'=>true,
		'sadfasdfsdfasdfasdfasdfasdf23%!@$!@$)!*@$)!@*)$!@)$@!)dsfasdfasfasfdasfasdfafasfasfafasfasfasfsafasfasdfasdfasdfasdfasdfasdfasfdasdfasdfasdfasdfasdfasfasfasfasdfasdfasdfasdfasdfasdfasdfasfasdfasdfadsfasdfasdfasdfasd11'=>true,
		'sadfasdfsdfasdfasdfasdfasdf23%!@$!@$)!*@$)!@*)$!@)$@!)dsfasdfasfasfdasfasdfafasfasfafasfasfasfsafasfasdfasdfasdfasdfasdfasdfasfdasdfasdfasdfasdfasdfasfasfasfasdfasdfasdfasdfasdfasdfasdfasfasdfasdfadsfasdfasdfasdfasd111'=>false,
		'sadfasdfsdfasdfasdfasdfasdf23%!@$!@$)!*@$)!@*)$!@)$@!)dsfasdfasfasfdasfasdfafasfasfafasfasfasfsafasfasdfasdfasdfasdfasdfasdfasfdasdfasdfasdfasdfasdfasfasfasfasdfasdfasdfasdfasdfasdfasdfasfasdfasdfadsfasdfasdfasdfasd111sdfsadfasdfasdfasdfasdfasdfasdfasdfasdfasdfasdfasfdsdf'=>false
		);

//test n size
$n_test_arr = array(1000000,10000000,100000000);

$mc = new Memcache();
$mc -> connect(BLOOM_HOST,BLOOM_PORT);

$key = BLOOM_KEY;
$n = 1000000;
$e = 0.0001;

//test key
echo "################ key test##############\n";
foreach($key_arr as $key => $v){
	$res = $mc->add($key,"$n|$e");	
	if($res != $v){
		echo "$key\t$n\t$e add fail \n";	
	}else{
		echo "-\n";
		$mc->delete($key);
	}
}

//test n
echo "################ n test##############\n";
foreach($n_arr as $n => $v){
	$res = $mc->add($key,"$n|$e");	
	if($res != $v){
		echo "$key\t$n\t$e add fail \n";	
	}else{
		echo "-\n";
		$mc->delete($key);
	}
}
$key = BLOOM_KEY;
$n = 1000000;

//test e
echo "################ e test##############\n";
foreach($e_arr as $e=>$v){
	$res = $mc->add($key,"$n|$e");	
	if($res != $v){
		echo "$key\t$n\t$e add fail \n";	
	}else{
		echo "-\n";
		$mc->delete($key);
	}
}

$e = 0.0001;


echo "################ subkey test##############\n";
$res = $mc -> add($key,"$n|$e");
$i = 0;
foreach($subkey_arr as $subkey=>$v){
		$res = $mc -> set($key,$subkey);
		//echo "subkey:$subkey subkey_len:".strlen($subkey)." v:$v res:$res \n";
		if($res != $v){
			echo "set $subkey $v fail \r\n";	
		}else{
			echo "-\n";
			if($v){
					$get_res = $mc -> get("$key|$subkey");
					if($get_res){
						echo "-\n";
						//echo "get $key|$subkey success \r\n";
					}else{
						echo "get $key|$subkey fail \r\n";
					}
			}
		}
		echo "---------------------\n";
}
$mc ->delete($key);

//start run time test
echo "################ start 100w set test##############\n";
foreach($n_test_arr as $k=>$v){
		$start_time = microtime_float();
		$n = $v;
		$key = BLOOM_KEY;
		$e = 0.0001;
		$res = $mc -> add($key,"$n|$e");

		for($i = 0 ; $i < 1000000;$i++){
			$res = $mc -> set($key,$i);
		}

		$end_time = microtime_float();
		$use_time = $end_time - $start_time;
		echo "set_1000000_res:\tkey:$key\tn:$n\te:$e\tuse_time:{$use_time}s\n";
		$mc ->delete($key);
}

echo "################ start 100w get test##############\n";
foreach($n_test_arr as $k=>$v){
		$start_time = microtime_float();
		$n = $v;
		$key = BLOOM_KEY;
		$e = 0.0001;
		$res = $mc -> add($key,"$n|$e");

		for($i = 0 ; $i < 1000000; $i++){
				$res = $mc -> get("$key|$i");
		}

		$end_time = microtime_float();
		$use_time = $end_time - $start_time;
		echo "get_1000000_res:\tkey:$key\tn:$n\te:$e\tuse_time:{$use_time}s\n";
		$mc ->delete($key);
}

$mc -> close();
//php time funtion
function microtime_float(){
		list($usec, $sec) = explode(" ", microtime());
		return ((float)$usec + (float)$sec);
}
?>
