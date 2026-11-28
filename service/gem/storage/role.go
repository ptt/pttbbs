package storage

import (
	"fmt"
	"strings"
)

// Role is who reads, and so what of the areas they see. mbbsd and the tools
// read as RoleAll and check people themselves; the web (mand) reads as
// RolePublic, so that a page is one call however deep it is.
type Role int

const (
	RoleAll    Role = iota
	RolePublic      // the boards and the site, without hidden entries
)

// Filemode bits of fileheader_t that hide an entry in a gem area.
const (
	fileHide = 0x0020 // FILE_HIDE
	fileBM   = 0x0040 // FILE_BM
)

// ParseRole reads a role of the protocol ("all", "public").
func ParseRole(s string) (Role, error) {
	switch s {
	case "all":
		return RoleAll, nil
	case "public":
		return RolePublic, nil
	}
	return RoleAll, fmt.Errorf("bad role %q", s)
}

// canRead tells whether r may read root: not the mail folders of people or
// a trash in public.
func (r Role) canRead(root string) bool {
	return r == RoleAll || root == "site" || strings.HasPrefix(root, "board:")
}

// sees tells whether r sees e.
func (r Role) sees(e *Entry) bool {
	return r == RoleAll || e.Header.Filemode()&(fileHide|fileBM) == 0
}

// filter returns l as r sees it: what is hidden is not there, so a path
// through it is not found and indexes are those r lists.
func (r Role) filter(l *Listing) *Listing {
	if r == RoleAll {
		return l
	}
	var out *Listing
	for i := range l.Entries {
		if r.sees(&l.Entries[i]) {
			if out != nil {
				out.Entries = append(out.Entries, l.Entries[i])
			}
			continue
		}
		if out == nil {
			out = &Listing{Entries: append([]Entry(nil), l.Entries[:i]...)}
		}
	}
	if out == nil {
		return l
	}
	out.Recount()
	return out
}
