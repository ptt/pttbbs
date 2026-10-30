package importer

import (
	"strings"
	"sync"

	"pttbbs/post/model"
)

// UserResolver finds the accounts of the users named in legacy records,
// which only have user IDs (or, for anonymous posts, the usernum).
type UserResolver interface {
	// LookupUserid returns the usernum and firstlogin of the account
	// that now has userid.
	LookupUserid(userid string) (usernum int, firstlogin uint32, ok bool)
	// LookupUsernum returns the userid and firstlogin of the account
	// that now uses slot usernum.
	LookupUsernum(usernum int) (userid string, firstlogin uint32, ok bool)
}

// ResolvePersonID returns the person ID of whoever did something at time t as userid
// (or, when usernum > 0, as the account in slot usernum): the account that
// has that name (or slot) now, if it already existed at t. Otherwise the
// account has been deleted (and maybe its name or slot reused), so the person ID
// is unknown (0).
func ResolvePersonID(users UserResolver, userid string, usernum int, t int64) model.PersonID {
	if users == nil {
		return 0
	}
	var firstlogin uint32
	var ok bool
	if usernum > 0 {
		_, firstlogin, ok = users.LookupUsernum(usernum)
	} else {
		if userid == "" || strings.ContainsAny(userid, ".@ ") {
			return 0 // not a local user
		}
		usernum, firstlogin, ok = users.LookupUserid(userid)
	}
	if !ok || (t > 0 && int64(firstlogin) > t) {
		return 0
	}
	return model.MakePersonID(usernum, firstlogin)
}

type userEntry struct {
	userid     string
	usernum    int
	firstlogin uint32
	ok         bool
}

// cachedResolver remembers the lookups of a UserResolver, as the same users
// appear many times in a board. It is safe for concurrent use.
type cachedResolver struct {
	users     UserResolver
	mu        sync.Mutex
	byUserid  map[string]userEntry
	byUsernum map[int]userEntry
}

// NewCachedResolver wraps users (which may be nil) with a cache.
func NewCachedResolver(users UserResolver) UserResolver {
	if users == nil {
		return nil
	}
	if _, ok := users.(*cachedResolver); ok {
		return users
	}
	return &cachedResolver{
		users:     users,
		byUserid:  make(map[string]userEntry),
		byUsernum: make(map[int]userEntry),
	}
}

func (c *cachedResolver) LookupUserid(userid string) (int, uint32, bool) {
	key := strings.ToLower(userid)
	c.mu.Lock()
	e, hit := c.byUserid[key]
	c.mu.Unlock()
	if !hit {
		e.usernum, e.firstlogin, e.ok = c.users.LookupUserid(userid)
		c.mu.Lock()
		c.byUserid[key] = e
		c.mu.Unlock()
	}
	return e.usernum, e.firstlogin, e.ok
}

func (c *cachedResolver) LookupUsernum(usernum int) (string, uint32, bool) {
	c.mu.Lock()
	e, hit := c.byUsernum[usernum]
	c.mu.Unlock()
	if !hit {
		e.userid, e.firstlogin, e.ok = c.users.LookupUsernum(usernum)
		c.mu.Lock()
		c.byUsernum[usernum] = e
		c.mu.Unlock()
	}
	return e.userid, e.firstlogin, e.ok
}

// ConvertParsedComments turns the comments parsed from a legacy article into
// comments and votes (the last 推 or 噓 of each user counts), resolving the
// person IDs of their authors as of the time of each comment.
func ConvertParsedComments(parsed []*ParsedComment, users UserResolver) (comments []*model.Comment, votes []*model.VoteRecord, upvotes, downvotes int) {
	voteOf := make(map[string]*model.VoteRecord)
	for _, pc := range parsed {
		personID := ResolvePersonID(users, pc.CreatorName, 0, pc.CTime)
		vote := model.VoteNeutral
		if pc.CommentType == "推" || pc.LegacyType == model.LegacyTypePush {
			vote = model.VoteUp
		} else if pc.CommentType == "噓" || pc.LegacyType == model.LegacyTypeBoo {
			vote = model.VoteDown
		}
		if vote != model.VoteNeutral {
			if v, ok := voteOf[pc.CreatorName]; ok {
				v.Vote, v.PersonID, v.CreatedAt = vote, personID, pc.CTime
			} else {
				v = &model.VoteRecord{PersonName: pc.CreatorName, PersonID: personID, Vote: vote, CreatedAt: pc.CTime}
				voteOf[pc.CreatorName] = v
				votes = append(votes, v)
			}
		}
		comments = append(comments, &model.Comment{
			CreatorName:   pc.CreatorName,
			CreatorID:     personID,
			Content:       pc.Content,
			IP:            pc.IP,
			CreatedAt:     pc.CTime,
			LastCommentAt: pc.LastCTime,
			LegacyType:    pc.LegacyType,
		})
	}
	for _, v := range votes {
		if v.Vote == model.VoteUp {
			upvotes++
		} else {
			downvotes++
		}
	}
	return comments, votes, upvotes, downvotes
}
