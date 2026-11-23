package daemon

import (
	"log"

	"pttbbs/post/importer"
)

// The communities follow the board headers in $BBSHOME/.BRD, which stays
// the source of truth. Everything that writes .BRD (mbbsd and the
// utilities) does it through BoF, which tells post.svc the slot it wrote
// (EDIT_COMMUNITY): only that record is read. What changed while post.svc
// was down (BoF could not tell it) is caught when it starts: all of .BRD is
// read once, and only the boards that changed are written. What the board
// header cannot say (removing, renaming and merging boards) comes as
// commands, through BoF too.

// SyncBoards mirrors all of $BBSHOME/.BRD into the communities. Boards that
// cannot be mirrored (e.g. a board renamed in .BRD but not in post.svc)
// are logged and skipped.
func (s *Server) SyncBoards() (*importer.ImportBoardsStats, error) {
	stats, err := importer.ImportBoards(s.storage, importer.ImportBoardsOptions{
		BBSHome: s.cfg.BBSHome,
		IsBig5:  s.cfg.IsBig5(),
		Users:   s.users,
	})
	if err != nil {
		return nil, err
	}
	for _, e := range stats.Errors {
		log.Printf("[post.svc] Cannot mirror board %s", e)
	}
	return stats, nil
}
