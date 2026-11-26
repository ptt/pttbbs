package storage

import (
	"bytes"
	"encoding/binary"

	"github.com/cockroachdb/pebble"
)

// Contents staged outside the writer's lock, so imports do not wait on each
// other's writes. StageBlobs writes new contents under unique serials
// without the lock; Tx.PutStagedBlob then records them (B + hash), or uses
// the copy another writer stored in between. Unrecorded staged contents
// (error, crash) are unreachable and harmless.

// Staged is a content after StageBlobs.
type Staged struct {
	Hash    BlobID
	Size    uint64
	Serial  uint64 // of the copy written; 0 if it was stored already
	Existed bool   // stored already when staged
}

// lastChunkSerial is the serial of the last content stored (0 if none),
// which the next serial follows: staged contents are written without the
// lock, so the serial last recorded under it may be behind.
func lastChunkSerial(db *pebble.DB) (uint64, error) {
	it, err := db.NewIter(&pebble.IterOptions{LowerBound: []byte{pChunk}, UpperBound: []byte{pChunk + 1}})
	if err != nil {
		return 0, err
	}
	defer it.Close()
	if !it.Last() {
		return 0, it.Error()
	}
	k := it.Key()
	if len(k) < 9 {
		return 0, nil
	}
	return binary.BigEndian.Uint64(k[1:9]), nil
}

// StageBlobs writes the contents datas (hashes[i] = HashContent(datas[i]))
// that are not stored yet, outside the writer's lock. Record them with
// Tx.PutStagedBlob in the Tx that refers to them.
func (s *Store) StageBlobs(hashes []BlobID, datas [][]byte) ([]Staged, error) {
	out := make([]Staged, len(hashes))
	if len(hashes) == 0 {
		return out, nil
	}
	it, err := s.db.NewIter(&pebble.IterOptions{UseL6Filters: true})
	if err != nil {
		return nil, err
	}
	b := s.data.NewBatch()
	defer b.Close()
	staged := map[BlobID]uint64{} // the same content twice in datas
	for i, h := range hashes {
		out[i] = Staged{Hash: h, Size: uint64(len(datas[i]))}
		if serial, ok := staged[h]; ok {
			out[i].Serial = serial
			continue
		}
		k := blobKey(h)
		if it.SeekPrefixGE(k) && bytes.Equal(it.Key(), k) {
			out[i].Existed = true
			continue
		}
		serial := s.nextBlob.Add(1) - 1
		if err := writeChunks(b, serial, datas[i]); err != nil {
			it.Close()
			return nil, err
		}
		out[i].Serial = serial
		staged[h] = serial
	}
	if err := it.Close(); err != nil {
		return nil, err
	}
	if b.Empty() {
		return out, nil
	}
	return out, b.Commit(pebble.NoSync)
}

// writeChunks puts the content data under serial into b.
func writeChunks(b *pebble.Batch, serial uint64, data []byte) error {
	for i := 0; i*ChunkSize < len(data) || i == 0; i++ {
		end := min((i+1)*ChunkSize, len(data))
		if err := b.Set(chunkKey(serial, uint32(i)), data[i*ChunkSize:end], nil); err != nil {
			return err
		}
		if end == len(data) {
			break
		}
	}
	return nil
}

// PutStagedBlob records a content staged by StageBlobs (data is its
// content, written here only if it was neither staged nor stored any
// more). Like PutBlob, it holds no reference.
func (t *Tx) PutStagedBlob(st Staged, data []byte) (BlobID, error) {
	b, err := t.blob(st.Hash)
	if err != nil {
		return st.Hash, err
	}
	switch {
	case b.exists:
		// Stored by someone else meanwhile (or before): the staged copy,
		// if any and not the one recorded, is dropped.
		if st.Serial != 0 && st.Serial != b.serial {
			t.dropSerials = append(t.dropSerials, st.Serial)
		}
	case st.Serial != 0:
		b.exists = true
		b.written = true
		b.size = st.Size
		b.serial = st.Serial
	default:
		// It was stored when staged, and is gone since.
		return t.PutBlobHashed(st.Hash, data)
	}
	return st.Hash, nil
}
