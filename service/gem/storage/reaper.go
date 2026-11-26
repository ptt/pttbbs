package storage

import (
	"log"
	"time"

	"github.com/cockroachdb/pebble"
)

func (s *Store) wakeReaper() {
	select {
	case s.reapWake <- struct{}{}:
	default:
	}
}

func (s *Store) reaper() {
	defer close(s.reapDone)
	for {
		select {
		case <-s.reapStop:
			return
		case <-s.reapWake:
		}
		for {
			n, err := s.freeSome(256)
			if err != nil {
				log.Printf("[gem.svc] freeing listings: %v", err)
				break
			}
			if n == 0 {
				break
			}
			select {
			case <-s.reapStop:
				return
			default:
			}
			// Leave room for the writers.
			time.Sleep(time.Millisecond)
		}
	}
}

// FreeAll frees every listing waiting to be freed (and what only they
// referred to), and returns how many listings it freed.
func (s *Store) FreeAll() (int, error) {
	total := 0
	for {
		n, err := s.freeSome(1024)
		total += n
		if err != nil || n == 0 {
			return total, err
		}
	}
}

// Pending is the number of listings waiting to be freed.
func (s *Store) Pending() (int, error) {
	it, err := s.db.NewIter(&pebble.IterOptions{LowerBound: []byte{pFree}, UpperBound: []byte{pFree + 1}})
	if err != nil {
		return 0, err
	}
	defer it.Close()
	n := 0
	for it.First(); it.Valid(); it.Next() {
		n++
	}
	return n, it.Error()
}

// freeSome frees up to max listings waiting in the free queue, in one Tx.
func (s *Store) freeSome(max int) (int, error) {
	var ids []ListingID
	it, err := s.db.NewIter(&pebble.IterOptions{LowerBound: []byte{pFree}, UpperBound: []byte{pFree + 1}})
	if err != nil {
		return 0, err
	}
	for it.First(); it.Valid() && len(ids) < max; it.Next() {
		k := it.Key()
		if len(k) == 9 {
			var id uint64
			for _, c := range k[1:] {
				id = id<<8 | uint64(c)
			}
			ids = append(ids, ListingID(id))
		}
	}
	if err := it.Close(); err != nil {
		return 0, err
	}
	if len(ids) == 0 {
		return 0, nil
	}
	err = s.UpdateNoSync(func(t *Tx) error {
		for _, id := range ids {
			if err := t.batch.Delete(idKey(pFree, id), nil); err != nil {
				return err
			}
			// Referenced again since (it should not be): keep it.
			if n, err := t.refCount(id); err != nil {
				return err
			} else if n > 0 {
				continue
			}
			l, err := t.listing(id)
			if err != nil {
				log.Printf("[gem.svc] free listing %d: %v", id, err)
				continue
			}
			for i := range l.Entries {
				if err := t.decEntry(&l.Entries[i]); err != nil {
					return err
				}
			}
			delete(t.dirty, id)
			if err := t.batch.Delete(idKey(pList, id), nil); err != nil {
				return err
			}
		}
		return nil
	})
	return len(ids), err
}
