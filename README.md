# PikiwiDB
![](docs/images/pikiwidb-logo.png)
[中文](README_CN.md)

A C++20 implementation of Redis Server, use RocksDB for persist storage.(not including cluster yet)

## Requirements

* C++20
* Linux or OS X

## compile

**It is recommended to use the latest version of Ubuntu or Debian for Linux systems**

Execute compilation

If the machine's GCC version is less than 11, especially on CentOS6 or CentOS7, you need to upgrade the gcc version firstly.

Execute the following commands on CentOS:

```bash
sudo yum -y install centos-release-scl
sudo yum -y install devtoolset-11-gcc devtoolset-11-gcc-c++
scl enable devtoolset-11 bash
```

Execute this command to start compiling Pikiwidb:

```bash
./build.sh
```

Pikiwidb is compiled by default in release mode, which does not support debugging. If debugging is needed, compile in debug mode.

```bash
./clean.sh
./build.sh --debug
```

## Support module for write your own extensions
 PikiwiDB supports module now, still in progress, much work to do.
 I added three commands(ldel, skeys, hgets) for demonstration.

## Persistence: Not limited to memory
 RocksDB can be configured as backend for PikiwiDB.

## Fully compatible with redis
 You can test PikiwiDB with redis-cli, redis-benchmark, or use redis as master with PikiwiDB as slave or conversely, it also can work with redis sentinel.

## High Performance
- PikiwiDB is approximately 20-25% faster than redis if run benchmark with pipeline requests(set -P = 50 or higher).
- Average 80K requests per seconds for write, and 90K requests per seconds for read.
- Before run test, please ensure that std::list::size() is O(1), obey the C++11 standards.

Run this command, compare with redis use pipeline commands, try it.
```bash
./redis-benchmark -q -n 1000000 -P 50 -c 50
```

## Command List
#### show all supported commands list
- cmdlist

#### key commands
- type exists del expire pexpire expireat pexpireat ttl pttl persist move keys randomkey rename renamenx scan sort

#### server commands
- select dbsize bgsave save lastsave flushdb flushall client debug shutdown bgrewriteaof ping echo info monitor auth

#### string commands
- set get getrange setrange getset append bitcount bitop getbit setbit incr incrby incrbyfloat decr decrby mget mset msetnx setnx setex psetex strlen

#### list commands
- lpush rpush lpushx rpushx lpop rpop lindex llen lset ltrim lrange linsert lrem rpoplpush blpop brpop brpoplpush

#### hash commands
- hget hmget hgetall hset hsetnx hmset hlen hexists hkeys hvals hdel hincrby hincrbyfloat hscan hstrlen

#### set commands
- sadd scard srem sismember smembers sdiff sdiffstore sinter sinterstore sunion sunionstore smove spop srandmember sscan

#### sorted set commands
- zadd zcard zrank zrevrank zrem zincrby zscore zrange zrevrange zrangebyscore zrevrangebyscore zremrangebyrank zremrangebyscore zpopmin zpopmax zunionstore zinterstore

#### pubsub commands
- subscribe unsubscribe publish psubscribe punsubscribe pubsub

#### multi commands
- watch unwatch multi exec discard

#### replication commands
- sync slaveof

## Contact Us

![](docs/images/pikiwidb-wechat.png)

