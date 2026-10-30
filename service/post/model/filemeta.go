package model

// Bits of fileheader_t.filemode (include/pttstruct.h) for board posts that
// decide how fileheader_t.multi is interpreted.
const (
	FilePetition  = 0x0040 // FILE_PETITION: multi is not used (petitions follow the board's limits)
	FileAnonymous = 0x0080 // FILE_ANONYMOUS: multi holds the usernum of the author
)

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
