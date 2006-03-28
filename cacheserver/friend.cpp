#define NDEBUG
#include <algorithm>
#include <cassert>

// for some constant and type
#include "bbs.h"

/* for each login of user, 
 * input: my index, friend[MAX_FRIEND] of uid, reject[MAX_REJECT] of uid,
 * for all my relation,
 * output: his index, his uid, relation to me, relation to him
 */
/* 除了 user 及 utmp 之外, 全部的 ref index 都是雙向的, 確保 insert & delete O(1) */
/* 當沒有人 refer 時則 resource recycle */

typedef int Uid;
typedef int Idx;


struct Relation {
    Uid him;
    short him_offset;

    Relation(Uid _him, short _him_offset=-1) :him(_him),him_offset(_him_offset) {}
};

template<class T>
struct freelist {
    static const int KEEP = 64;
    static T* list[8][KEEP]; // 2^0~2^7
    static int tail[8];

#define IS_2xxN(a)      (a && (a&(a-1))==0)
    static T* alloc(int n) {
	assert(n>0);
	if(n<256 && IS_2xxN(n) && sizeof(T)*n<65536) {
	    int t=n;
	    int slot;
	    for(slot=0; t>1; t/=2)
		slot++;
	    assert(0<=slot && slot<8);
	    if(tail[slot]) {
		return list[slot][--tail[slot]];
	    }
	}
	return (T*)malloc(sizeof(T)*n);
    }
    static void free(T* p, int n) {
	assert(n>0);
	if(n<256 && IS_2xxN(n) && sizeof(T)*n<65536) {
	    int t=n;
	    int slot;
	    for(slot=0; t>1; t/=2)
		slot++;
	    assert(0<=slot && slot<8);
	    if(tail[slot]<KEEP) {
		list[slot][tail[slot]++]=p;
		return;
	    }
	}
	::free(p);
    }
};

template<class T> T*  freelist<T>::list[8][KEEP];
template<class T> int freelist<T>::tail[8]={0};

template<class T,int MIN_ROOM = 8, class S = int>
struct myvector {
    // 大致上想要 STL vector, 但 STL 的 capacity 只會增加不會縮小
    // (後來發現, 以 online friend 來說, capacity 不會縮小其實沒什麼影響)
    // 此外, pointer 在 64bit 機器上要 8bytes, basic overhead 8*3 bytes,
    // 但我的資料量沒那麼大, 改用 S(int or short) 存 size & capacity 比較省
    T *base;
    S room, n;

    myvector() :base(0),room(0),n(0) {}
    ~myvector() {
	clear();
    }
    S append(T data) {
	if(room<n+1)
	    resizefor(n+1);
	base[n++]=data;
	return n-1;
    }
    void pop_back() {
	assert(n>0);
	n--;
	resizefor(n);
    }
    void clear() {
	n=0;
	resizefor(n);
    }
    /*
    T& operator[](int idx) {
	return base[idx];
    }
    */

    void resizefor(S size) {
	assert(size>=n);
	if(size==0) {
	    if(base) freelist<T>::free(base, room);
	    base=0;
	    room=0;
	} else {
	    S origroom=room;
	    if(room==0)
		room=MIN_ROOM;
	    while(room<size) room=S(room*2);
	    if(size<MIN_ROOM) size=MIN_ROOM;
	    while(room/2>size) room=S(room/2);
	    if(room!=origroom || base==0) {
		//base=(T*)realloc(base, sizeof(T)*room);
		T* tmp=freelist<T>::alloc(room);
		assert(tmp);
		if(n>0)
		    memcpy(tmp, base, sizeof(T)*n);
		if(base!=0)
		    freelist<T>::free(base, origroom);
		base=tmp;
	    }
	    assert(base);
	}
    }
};

template<class R,class B>
struct RelationList: public myvector<Relation, 8, short> {
    RelationList() :myvector<Relation, 8, short>() {}
    void add(Uid me, Uid him) {
	RelationList<B,R>& bl=R::backlist(him);
	short me_offset=append(Relation(him));
       	short him_offset=bl.append(Relation(me,me_offset));

	setbackoffset(me_offset,him_offset);
	assert(bl.base[him_offset].him==me);
	assert(bl.base[him_offset].him_offset==me_offset);
    }
    void deleteall(Uid me) {
	for(int i=0; i<n; i++) {
	    RelationList<B,R>& bl=R::backlist(base[i].him);
	    assert(bl.base[base[i].him_offset].him==me);
	    assert(bl.base[base[i].him_offset].him_offset==i);
	    bl.delete_half(base[i].him_offset);
	    //try_recycle(base[i].him); // dirty
	}
	clear();
    }
    private:
    void setbackoffset(short which,short offset) {
	assert(0<=which && which<n);
	base[which].him_offset=offset;
    }
    void delete_half(short offset) {
	assert(0<=offset && offset<n);
	if(offset<n-1) {
	    base[offset]=base[n-1];
	    R::backlist(base[offset].him).setbackoffset(base[offset].him_offset,offset);
	}
	pop_back();
    }
    friend class RelationList<B,R>;
};

struct Like;
struct Likeby;
struct Hate;
struct Hateby;
struct Like: public Relation {
    Like(Uid _him, short _him_offset=-1) :Relation(_him,_him_offset) {}
    static RelationList<Likeby,Like>& backlist(Uid him);
};
struct Likeby: public Relation {
    Likeby(Uid _him, short _him_offset=-1) :Relation(_him,_him_offset) {}
    static RelationList<Like,Likeby>& backlist(Uid him);
};
struct Hate: public Relation {
    Hate(Uid _him, short _him_offset=-1) :Relation(_him,_him_offset) {}
    static RelationList<Hateby,Hate>& backlist(Uid him);
};
struct Hateby: public Relation {
    Hateby(Uid _him, short _him_offset=-1) :Relation(_him,_him_offset) {}
    static RelationList<Hate,Hateby>& backlist(Uid him);
};


struct Utmp {
    Utmp() {
	for(int i=0; i<USHM_SIZE; i++)
	    utmp[i]=-1;
    }
    /*
    Uid& operator[](int idx) {
	return utmp[idx];
    }
    */
    public:
    Uid utmp[USHM_SIZE];
};
static Utmp utmp;

struct BBSUser {
    Uid me;
    int online;
    /* assume utmplist is short, so just use plain vector and linear search */
    myvector<int,2> utmplist;

    RelationList<Like,Likeby> like;
    RelationList<Hate,Hateby> hate;
    RelationList<Likeby,Like> likeby;
    RelationList<Hateby,Hate> hateby;

    BBSUser() :me(-1),online(0),utmplist(),like(),hate(),likeby(),hateby() {}
    BBSUser(Uid uid) :me(uid),online(0),utmplist(),like(),hate(),likeby(),hateby() {}

    void login(int utmpidx, const Uid likehim[MAX_FRIEND], const Uid hatehim[MAX_REJECT]) {
	if(online>0) {
	    /* multiple login 的話, 以最後一次的 like/hate 為準 */
	    like.deleteall(me);
	    hate.deleteall(me);
	}
	utmp.utmp[utmpidx]=me;
	utmplist.append(utmpidx);
	online++;
	assert(online==utmplist.n);
	for(int i=0; i<MAX_FRIEND && likehim[i]; i++)
	    like.add(me, likehim[i]);
	for(int i=0; i<MAX_REJECT && hatehim[i]; i++)
	    hate.add(me, hatehim[i]);
    }

    void logout(int utmpidx) {
	assert(utmp.utmp[utmpidx]==me);
	assert(online==utmplist.n);
	for(int i=0; i<utmplist.n; i++)
	    if(utmplist.base[i]==utmpidx) {
		utmplist.base[i]=utmplist.base[utmplist.n-1];
		utmplist.pop_back();
		break;
	    }
	utmp.utmp[utmpidx]=-1;
	online--;
	assert(online==utmplist.n);
	if(online==0) {
	    like.deleteall(me);
	    hate.deleteall(me);
	}
    }
    bool isfree() const {
	return online==0 && like.n==0 && hate.n==0 && likeby.n==0 && hateby.n==0;
    }
};

struct UserList {
    BBSUser users[MAX_USERS];

    UserList() {
	for(int i=0; i<MAX_USERS; i++)
	    users[i].me=i;
    }
    void login(Uid uid, Idx idx, const Uid likehim[MAX_FRIEND], const Uid hatehim[MAX_REJECT]) {
	assert(uid<MAX_USERS);
	assert(idx<USHM_SIZE);
	/* 由於不會收到 logout event, 因此 logout 只發生在 utmp override */
	if(utmp.utmp[idx]!=-1) users[utmp.utmp[idx]].logout(idx);
	users[uid].login(idx, likehim, hatehim);
    }
};

struct UserList userlist;
RelationList<Likeby,Like>& Like::backlist(Uid him) { return userlist.users[him].likeby; }
RelationList<Like,Likeby>& Likeby::backlist(Uid him) { return userlist.users[him].like; }
RelationList<Hateby,Hate>& Hate::backlist(Uid him) { return userlist.users[him].hateby; }
RelationList<Hate,Hateby>& Hateby::backlist(Uid him) { return userlist.users[him].hate; }

struct Result {
    Uid who;
    int bits;
    Result(Uid _who, int _bits) :who(_who),bits(_bits) {}
    bool operator<(const Result& b) const {
	return who<b.who;
    }
};

int reverse_friend_stat(int stat)
{
    int             stat1 = 0;
    if (stat & IFH) stat1 |= HFM;
    if (stat & IRH) stat1 |= HRM;
    if (stat & HFM) stat1 |= IFH;
    if (stat & HRM) stat1 |= IRH;
    return stat1;
}

extern "C" void utmplogin(int uid, int index, const int like[MAX_FRIEND], const int hate[MAX_REJECT])
{
    /* login */
    userlist.login(uid, index, like, hate);
}

extern "C" int genfriendlist(int uid, int index, ocfs_t *fs, int maxfs)
{
    /* collect data */
    BBSUser& u=userlist.users[uid];
    myvector<Result,64,short> work;
    for(int i=0; i<u.like.n; i++) work.append(Result(u.like.base[i].him, IFH));
    for(int i=0; i<u.hate.n; i++) work.append(Result(u.hate.base[i].him, IRH));
    for(int i=0; i<u.likeby.n; i++) work.append(Result(u.likeby.base[i].him, HFM));
    for(int i=0; i<u.hateby.n; i++) work.append(Result(u.hateby.base[i].him, HRM));

    /* sort */
    std::sort(work.base, work.base+work.n);
    /* merge */
    if(work.n>0) {
	int newn=1;
	for(int i=1; i<work.n; i++)
	    if(work.base[i].who==work.base[newn-1].who) {
		work.base[newn-1].bits|=work.base[i].bits;
	    } else {
		work.base[newn++]=work.base[i];
	    }
	work.n=newn;
    }
    /* fill */
    int nfs=0;
    for(int i=0; i<work.n && nfs<maxfs; i++) {
	BBSUser& h=userlist.users[work.base[i].who];
	for(int j=0; j<h.utmplist.n && nfs<maxfs; j++) {
	    int rstat=reverse_friend_stat(work.base[i].bits);
	    if(h.utmplist.base[j]==index) continue;
	    fs[nfs].index=h.utmplist.base[j];
	    fs[nfs].uid=h.me;
	    fs[nfs].friendstat=(work.base[i].bits<<24)|h.utmplist.base[j];
	    fs[nfs].rfriendstat=(rstat<<24)|index;
	    nfs++;
	}
    }
    return nfs;
}

extern "C" void utmplogoutall(void)
{
    for(int i=0; i<USHM_SIZE; i++)
	if(utmp.utmp[i]!=-1)
	    userlist.users[utmp.utmp[i]].logout(i);
}

