# Introduction #

Bloom filter 是由 Howard Bloom 在 1970 年提出的二进制向量数据结构，它具有很好的空间和时间效率，被用来检测一个元素是不是集合中的一个成员，被广泛使用于各种海量数据排重的场景中。Mc bloom filter是一个全新的排重服务器，它采用memcached的网络层封装了bloom filter的操作，使各种语言php、java、perl、python、go、c等等，都能使用memcached的协议进行bloom filter的操作。

# Details #
## 作者 ##
新浪 [汤晓刚](http://weibo.com/11015504) [何跃](http://www.heyues.com) [胡鸿](http://weibo.com/u/1848557075)
## bloom filter 的简介 ##
[bloom\_filter的百度百科](http://baike.baidu.com/view/1912944.htm)
[Google黑板报](http://www.google.com.hk/ggblog/googlechinablog/2007/07/bloom-filter_7469.html)
[算法详细介绍](http://blog.csdn.net/v_july_v/article/details/6685894)
### mc bloom filter 的特性 ###

  * 完全采用memcached的网络层协议，创建、删除、添加、查看状态等。
  * mc bloom filter 是一个全内存的排重服务器，所有数据均放在内存中。
  * 可以在一个实例中创建多个bloom filter，在内存允许的情况，可以创建几十G大小的bloom filter，支持最高上 **百亿** 的数据排重。
  * 采用google员工写的的高性能hash算法murmurhash，保证bloom filter的hash的高速
  * 单线程版单机读写速度能达到十万次/s（同网段两台服务器多线程压力测试 服务器配置：8核 Intel(R) Xeon(R) CPU           E5620  @ 2.40GHz 12G内存）
  * 多线程版单机读写能力均能达到30万次/s(同网段两台服务器多线程压力测试 服务器配置：8核 Intel(R) Xeon(R) CPU           E5620  @ 2.40GHz 12G内存）
  * 32位、64位服务器兼容。

### mc bloom filter 的安装 ###
  * bloom filter 使用memcached网络层，依赖于libevent，先登录http://libevent.org/ 下载最新稳定版本。
```
     wget https://github.com/downloads/libevent/libevent/libevent-2.0.20-stable.tar.gz
     tar zxvf libevent-2.0.20-stable.tar.gz
     cd libevent-2.0.20-stable
     ./configure
     make && make install
```
  * 在bloom filter 的google code 上，下载mc bloom filter的最新稳定版本
```
    1.wget bloom filter的最新稳定版本
    2.修改Makefile文件，主要是修改libevent到你的目录
    3.在目录中执行make,生成mc_bloom_filter【线上版】 mc_bloom_filter_【调试版】 两个可执行文件，调试版会打很多日志
    4. nohup ./mc_bloom_filter -p12345 -d -uroot -m4000 –p/tmp/mc_bloom_filter.pid –l127.0.0.1
    日志文件就是当前目录的nohup.out文件
```
  * mc bloom filter的启动参数
| **参数** | **是否必须** | **值的含义** |
|:-------|:---------|:---------|
| p(小p)  | 是        | 监听端口，默认12345 |
| P(大P)  | 是        | pid文件的地址 |
| u(小u)  | 是        | 用哪个用户运行  |
| m(小m)  | 是        | 最大内存，单位m |
| d(小d)  | 是        | 是否用daemon后台运行 |
| l(小l)  | 是        | 监听的ip    |
|t       |否         |表示线程个数，只多线程版本有此参数，单线程无此参数，t默认为4|
|v       |否         |是否将调试的输出打印出来，如果添加这个参数，会在终端或者nohup.out中打印调试信息|

### mc bloom filter 的命令 ###

| add | add key 0 0 value\_length<br />expected\_max\_amount\_of\_elements|false\_positive\_rate<br />比如add test 0 0 13<br />1000000|0.001 表示创建一个预计存100万，误判率千分之一的bloom filter | 成功返回STORED 失败返回NOT\_STORED|
|:----|:----------------------------------------------------------------------------------------------------------------------------------------------------------------------|:--------------------------|
| set | set key 0 0 subkey\_length<br /> subkey                                                                                                                               | 成功返回STORED 失败返回NOT\_STORED |
| get | get key|subkey                                                                                                                                                        | 存在返回1，不存在啥都不返回            |
| stats | stats 查看服务器的总体状况                                                                                                                                                      | 信息列表                      |
| stats blooms | 列举所有过滤器的名称和占用内存字节大小                                                                                                                                                   | 信息列表                      |
| stats bloom key | 可以查看名字为key的bloom filter的详细信息	                                                                                                                                         | 信息列表                      |
| try | try expected\_max\_amount\_of\_elements|false\_positive\_rate<br />比如 try 100000000|0.0001 表示计算1亿个目标存储数，在误判率万分之一的情况下，<br />需要的内存大小用来预估过滤器所需的内存大小和hash函数个数             | 信息列表                      |
| setmem| setmem size(Mbytes) 用来设定当前进程可使用的内存容量，单位是m,<br />比如要设置内存1G，setmem 1024	成功返回STORED                                                                                      | 成功返回STORED，失败返回NOT\_STORED |

PHP 的使用demo
```
<?php
    $mc = new Memcache();
    $mc -> add("my_bloom","10000000|0.0001");
    $mc -> set("my_bloom","2222222");
    var_dump($mc->get("my_bloom|2222222");
?>
```