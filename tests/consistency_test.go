package pikiwidb_test

import (
	"bufio"
	"context"
	"log"
	"strconv"
	"strings"
	"time"

	. "github.com/onsi/ginkgo/v2"
	. "github.com/onsi/gomega"
	"github.com/redis/go-redis/v9"

	"github.com/OpenAtomFoundation/pikiwidb/tests/util"
)

var _ = Describe("Consistency", Ordered, func() {
	var (
		ctx       = context.TODO()
		servers   []*util.Server
		followers []*redis.Client
		leader    *redis.Client
	)

	BeforeAll(func() {
		for i := 0; i < 3; i++ {
			config := util.GetConfPath(false, int64(i))
			s := util.StartServer(config, map[string]string{"port": strconv.Itoa(12000 + (i+1)*111)}, true)
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
				Expect(c.FlushDB(ctx).Err().Error()).To(Equal("ERR MOVED 127.0.0.1:12111"))
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
		// write on leader
		set, err := leader.HSet(ctx, testKey, map[string]string{
			"fa": "va",
			"fb": "vb",
			"fc": "vc",
		}).Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(set).To(Equal(int64(3)))

		// read on leader
		getall, err := leader.HGetAll(ctx, testKey).Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(getall).To(Equal(map[string]string{
			"fa": "va",
			"fb": "vb",
			"fc": "vc",
		}))

		time.Sleep(10000 * time.Millisecond)

		// read on followers
		followerChecker(followers, func(f *redis.Client) {
			getall, err := f.HGetAll(ctx, testKey).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(getall).To(Equal(map[string]string{
				"fa": "va",
				"fb": "vb",
				"fc": "vc",
			}))
		})

		// write on leader
		del, err := leader.HDel(ctx, testKey, "fb").Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(del).To(Equal(int64(1)))

		// read on leader
		getall, err = leader.HGetAll(ctx, testKey).Result()
		Expect(err).NotTo(HaveOccurred())
		Expect(getall).To(Equal(map[string]string{
			"fa": "va",
			"fc": "vc",
		}))

		time.Sleep(10000 * time.Millisecond)

		// read on followers
		followerChecker(followers, func(f *redis.Client) {
			getall, err := f.HGetAll(ctx, testKey).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(getall).To(Equal(map[string]string{
				"fa": "va",
				"fc": "vc",
			}))
		})
	})

	It("SAdd Consistency Test", func() {
		const testKey = "SetsConsistencyTestKey"
		testValues := []string{"sa", "sb", "sc", "sd"}

		{
			// write on leader
			sadd, err := leader.SAdd(ctx, testKey, testValues).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(sadd).To(Equal(int64(len(testValues))))

			// read on leader
			smembers, err := leader.SMembers(ctx, testKey).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(smembers).To(Equal(testValues))

			time.Sleep(10000 * time.Millisecond)

			// read on followers
			followerChecker(followers, func(f *redis.Client) {
				smembers, err := leader.SMembers(ctx, testKey).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(smembers).To(Equal(testValues))
			})
		}

		{
			// write on leader
			srem, err := leader.SRem(ctx, testKey, []string{"sb", "sd"}).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(srem).To(Equal(int64(2)))

			// read on leader
			smembers, err := leader.SMembers(ctx, testKey).Result()
			Expect(err).NotTo(HaveOccurred())
			Expect(smembers).To(Equal([]string{"sa", "sc"}))

			time.Sleep(10000 * time.Millisecond)

			// read on followers
			followerChecker(followers, func(f *redis.Client) {
				smembers, err := leader.SMembers(ctx, testKey).Result()
				Expect(err).NotTo(HaveOccurred())
				Expect(smembers).To(Equal([]string{"sa", "sc"}))
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

func followerChecker(fs []*redis.Client, check func(*redis.Client)) {
	for _, f := range fs {
		check(f)
	}
}
