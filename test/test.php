<?php
	for($i = 0;$i<100;$i++){
		$mc = new Memcache();
		$mc -> connect('127.0.0.1','12345');
		$num = 1000*$i;
		$res = $mc -> add("key$i","$num|0.0001");
		var_dump($res);
	}
//	$res = $mc -> add($argv[1],$argv[2],$argv[3]);
	var_dump($res);
	/**
	for($i = 0; $i< 100000;$i++){
		$mc -> set("hema",$i);
		$res = $mc -> get("hema|$i");
		//echo $i."--------".$res."\n";
	}
	*/
?>
