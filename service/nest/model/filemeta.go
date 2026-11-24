package model

// Bits of fileheader_t.filemode (include/pttstruct.h) for board posts that
// decide how fileheader_t.multi is interpreted.
const (
	FilePetition  = 0x0040 // FILE_PETITION: multi is not used (petitions follow the board's limits)
	FileAnonymous = 0x0080 // FILE_ANONYMOUS: multi holds the usernum of the author
)

// Other bits of fileheader_t.filemode for board posts.
const (
	FileMarked = 0x0002 // FILE_MARKED: kept (m)
	FileDigest = 0x0004 // FILE_DIGEST: in the board digest
	FileBottom = 0x0008 // FILE_BOTTOM: pinned to the bottom of the list (Lemmy featured_community)
	FileSolved = 0x0010 // FILE_SOLVED
)

// CanonicalPostFile returns the name nest.svc keeps a post under. mbbsd
// locks a post (Lemmy's locked) by renaming it in .DIR to L.<stamp> and
// its file to X.<stamp>, while nest.svc keeps the M.<stamp> name and the
// Locked flag, so that the post keeps its key.
func CanonicalPostFile(name string) string {
	if len(name) > 2 && name[1] == '.' && (name[0] == 'L' || name[0] == 'X') {
		return "M" + name[1:]
	}
	return name
}

// LockedListName returns the name of a locked post in .DIR (L.<stamp>).
func LockedListName(name string) string {
	if len(name) > 2 && name[1] == '.' && name[0] == 'M' {
		return "L" + name[1:]
	}
	return name
}

// LockedFileName returns the name of the file of a locked post
// (X.<stamp>), which mbbsd does not show.
func LockedFileName(name string) string {
	if len(name) > 2 && name[1] == '.' && name[0] == 'M' {
		return "X" + name[1:]
	}
	return name
}

// PackMulti returns fileheader_t.multi for a post.
func (p *Post) PackMulti() uint32 {
	switch {
	case p.Filemode&FilePetition != 0:
		return 0
	case p.Filemode&FileAnonymous != 0:
		return uint32(p.CreatorID.Usernum())
	default:
		return uint32(int32(p.Money))
	}
}
