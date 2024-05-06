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
				Expect(leader.FlushDB(ctx).Err()).NotTo(HaveOccurred())
			} else {
				c := s.NewClient()
				Expect(c).NotTo(BeNil())
				Expect(c.FlushDB(ctx).Err()).NotTo(HaveOccurred())
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
				Expect(leader.FlushDB(ctx).Err()).NotTo(HaveOccurred())
			} else {
				c := s.NewClient()
				Expect(c).NotTo(BeNil())
				//Expect(c.FlushDB(ctx).Err().Error()).To(Equal("ERR MOVED 127.0.0.1:12111"))
				followers = append(followers, c)
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

	It("Set Consistency Test", func() {
		const testKey = "StringsConsistencyTestKey"
		const testValue = "StringsConsistencyTestKey"
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
	time.Sleep(10000 * time.Millisecond)

	// read on followers
	followerChecker(followers, check)
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
