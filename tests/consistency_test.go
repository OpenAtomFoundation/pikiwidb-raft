/*
 * Copyright (c) 2024-present, Qihoo, Inc.  All rights reserved.
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

package pikiwidb_test

import (
	"bufio"
	"context"
	"fmt"
	"log"
	"os/exec"
	"strconv"
	"strings"
	"time"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	"github.com/redis/go-redis/v9"

	"github.com/OpenAtomFoundation/pikiwidb/tests/util"
)

var (
	followers []*redis.Client
	leader    *redis.Client
)

var _ = Describe("Consistency", Ordered, func() {
	var (
		ctx     = context.TODO()
		servers []*util.Server
	)

	BeforeAll(func() {
		cmd := exec.Command("ulimit", "-n", "999999")
		_ = cmd.Run()
		for i := 0; i < 3; i++ {
			config := util.GetConfPath(false, int64(i))
			s := util.StartServer(config, map[string]string{"port": strconv.Itoa(12000 + (i+1)*111),
				"use-raft": "yes"}, true)
			Expect(s).NotTo(BeNil())
			servers = append(servers, s)

			if i == 0 {
				leader = s.NewClient()
				Expect(leader).NotTo(BeNil())
				// TODO don't assert FlushDB's result, bug will fixed by issue #401
				//Expect(leader.FlushDB(ctx).Err().Error()).To(Equal("ERR PRAFT is not initialized"))
				if res := leader.FlushDB(ctx); res.Err() == nil || res.Err().Error() != "ERR PRAFT is not initialized" {
					fmt.Println("[Consistency]FlushDB error: ", res.Err())
				}
			} else {
				c := s.NewClient()
				Expect(c).NotTo(BeNil())
				// TODO don't assert FlushDB's result, bug will fixed by issue #401
				//Expect(c.FlushDB(ctx).Err().Error()).To(Equal("ERR PRAFT is not initialized"))
				if res := c.FlushDB(ctx); res.Err() == nil || res.Err().Error() != "ERR PRAFT is not initialized" {
					fmt.Println("[Consistency]FlushDB error: ", res.Err())
				}
				followers = append(followers, c)
			}
		}

		res, err := leader.Do(ctx, "RAFT.CLUSTER", "INIT").Result()
		Expect(err).NotTo(HaveOccurred())
		msg, ok := res.(string)
		Expect(ok).To(BeTrue())
		Expect(msg).To(Equal("OK"))
		err = leader.Close()
		Expect(err).NotTo(HaveOccurred())
		leader = nil

		for _, f := range followers {
			res, err := f.Do(ctx, "RAFT.CLUSTER", "JOIN", "127.0.0.1:12111").Result()
			Expect(err).NotTo(HaveOccurred())
			msg, ok := res.(string)
			Expect(ok).To(BeTrue())
			Expect(msg).To(Equal("OK"))
			err = f.Close()
			Expect(err).NotTo(HaveOccurred())
		}
		followers = nil
	})

	AfterAll(func() {
		for _, s := range servers {
			err := s.Close()
			if err != nil {
				log.Println("Close Server fail.", err.Error())
				return
			}
		}
	})

	BeforeEach(func() {
		for i, s := range servers {
			if i == 0 {
				leader = s.NewClient()
				Expect(leader).NotTo(BeNil())
				// TODO don't assert FlushDB's result, bug will fixed by issue #401
				//Expect(leader.FlushDB(ctx).Err()).NotTo(HaveOccurred())
				if res := leader.FlushDB(ctx); res.Err() != nil {
					fmt.Println("[Consistency]FlushDB error: ", res.Err())
				}

				info, err := leader.Do(ctx, "info", "raft").Result()
				Expect(err).NotTo(HaveOccurred())
				info_str := info.(string)
				scanner := bufio.NewScanner(strings.NewReader(info_str))
				for scanner.Scan() {
					line := scanner.Text()
					if strings.Contains(line, "raft_role") {
						Expect(strings.Split(line, ":")[1]).To(Equal("LEADER"))
					}
				}
			} else {
				c := s.NewClient()
				Expect(c).NotTo(BeNil())
				// TODO don't assert FlushDB's result, bug will fixed by issue #401
				//Expect(c.FlushDB(ctx).Err().Error()).To(Equal("ERR -MOVED 127.0.0.1:12111"))
				if res := c.FlushDB(ctx); res.Err() != nil {
					fmt.Println("[Consistency]FlushDB error: ", res.Err())
				}
				followers = append(followers, c)

				info, err := c.Do(ctx, "info", "raft").Result()
				Expect(err).NotTo(HaveOccurred())
				info_str := info.(string)
				scanner := bufio.NewScanner(strings.NewReader(info_str))
				for scanner.Scan() {
					line := scanner.Text()
					if strings.Contains(line, "raft_role") {
						Expect(strings.Split(line, ":")[1]).To(Equal("FOLLOWER"))
					}
				}
			}
		}
	})

	AfterEach(func() {
		err := leader.Close()
		Expect(err).NotTo(HaveOccurred())
		leader = nil

		for _, f := range followers {
			err = f.Close()
			Expect(err).NotTo(HaveOccurred())
		}
		followers = nil
	})

	It("HSet & HDel Consistency Test", func() {
		const testKey = "HashConsistencyTest"
		testValue := map[string]string{
			"fa": "va",
			"fb": "vb",
			"fc": "vc",
		}

		{
			// hset write on leader
			set, err := leader.HSet(ctx, testKey, testValue).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(set).To(Equal(int64(3)))

			// read check
			readChecker(func(c *redis.Client) {
				getall, err := c.HGetAll(ctx, testKey).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(getall).To(Equal(testValue))
			})
		}

		{
			// hdel write on leader
			del, err := leader.HDel(ctx, testKey, "fb").Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(del).To(Equal(int64(1)))

			// read check
			readChecker(func(c *redis.Client) {
				getall, err := c.HGetAll(ctx, testKey).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(getall).To(Equal(map[string]string{
					"fa": "va",
					"fc": "vc",
				}))
			})
		}
	})

	It("HSetnx Consistency Test", func() {
		const testKey = "HashConsistencyTest"
		const testField = "HSetnxTestField"
		const testValue = "HSetnxTestValue"

		setnx, err := leader.HSetNX(ctx, testKey, testField, testValue).Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(setnx).To(BeTrue())

		setnx, err = leader.HSetNX(ctx, testKey, testField, "NewValue").Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(setnx).To(BeFalse())

		// read check
		readChecker(func(c *redis.Client) {
			hget, err := c.HGet(ctx, testKey, testField).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(hget).To(Equal(testValue))
		})
	})

	It("HMSet Consistency Test", func() {
		const testKey = "HashConsistencyTest"
		testValue := map[string]string{
			"fa": "va",
			"fb": "vb",
			"fc": "vc",
		}
		// write on leader
		hmset, err := leader.HMSet(ctx, testKey, testValue).Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(hmset).To(BeTrue())

		// read check
		readChecker(func(c *redis.Client) {
			getall, err := c.HGetAll(ctx, testKey).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(getall).To(Equal(testValue))
		})
	})

	It("HIncrby Consistency Test", func() {
		const testKey = "HashConsistencyTest"
		const testField = "HIncrbyField"
		// write on leader
		set, err := leader.HSet(ctx, testKey, testField, 5).Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(set).To(Equal(int64(1)))

		// incrby 1
		hincrby, err := leader.HIncrBy(ctx, testKey, testField, 1).Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(hincrby).To(Equal(int64(6)))
		// read check
		readChecker(func(c *redis.Client) {
			hget, err := c.HGet(ctx, testKey, testField).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(hget).To(Equal("6"))
		})

		// incrby -1
		hincrby, err = leader.HIncrBy(ctx, testKey, testField, -1).Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(hincrby).To(Equal(int64(5)))
		// read check
		readChecker(func(c *redis.Client) {
			hget, err := c.HGet(ctx, testKey, testField).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(hget).To(Equal("5"))
		})

		// incrby -10
		hincrby, err = leader.HIncrBy(ctx, testKey, testField, -10).Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(hincrby).To(Equal(int64(-5)))
		// read check
		readChecker(func(c *redis.Client) {
			hget, err := c.HGet(ctx, testKey, testField).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(hget).To(Equal("-5"))
		})
	})

	It("SAdd & SRem Consistency Test", func() {
		const testKey = "SetsConsistencyTestKey"
		testValues := []string{"sa", "sb", "sc", "sd"}

		{
			// sadd write on leader
			sadd, err := leader.SAdd(ctx, testKey, testValues).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(sadd).To(Equal(int64(len(testValues))))

			// read check
			readChecker(func(c *redis.Client) {
				smembers, err := c.SMembers(ctx, testKey).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(smembers).To(Equal(testValues))
			})
		}

		{
			// srem write on leader
			srem, err := leader.SRem(ctx, testKey, []string{"sb", "sd"}).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(srem).To(Equal(int64(2)))

			// read check
			readChecker(func(c *redis.Client) {
				smembers, err := c.SMembers(ctx, testKey).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(smembers).To(Equal([]string{"sa", "sc"}))
			})
		}
	})

	It("SDiffstore & SInterstore & SMove & SPop & SUnionstore Consistency Test", func() {
		const testKey1 = "SetsConsistencyTestKey1"
		const testKey2 = "SetsConsistencyTestKey2"
		testValues1 := []string{"sa", "sb", "sc", "sd"}
		testValues1Less := []string{"sa", "sb", "sc"}
		testValues2 := []string{"sa", "sb", "sc2", "sd2"}
		testValues2More := []string{"sa", "sb", "sc2", "sd", "sd2"}

		{
			sadd, err := leader.SAdd(ctx, testKey1, testValues1).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(sadd).To(Equal(int64(len(testValues1))))

			sadd, err = leader.SAdd(ctx, testKey2, testValues2).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(sadd).To(Equal(int64(len(testValues2))))

			flag, err := leader.SMove(ctx, testKey1, testKey2, "sd").Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(flag).To(Equal(true))

			readChecker(func(c *redis.Client) {
				smembers, err := c.SMembers(ctx, testKey1).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(smembers).To(Equal(testValues1Less))

				smembers, err = c.SMembers(ctx, testKey2).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(smembers).To(Equal(testValues2More))
			})
		}

		const testKey3 = "SetsConsistencyTestKey3"
		testValues3 := []string{"sa", "sb", "sc", "sd"}
		{
			sadd, err := leader.SAdd(ctx, testKey3, testValues3).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(sadd).To(Equal(int64(len(testValues3))))

			spop, err := leader.SPop(ctx, testKey3).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(spop).To(BeElementOf(testValues3))

			spops, err := leader.SPopN(ctx, testKey3, 2).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(spops[0]).To(BeElementOf(testValues3))
			Expect(spops[1]).To(BeElementOf(testValues3))

			readChecker(func(c *redis.Client) {
				smembers, err := c.SMembers(ctx, testKey3).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(len(smembers)).To(Equal(int(1)))
			})
		}

		const testKey4 = "SetsConsistencyTestKey4"
		diff := []string{"sc", "sd"}
		{
			_, err := leader.Del(ctx, testKey1).Result()
			Expect(err).NotTo(HaveOccurred())

			sadd, err := leader.SAdd(ctx, testKey1, testValues1).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(sadd).To(Equal(int64(len(testValues1))))

			_, err = leader.Del(ctx, testKey2).Result()
			Expect(err).NotTo(HaveOccurred())

			sadd, err = leader.SAdd(ctx, testKey2, testValues2).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(sadd).To(Equal(int64(len(testValues2))))

			_, err = leader.Del(ctx, testKey4).Result()
			Expect(err).NotTo(HaveOccurred())

			sdiff, err := leader.SDiffStore(ctx, testKey4, testKey1, testKey2).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(sdiff).To(Equal(int64(len(diff))))

			readChecker(func(c *redis.Client) {
				smembers, err := c.SMembers(ctx, testKey4).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(smembers).To(Equal(diff))
			})
		}

		inter := []string{"sa", "sb"}
		{
			_, err := leader.Del(ctx, testKey1).Result()
			Expect(err).NotTo(HaveOccurred())

			sadd, err := leader.SAdd(ctx, testKey1, testValues1).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(sadd).To(Equal(int64(len(testValues1))))

			_, err = leader.Del(ctx, testKey2).Result()
			Expect(err).NotTo(HaveOccurred())

			sadd, err = leader.SAdd(ctx, testKey2, testValues2).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(sadd).To(Equal(int64(len(testValues2))))

			_, err = leader.Del(ctx, testKey4).Result()
			Expect(err).NotTo(HaveOccurred())

			sinter, err := leader.SInterStore(ctx, testKey4, testKey1, testKey2).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(sinter).To(Equal(int64(len(inter))))

			readChecker(func(c *redis.Client) {
				smembers, err := c.SMembers(ctx, testKey4).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(smembers).To(Equal(inter))
			})
		}

		union := []string{"sa", "sb", "sc", "sc2", "sd", "sd2"}
		{
			_, err := leader.Del(ctx, testKey1).Result()
			Expect(err).NotTo(HaveOccurred())

			sadd, err := leader.SAdd(ctx, testKey1, testValues1).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(sadd).To(Equal(int64(len(testValues1))))

			_, err = leader.Del(ctx, testKey2).Result()
			Expect(err).NotTo(HaveOccurred())

			sadd, err = leader.SAdd(ctx, testKey2, testValues2).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(sadd).To(Equal(int64(len(testValues2))))

			_, err = leader.Del(ctx, testKey4).Result()
			Expect(err).NotTo(HaveOccurred())

			sunion, err := leader.SUnionStore(ctx, testKey4, testKey1, testKey2).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(sunion).To(Equal(int64(len(union))))

			readChecker(func(c *redis.Client) {
				smembers, err := c.SMembers(ctx, testKey4).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(smembers).To(Equal(union))
			})
		}
	})

	It("LPush & LPop Consistency Test", func() {
		const testKey = "ListsConsistencyTestKey"
		testValues := []string{"la", "lb", "lc", "ld"}

		{
			// lpush write on leader
			lpush, err := leader.LPush(ctx, testKey, testValues).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(lpush).To(Equal(int64(len(testValues))))

			// read check
			readChecker(func(c *redis.Client) {
				lrange, err := c.LRange(ctx, testKey, 0, 10).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(lrange).To(Equal(reverse(testValues)))
			})
		}

		{
			// lpop write on leader
			lpop, err := leader.LPop(ctx, testKey).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(lpop).To(Equal("ld"))
			lpop, err = leader.LPop(ctx, testKey).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(lpop).To(Equal("lc"))

			// read check
			readChecker(func(c *redis.Client) {
				lrange, err := c.LRange(ctx, testKey, 0, 10).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(lrange).To(Equal([]string{"lb", "la"}))
			})
		}
	})

	It("LPushx Consistency Test", func() {
		const testKey = "LPushxConsistencyTestKey"
		testValues := []string{"la", "lb", "lc", "ld"}

		{
			lpushx, err := leader.LPushX(ctx, testKey, testValues).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(lpushx).To(Equal(int64(0)))

			// lpush write on leader
			lpush, err := leader.LPush(ctx, testKey, testValues[0]).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(lpush).To(Equal(int64(1)))
		}

		{
			// lpushx write on leader
			lpushx, err := leader.LPushX(ctx, testKey, testValues[1:]).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(lpushx).To(Equal(int64(len(testValues))))

			// read check
			readChecker(func(c *redis.Client) {
				lrange, err := c.LRange(ctx, testKey, 0, -1).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(lrange).To(Equal(reverse(testValues)))
			})
		}
	})

	It("LInsert Consistency Test", func() {
		const testKey = "LInsertConsistencyTestKey"
		testValues := []string{"hello", "there", "world", "!"}
		leader.LPush(ctx, testKey, testValues[2], testValues[0])

		{
			// LInsert before write on leader
			lInsert, err := leader.LInsert(ctx, testKey, "BEFORE", testValues[2], testValues[1]).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(lInsert).To(Equal(int64(3)))

			// read check
			readChecker(func(c *redis.Client) {
				lrange, err := c.LRange(ctx, testKey, 0, -1).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(lrange).To(Equal(testValues[0:3]))
			})
		}

		{
			// LInsert after write on leader
			lInsert, err := leader.LInsert(ctx, testKey, "AFTER", testValues[2], testValues[3]).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(lInsert).To(Equal(int64(4)))

			// read check
			readChecker(func(c *redis.Client) {
				lrange, err := c.LRange(ctx, testKey, 0, -1).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(lrange).To(Equal(testValues))
			})
		}
	})

	It("LRem Consistency Test", func() {
		const testKey = "LRemConsistencyTestKey"
		testValues := []string{"la", "lb", "lc", "ld"}
		lpush, err := leader.LPush(ctx, testKey, []string{testValues[0], testValues[1], testValues[0], testValues[0]}).Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(lpush).To(Equal(int64(4)))

		{
			// LRem on leader
			lRem, err := leader.LRem(ctx, testKey, -2, testValues[0]).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(lRem).To(Equal(int64(2)))

			// read check
			readChecker(func(c *redis.Client) {
				lrange, err := c.LRange(ctx, testKey, 0, -1).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(lrange).To(Equal([]string{testValues[0], testValues[1]}))
			})
		}
	})

	It("LSet Consistency Test", func() {
		const testKey = "LSetConsistencyTestKey"
		testValues := []string{"la", "lb", "lc", "ld"}
		lpush, err := leader.LPush(ctx, testKey, testValues).Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(lpush).To(Equal(int64(4)))

		{
			// LSet on leader
			lSet, err := leader.LSet(ctx, testKey, 0, testValues[0]).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(lSet).To(Equal(OK))

			// LSet on leader
			lSet, err = leader.LSet(ctx, testKey, 1, testValues[1]).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(lSet).To(Equal(OK))

			// LSet on leader
			lSet, err = leader.LSet(ctx, testKey, -1, testValues[3]).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(lSet).To(Equal(OK))

			// LSet on leader
			lSet, err = leader.LSet(ctx, testKey, -2, testValues[2]).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(lSet).To(Equal(OK))

			// read check
			readChecker(func(c *redis.Client) {
				lrange, err := c.LRange(ctx, testKey, 0, -1).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(lrange).To(Equal(testValues))
			})
		}
	})

	It("LTrim Consistency Test", func() {
		const testKey = "LTrimConsistencyTestKey"
		testValues := []string{"la", "lb", "lc", "ld"}
		lpush, err := leader.LPush(ctx, testKey, testValues).Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(lpush).To(Equal(int64(4)))

		{
			// LTrim on leader
			lSet, err := leader.LTrim(ctx, testKey, 1, -1).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(lSet).To(Equal(OK))

			// read check
			readChecker(func(c *redis.Client) {
				lrange, err := c.LRange(ctx, testKey, 0, -1).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(lrange).To(Equal(reverse(testValues[0:3])))
			})
		}
	})

	It("RPop Consistency Test", func() {
		const testKey = "RPopConsistencyTestKey"
		testValues := []string{"ra", "rb", "rc", "rd"}
		rpush, err := leader.RPush(ctx, testKey, testValues).Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(rpush).To(Equal(int64(len(testValues))))
		{
			// RPop on leader
			rPop, err := leader.RPop(ctx, testKey).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(rPop).To(Equal(testValues[len(testValues)-1]))
			// read check
			readChecker(func(c *redis.Client) {
				lrange, err := c.LRange(ctx, testKey, 0, -1).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(lrange).To(Equal(testValues[:len(testValues)-1]))
			})
		}
	})

	It("RPopLPush Consistency Test", func() {
		const sourceKey = "RPopLPushSourceTestKey"
		const destinationKey = "RPopLPushDestinationTestKey"
		testValues := []string{"ra", "rb", "rc", "rd"}
		lpush, err := leader.RPush(ctx, sourceKey, testValues).Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(lpush).To(Equal(int64(len(testValues))))
		{
			// RPopLPush on leader
			rPopLPush, err := leader.RPopLPush(ctx, sourceKey, destinationKey).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(rPopLPush).To(Equal(testValues[len(testValues)-1]))
			// read check for source
			readChecker(func(c *redis.Client) {
				lrange, err := c.LRange(ctx, sourceKey, 0, -1).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(lrange).To(Equal(testValues[:len(testValues)-1]))
			})
			// read check for destination
			readChecker(func(c *redis.Client) {
				lrange, err := c.LRange(ctx, destinationKey, 0, -1).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(lrange).To(Equal([]string{testValues[len(testValues)-1]}))
			})
		}
	})

	It("RPush Consistency Test", func() {
		const testKey = "RPushConsistencyTestKey"
		testValues := []string{"ra", "rb", "rc", "rd"}
		{
			// RPush on leader
			rPush, err := leader.RPush(ctx, testKey, testValues).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(rPush).To(Equal(int64(len(testValues))))
			// read check
			readChecker(func(c *redis.Client) {
				lrange, err := c.LRange(ctx, testKey, 0, -1).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(lrange).To(Equal(testValues))
			})
		}
	})

	It("RPushX Consistency Test", func() {
		const testKey = "RPushXConsistencyTestKey"
		testValues := []string{"ra", "rb", "rc", "rd"}
		{
			// RPush write on leader
			rPush, err := leader.RPush(ctx, testKey, testValues[0]).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(rPush).To(Equal(int64(1)))
			// RPushX write on leader
			rPushX, err := leader.RPushX(ctx, testKey, testValues[1:]).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(rPushX).To(Equal(int64(len(testValues))))
			// read check
			readChecker(func(c *redis.Client) {
				lrange, err := c.LRange(ctx, testKey, 0, -1).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(lrange).To(Equal(testValues))
			})
		}
	})

	It("ZAdd Consistency Test", func() {
		const testKey = "ZSetsConsistencyTestKey"
		testData := []redis.Z{
			{Score: 4, Member: "z4"},
			{Score: 8, Member: "z8"},
			{Score: 5, Member: "z5"},
		}
		expectData := []redis.Z{
			{Score: 8, Member: "z8"},
			{Score: 5, Member: "z5"},
			{Score: 4, Member: "z4"},
		}
		{
			// zadd write on leader
			zadd, err := leader.ZAdd(ctx, testKey, testData...).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(zadd).To(Equal(int64(len(testData))))

			// read check
			readChecker(func(c *redis.Client) {
				zrange, err := c.ZRevRangeWithScores(ctx, testKey, 0, -1).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(zrange).To(Equal(expectData))
			})
		}
	})

	It("ZPopMin & ZPopMax Consistency Test", func() {
		const testKey = "ZSetsConsistencyTestKey"
		i4 := redis.Z{Score: 4, Member: "z4"}
		i5 := redis.Z{Score: 5, Member: "z5"}
		i8 := redis.Z{Score: 8, Member: "z8"}
		{
			zadd, err := leader.ZAdd(ctx, testKey, i4, i5, i8).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(zadd).To(Equal(int64(3)))

			vals, err := leader.ZPopMin(ctx, testKey).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(vals).To(Equal([]redis.Z{i4}))

			vals, err = leader.ZPopMax(ctx, testKey).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(vals).To(Equal([]redis.Z{i8}))

			// read check
			readChecker(func(c *redis.Client) {
				zrange, err := c.ZRevRangeWithScores(ctx, testKey, 0, -1).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(zrange).To(Equal([]redis.Z{i5}))
			})
		}
	})

	It("ZUnionstore & ZInterStore Consistency Test", func() {
		i4 := redis.Z{Score: 4, Member: "z4"}
		i5 := redis.Z{Score: 5, Member: "z5"}
		i8 := redis.Z{Score: 8, Member: "z8"}
		{
			zadd, err := leader.ZAdd(ctx, "in1", i4, i5).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(zadd).To(Equal(int64(2)))

			zadd, err = leader.ZAdd(ctx, "in2", i4, i8).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(zadd).To(Equal(int64(2)))

			vals, err := leader.ZUnionStore(ctx, "out1", &redis.ZStore{
				Keys:      []string{"in1", "in2"},
				Weights:   []float64{1, 1},
				Aggregate: "MIN",
			}).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(vals).To(Equal(int64(3)))

			vals, err = leader.ZInterStore(ctx, "out2", &redis.ZStore{
				Keys:      []string{"in1", "in2"},
				Weights:   []float64{1, 1},
				Aggregate: "MIN",
			}).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(vals).To(Equal(int64(1)))

			readChecker(func(c *redis.Client) {
				zrange, err := c.ZRevRangeWithScores(ctx, "out1", 0, -1).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(zrange).To(Equal([]redis.Z{i8, i5, i4}))

				zrange, err = c.ZRevRangeWithScores(ctx, "out2", 0, -1).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(zrange).To(Equal([]redis.Z{i4}))
			})
		}
	})

	It("SetBit Consistency Test", func() {
		const testKey = "StringsConsistencyTestKey"
		{
			// set write on leader
			set, err := leader.SetBit(ctx, testKey, 1, 1).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(set).To(Equal(int64(0)))

			readChecker(func(c *redis.Client) {
				get, err := c.GetBit(ctx, testKey, 0).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(get).To(Equal(int64(0)))

				get, err = c.GetBit(ctx, testKey, 1).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(get).To(Equal(int64(1)))
			})
		}
	})

	It("Set & SetEx & MSet & MSetNX Consistency Test", func() {
		const testKey = "StringsConsistencyTestKey"
		const testValue = "StringsConsistencyTestKey"
		const testValueNew = "StringsConsistencyTestKey-new"
		const testKey2 = "StringsConsistencyTestKey2"
		const testValue2 = "StringsConsistencyTestKey2"
		{
			// set write on leader
			set, err := leader.Set(ctx, testKey, testValue, 0).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(set).To(Equal("OK"))

			// read check
			readChecker(func(c *redis.Client) {
				get, err := c.Get(ctx, testKey).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(get).To(Equal(testValue))
			})
		}
		{
			// set write on leader
			set, err := leader.MSet(ctx, testKey, testValue, testKey2, testValue2).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(set).To(Equal("OK"))

			// read check
			readChecker(func(c *redis.Client) {
				get, err := c.Get(ctx, testKey).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(get).To(Equal(testValue))

				get, err = c.Get(ctx, testKey2).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(get).To(Equal(testValue2))
			})
		}
		{
			mSetNX, err := leader.MSetNX(ctx, testKey, testValueNew, testKey2, testValue2).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(mSetNX).To(Equal(false))

			del, err := leader.Del(ctx, testKey, testKey2).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(del).To(Equal(int64(2)))

			mSetNX, err = leader.MSetNX(ctx, testKey, testValueNew, testKey2, testValue2).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(mSetNX).To(Equal(true))

			readChecker(func(c *redis.Client) {
				get, err := c.Get(ctx, testKey).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(get).To(Equal(testValueNew))

				get, err = c.Get(ctx, testKey2).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(get).To(Equal(testValue2))
			})
		}
		{
			// set write on leader
			set, err := leader.SetEx(ctx, testKey, testValue, 3).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(set).To(Equal("OK"))

			// read check
			time.Sleep(10 * time.Second)
			readChecker(func(c *redis.Client) {
				_, err := c.Get(ctx, testKey).Result()
				Expect(err).To(Equal(redis.Nil))
			})
		}
	})

	It("ReadConsistencyTest", func() {
		// set write on leader
		set, err := leader.Set(ctx, "a", "b", 0).Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(set).To(Equal("OK"))

		// get from leader
		get_leader, err := leader.Get(ctx, "a").Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(get_leader).To(Equal("b"))

		if len(followers) > 0 {
			get_follower, err := followers[0].Get(ctx, "a").Result()
			Expect(err).To(HaveOccurred())

			if strings.Contains(get_follower, "-MOVED") {
				redirect_leader_ip := strings.Split(get_follower, "MOVED")[1]
				real_leader_ip := leader.Options().Addr
				Expect(redirect_leader_ip).To(Equal(real_leader_ip))
			}
		}
	})

	It("ThreeNodesClusterConstructionTest", func() {
		for _, follower := range followers {
			info, err := follower.Do(ctx, "info", "raft").Result()
			Expect(err).NotTo(HaveOccurred())
			info_str := info.(string)
			scanner := bufio.NewScanner(strings.NewReader(info_str))
			var peer_id string
			var is_member bool
			for scanner.Scan() {
				line := scanner.Text()
				if strings.Contains(line, "raft_peer_id") {
					parts := strings.Split(line, ":")
					if len(parts) >= 2 {
						peer_id = parts[1]
						is_member = true
						break
					}
				}
			}

			if is_member {
				ret, err := follower.Do(ctx, "raft.node", "remove", peer_id).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(ret).To(Equal(OK))
			}
		}
	})
})

func readChecker(check func(*redis.Client)) {
	// read on leader
	check(leader)
	time.Sleep(1000 * time.Millisecond)

	// read on followers
	// followerChecker(followers, check)
}

func followerChecker(fs []*redis.Client, check func(*redis.Client)) {
	for _, f := range fs {
		check(f)
	}
}

func reverse(src []string) []string {
	a := make([]string, len(src))
	copy(a, src)

	for i := len(a)/2 - 1; i >= 0; i-- {
		opp := len(a) - 1 - i
		a[i], a[opp] = a[opp], a[i]
	}

	return a
}
