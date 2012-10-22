#/bin/sh
PROC=mc_bloom_filter

killall ${PROC}
make && nohup ./${PROC} -p12345 -d -uroot -m4000 -P${PROC}.pid

#gdb --pid=`pidof ${PROC}`
ps aux|grep ${PROC}

