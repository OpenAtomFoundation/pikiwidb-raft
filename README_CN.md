# PikiwiDB
![](docs/images/pikiwidb-logo.png)
[Click me switch to English](README.en.md)

C++20 实现的增强版 Redis 服务器,使用 RocksDB 作为持久化存储引擎。(集群支持尚正在计划中)

## 环境需求

* C++20、CMake
* Linux 或 MAC OS

## 编译

**建议使用最新版本的 Ubuntu 或 Debian Linux 系统**

执行编译:

如果机器的 GCC 版本低于 11，特别是在 CentOS 6.x 或 CentOS 7.x 上，你需要先升级 GCC 版本。

在 CentOS 上执行以下命令:

```bash
sudo yum -y install centos-release-scl
sudo yum -y install devtoolset-11-gcc devtoolset-11-gcc-c++
scl enable devtoolset-11 bash
```

执行以下命令开始编译 PikiwiDB:

```bash
./build.sh
```

PikiwiDB 默认以 release 模式编译，不支持调试。如果需要调试，请以 debug 模式编译。

```bash
./clean.sh
./build.sh --debug
```

## 与 Redis 完全兼容

你可以用 Redis 的各种工具来测试 PikiwiDB，比如官方的 redis-cli, redis-benchmark。

PikiwiDB 可以和 Redis 之间进行复制，可以读取 Redis 的 rdb 文件或 aof 文件。当然，PikiwiDB 生成的 aof 或 rdb 文件也可以被 Redis 读取。

你还可以用 redis-sentinel 来实现 PikiwiDB 的高可用！

总之，PikiwiDB 与 Redis 完全兼容。

## 高性能

- PikiwiDB 性能大约比 Redis 3.2 高出 20% (使用 redis-benchmark 测试 pipeline 请求，比如设置 -P=50 或更高)
- PikiwiDB 的高性能有一部分得益于独立的网络线程处理 IO，因此和 redis 比占了便宜。但 PikiwiDB 逻辑仍然是单线程的。
- 另一部分得益于 C++ STL 的高效率（CLANG 的表现比 GCC 更好）。
- 在测试前，你要确保 std::list 的 size() 是 O(1) 复杂度，这才遵循 C++11 的标准。否则 list 相关命令不可测。

运行下面这个命令，试试和 redis 比一比~
```bash
./redis-benchmark -q -n 1000000 -P 50 -c 50
```

## 支持冷数据淘汰

是的，在内存受限的情况下，你可以让 PikiwiDB 根据简单的 LRU 算法淘汰一些 key 以释放内存。

## 主从复制，事务，RDB/AOF持久化，慢日志，发布订阅

这些特性 PikiwiDB 都有:-)

## 持久化：内存不再是上限
RocksDB 可以配置为 PikiwiDB 的持久化存储引擎，可以存储更多的数据。

## 命令列表

#### 展示 PikiwiDB 支持的所有命令

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

![](docs/images/pikiwidb-wechat-cn.png)

