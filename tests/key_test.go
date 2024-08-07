/*
 * Copyright (c) 2023-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

package pikiwidb_test

import (
	"context"
	"fmt"
	"log"
	"strconv"
	"time"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	"github.com/redis/go-redis/v9"

	"github.com/OpenAtomFoundation/pikiwidb/tests/util"
)

var _ = Describe("Keyspace", Ordered, func() {
	var (
		ctx    = context.TODO()
		s      *util.Server
		client *redis.Client
	)

	// BeforeAll closures will run exactly once before any of the specs
	// within the Ordered container.
	BeforeAll(func() {
		config := util.GetConfPath(false, 0)

		s = util.StartServer(config, map[string]string{"port": strconv.Itoa(7777)}, true)
		Expect(s).NotTo(Equal(nil))
	})

	// AfterAll closures will run exactly once after the last spec has
	// finished running.
	AfterAll(func() {
		err := s.Close()
		if err != nil {
			log.Println("Close Server fail.", err.Error())
			return
		}
	})

	// When running each spec Ginkgo will first run the BeforeEach
	// closure and then the subject closure.Doing so ensures that
	// each spec has a pristine, correctly initialized, copy of the
	// shared variable.
	BeforeEach(func() {
		client = s.NewClient()
		// TODO don't assert FlushDB's result, bug will fixed by issue #401
		// Expect(client.FlushDB(ctx).Err()).NotTo(HaveOccurred())
		if res := client.FlushDB(ctx); res.Err() != nil {
			fmt.Println("[Keyspace]FlushDB error: ", res.Err())
		}
		time.Sleep(2 * time.Second)
	})

	// nodes that run after the spec's subject(It).
	AfterEach(func() {
		err := client.Close()
		if err != nil {
			log.Println("Close client conn fail.", err.Error())
			return
		}
	})

	It("Set", func() {
		{
			// set px
			res, err := client.Set(ctx, "a", "a", time.Millisecond*1001).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(res).To(Equal("OK"))
			time.Sleep(time.Millisecond * 2000)

			n, err := client.Exists(ctx, "a").Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(n).To(Equal(int64(0)))
		}
		{
			// set ex
			res, err := client.Set(ctx, "a", "a", time.Second*60).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(res).To(Equal("OK"))

			n, err := client.Exists(ctx, "a").Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(n).To(Equal(int64(1)))
		}
		{
			// set xx
			res, err := client.SetXX(ctx, "a", "a", 0).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(res).To(Equal(true))

			n, err := client.Exists(ctx, "a").Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(n).To(Equal(int64(1)))
		}
		{
			// set ex xx
			res, err := client.SetXX(ctx, "a", "a", time.Second*30).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(res).To(Equal(true))

			n, err := client.Exists(ctx, "a").Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(n).To(Equal(int64(1)))
		}
		{
			// set px xx
			res, err := client.SetXX(ctx, "a", "a", time.Millisecond*1001).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(res).To(Equal(true))
			time.Sleep(time.Millisecond * 2000)

			n, err := client.Exists(ctx, "a").Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(n).To(Equal(int64(0)))
		}
		{
			// set ex nx
			res, err := client.SetNX(ctx, "a", "a", time.Second).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(res).To(Equal(true))
			time.Sleep(time.Second * 2)

			n, err := client.Exists(ctx, "a").Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(n).To(Equal(int64(0)))
		}
		{
			// set px nx
			res, err := client.SetNX(ctx, "a", "a", time.Millisecond*1001).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(res).To(Equal(true))
			time.Sleep(time.Millisecond * 2000)

			n, err := client.Exists(ctx, "a").Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(n).To(Equal(int64(0)))
		}
		{
			// set nx
			res, err := client.SetNX(ctx, "a", "a", 0).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(res).To(Equal(true))

			n, err := client.Exists(ctx, "a").Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(n).To(Equal(int64(1)))
		}
		{
			// setex
			res, err := client.SetEx(ctx, "a", "a", time.Second).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(res).To(Equal("OK"))
			time.Sleep(time.Second * 2)

			n, err := client.Exists(ctx, "a").Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(n).To(Equal(int64(0)))
		}
	})

	//TODO(dingxiaoshuai) Add more test cases.
	It("Exists", func() {
		n, err := client.Exists(ctx, "key1").Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(n).To(Equal(int64(0)))

		set := client.Set(ctx, "key1", "value1", 0)
		Expect(set.Err()).NotTo(HaveOccurred())
		Expect(set.Val()).To(Equal("OK"))

		n, err = client.Exists(ctx, "key1").Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(n).To(Equal(int64(1)))

		set = client.Set(ctx, "key2", "value2", 0)
		Expect(set.Err()).NotTo(HaveOccurred())
		Expect(set.Val()).To(Equal("OK"))

		n, err = client.Exists(ctx, "key1", "key2", "notExistKey").Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(n).To(Equal(int64(2)))

		_, err = client.Del(ctx, "key1", "key2").Result()
		Expect(err).NotTo(HaveOccurred())
	})

	It("Del", func() {
		set := client.Set(ctx, "key1", "value1", 0)
		Expect(set.Err()).NotTo(HaveOccurred())
		Expect(set.Val()).To(Equal("OK"))

		set = client.Set(ctx, "key2", "value2", 0)
		Expect(set.Err()).NotTo(HaveOccurred())
		Expect(set.Val()).To(Equal("OK"))

		n, err := client.Del(ctx, "key1", "key2", "notExistKey").Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(n).To(Equal(int64(2)))

		n, err = client.Exists(ctx, "key1", "key2").Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(n).To(Equal(int64(0)))
	})

	It("Type", func() {
		set := client.Set(ctx, "key", "value", 0)
		Expect(set.Err()).NotTo(HaveOccurred())
		Expect(set.Val()).To(Equal("OK"))

		lPush := client.LPush(ctx, "mlist", "hello")
		Expect(lPush.Err()).NotTo(HaveOccurred())
		Expect(lPush.Val()).To(Equal(int64(1)))

		sAdd := client.SAdd(ctx, "mset", "world")
		Expect(sAdd.Err()).NotTo(HaveOccurred())
		Expect(sAdd.Val()).To(Equal(int64(1)))

		Expect(client.Type(ctx, "key").Val()).To(Equal("string"))
		Expect(client.Type(ctx, "mlist").Val()).To(Equal("list"))
		Expect(client.Type(ctx, "mset").Val()).To(Equal("set"))

		Expect(client.Del(ctx, "key", "mlist", "mset").Err()).NotTo(HaveOccurred())
	})

	It("Expire", func() {
		Expect(client.Set(ctx, "key_3s", "value", 0).Val()).To(Equal("OK"))
		Expect(client.Expire(ctx, "key_3s", 3*time.Second).Val()).To(Equal(true))
		Expect(client.TTL(ctx, "key_3s").Val()).NotTo(Equal(int64(-2)))

		time.Sleep(4 * time.Second)
		Expect(client.TTL(ctx, "key_3s").Val()).To(Equal(time.Duration(-2)))
		Expect(client.Get(ctx, "key_3s").Err()).To(MatchError(redis.Nil))
		Expect(client.Exists(ctx, "key_3s").Val()).To(Equal(int64(0)))

		Expect(client.Do(ctx, "expire", "foo", "bar").Err()).To(MatchError("ERR value is not an integer or out of range"))

	})

	It("TTL", func() {
		set := client.Set(ctx, "key1", "bcd", 10*time.Minute)
		Expect(set.Err()).NotTo(HaveOccurred())
		Expect(set.Val()).To(Equal("OK"))
		Expect(client.TTL(ctx, "key1").Val()).NotTo(Equal(int64(-2)))

		get := client.Get(ctx, "key1")
		Expect(get.Err()).NotTo(HaveOccurred())
		Expect(get.Val()).To(Equal("bcd"))
		Expect(client.TTL(ctx, "key1").Val()).NotTo(Equal(int64(-2)))

		_, err := client.Del(ctx, "key1").Result()
		Expect(err).NotTo(HaveOccurred())

		set1 := client.Set(ctx, "key1", "bcd", 10*time.Minute)
		Expect(set1.Err()).NotTo(HaveOccurred())
		Expect(set1.Val()).To(Equal("OK"))
		Expect(client.TTL(ctx, "key1").Val()).NotTo(Equal(int64(-2)))

		mGet := client.MGet(ctx, "key1")
		Expect(mGet.Err()).NotTo(HaveOccurred())
		Expect(mGet.Val()).To(Equal([]interface{}{"bcd"}))

		Expect(client.TTL(ctx, "key1").Val()).NotTo(Equal(int64(-2)))

		Expect(client.Expire(ctx, "key1", 1*time.Second).Val()).To(Equal(true))
		time.Sleep(2 * time.Second)
	})

	// pikiwidb should treat numbers other than base-10 as strings
	It("base", func() {
		set := client.Set(ctx, "key", "0b1", 0)
		Expect(set.Err()).NotTo(HaveOccurred())
		Expect(set.Val()).To(Equal("OK"))

		get := client.Get(ctx, "key")
		Expect(get.Err()).NotTo(HaveOccurred())
		Expect(get.Val()).To(Equal("0b1"))

		set = client.Set(ctx, "key", "011", 0)
		Expect(set.Err()).NotTo(HaveOccurred())
		Expect(set.Val()).To(Equal("OK"))

		get = client.Get(ctx, "key")
		Expect(get.Err()).NotTo(HaveOccurred())
		Expect(get.Val()).To(Equal("011"))

		set = client.Set(ctx, "key", "0xA", 0)
		Expect(set.Err()).NotTo(HaveOccurred())
		Expect(set.Val()).To(Equal("OK"))

		get = client.Get(ctx, "key")
		Expect(get.Err()).NotTo(HaveOccurred())
		Expect(get.Val()).To(Equal("0xA"))

		del := client.Del(ctx, "key")
		Expect(del.Err()).NotTo(HaveOccurred())
	})

	It("should pexpire", func() {
		Expect(client.Set(ctx, DefaultKey, DefaultValue, 0).Val()).To(Equal(OK))
		Expect(client.PExpire(ctx, DefaultKey, 3000*time.Millisecond).Val()).To(Equal(true))
		// Expect(client.PTTL(ctx, DefaultKey).Val()).NotTo(Equal(time.Duration(-2)))

		time.Sleep(4 * time.Second)
		// Expect(client.PTTL(ctx, DefaultKey).Val()).To(Equal(time.Duration(-2)))
		Expect(client.Get(ctx, DefaultKey).Err()).To(MatchError(redis.Nil))
		Expect(client.Exists(ctx, DefaultKey).Val()).To(Equal(int64(0)))

		Expect(client.Do(ctx, "pexpire", DefaultKey, "err").Err()).To(MatchError("ERR value is not an integer or out of range"))
	})

	It("should expireat", func() {
		Expect(client.Set(ctx, DefaultKey, DefaultValue, 0).Val()).To(Equal(OK))
		Expect(client.ExpireAt(ctx, DefaultKey, time.Now().Add(time.Second*-1)).Val()).To(Equal(true))
		Expect(client.Exists(ctx, DefaultKey).Val()).To(Equal(int64(0)))

	})

	It("should expireat", func() {
		Expect(client.Set(ctx, DefaultKey, DefaultValue, 0).Val()).To(Equal(OK))
		Expect(client.ExpireAt(ctx, DefaultKey, time.Now().Add(time.Second*3)).Val()).To(Equal(true))
		Expect(client.Exists(ctx, DefaultKey).Val()).To(Equal(int64(1)))

		time.Sleep(4 * time.Second)

		Expect(client.Get(ctx, DefaultKey).Err()).To(MatchError(redis.Nil))
		Expect(client.Exists(ctx, DefaultKey).Val()).To(Equal(int64(0)))
	})

	It("should pexpirat", func() {
		Expect(client.Set(ctx, DefaultKey, DefaultValue, 0).Val()).To(Equal(OK))
		Expect(client.PExpireAt(ctx, DefaultKey, time.Now().Add(time.Second*-1)).Val()).To(Equal(true))
		Expect(client.Exists(ctx, DefaultKey).Val()).To(Equal(int64(0)))

	})

	It("should pexpirat", func() {
		Expect(client.Set(ctx, DefaultKey, DefaultValue, 0).Val()).To(Equal(OK))
		Expect(client.PExpireAt(ctx, DefaultKey, time.Now().Add(time.Second*3)).Val()).To(Equal(true))
		Expect(client.Exists(ctx, DefaultKey).Val()).To(Equal(int64(1)))

		time.Sleep(4 * time.Second)

		Expect(client.Get(ctx, DefaultKey).Err()).To(MatchError(redis.Nil))
		Expect(client.Exists(ctx, DefaultKey).Val()).To(Equal(int64(0)))
	})

	It("persist", func() {
		// return 0 if key does not exist
		Expect(client.Persist(ctx, DefaultKey).Val()).To(Equal(false))

		// return 0 if key does not have an associated timeout
		Expect(client.Set(ctx, DefaultKey, DefaultValue, 0).Val()).To(Equal(OK))
		Expect(client.Persist(ctx, DefaultKey).Val()).To(Equal(false))

		// return 1 if the timueout was set
		Expect(client.PExpireAt(ctx, DefaultKey, time.Now().Add(time.Second*3)).Val()).To(Equal(true))
		Expect(client.Persist(ctx, DefaultKey).Val()).To(Equal(true))
		time.Sleep(5 * time.Second)
		Expect(client.Exists(ctx, DefaultKey).Val()).To(Equal(int64(1)))

		Expect(client.PExpireAt(ctx, DefaultKey, time.Now().Add(time.Second*1000)).Err()).NotTo(HaveOccurred())
		Expect(client.Persist(ctx, DefaultKey).Err()).NotTo(HaveOccurred())

		// del keys
		Expect(client.PExpireAt(ctx, DefaultKey, time.Now().Add(time.Second*1)).Err()).NotTo(HaveOccurred())
		time.Sleep(2 * time.Second)
		Expect(client.Exists(ctx, DefaultKey).Val()).To(Equal(int64(0)))
	})

	It("keys", func() {
		// empty
		Expect(client.Keys(ctx, "*").Val()).To(Equal([]string{}))
		Expect(client.Keys(ctx, "dummy").Val()).To(Equal([]string{}))
		Expect(client.Keys(ctx, "dummy*").Val()).To(Equal([]string{}))

		Expect(client.Set(ctx, "a1", "v1", 0).Val()).To(Equal(OK))
		Expect(client.Set(ctx, "k1", "v1", 0).Val()).To(Equal(OK))
		Expect(client.SAdd(ctx, "k2", "v2").Val()).To(Equal(int64(1)))
		Expect(client.HSet(ctx, "k3", "k3", "v3").Val()).To(Equal(int64(1)))
		Expect(client.LPush(ctx, "k4", "v4").Val()).To(Equal(int64(1)))
		Expect(client.ZAdd(ctx, "k5", redis.Z{Score: 1, Member: "v5"}).Val()).To(Equal(int64(1)))

		// all
		Expect(client.Keys(ctx, "*").Val()).To(Equal([]string{"a1", "k1", "k2", "k3", "k4", "k5"}))

		// pattern
		Expect(client.Keys(ctx, "k*").Val()).To(Equal([]string{"k1", "k2", "k3", "k4", "k5"}))
		Expect(client.Keys(ctx, "k1").Val()).To(Equal([]string{"k1"}))

		// del keys
		Expect(client.Del(ctx, "a1", "k1", "k2", "k3", "k4", "k5").Err()).NotTo(HaveOccurred())
	})

	It("should pexpire", func() {
		Expect(client.Set(ctx, DefaultKey, DefaultValue, 0).Val()).To(Equal(OK))
		Expect(client.PExpire(ctx, DefaultKey, 3000*time.Millisecond).Val()).To(Equal(true))
		Expect(client.PTTL(ctx, DefaultKey).Val()).NotTo(Equal(time.Duration(-2)))

		time.Sleep(4 * time.Second)
		Expect(client.PTTL(ctx, DefaultKey).Val()).To(Equal(time.Duration(-2)))
		Expect(client.Get(ctx, DefaultKey).Err()).To(MatchError(redis.Nil))
		Expect(client.Exists(ctx, DefaultKey).Val()).To(Equal(int64(0)))

		Expect(client.Do(ctx, "pexpire", DefaultKey, "err").Err()).To(MatchError("ERR value is not an integer or out of range"))
	})

	PIt("should Rename", func() {
		client.Set(ctx, "mykey", "hello", 0)
		client.Rename(ctx, "mykey", "mykey1")
		client.Rename(ctx, "mykey1", "mykey2")
		Expect(client.Get(ctx, "mykey2").Val()).To(Equal("hello"))

		Expect(client.Exists(ctx, "mykey").Val()).To(Equal(int64(0)))

		client.Set(ctx, "mykey", "foo", 0)
		Expect(client.Rename(ctx, "mykey", "mykey").Val()).To(Equal(OK))

		client.Del(ctx, "mykey", "mykey2")
		client.Set(ctx, "mykey", "foo", 0)
		client.Set(ctx, "mykey2", "bar", 0)
		client.Expire(ctx, "mykey2", 100*time.Second)
		Expect(client.TTL(ctx, "mykey").Val()).To(Equal(-1 * time.Nanosecond))
		Expect(client.TTL(ctx, "mykey2").Val()).NotTo(Equal(-1 * time.Nanosecond))
		client.Rename(ctx, "mykey", "mykey2")
		Expect(client.TTL(ctx, "mykey2").Val()).To(Equal(-1 * time.Nanosecond))
	})

	PIt("should RenameNX", func() {
		client.Del(ctx, "mykey", "mykey1", "mykey2")
		client.Set(ctx, "mykey", "hello", 0)
		client.RenameNX(ctx, "mykey", "mykey1")
		client.RenameNX(ctx, "mykey1", "mykey2")
		Expect(client.Get(ctx, "mykey2").Val()).To(Equal("hello"))

		client.Set(ctx, "mykey", "foo", 0)
		client.Set(ctx, "mykey2", "bar", 0)
		Expect(client.RenameNX(ctx, "mykey", "mykey2").Val()).To(Equal(false))

		client.Set(ctx, "mykey", "foo", 0)
		Expect(client.RenameNX(ctx, "mykey", "mykey").Val()).To(Equal(false))
	})

})
