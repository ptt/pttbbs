package legacy

import (
	"path"
	"strings"

	"pttbbs/gem/storage"
)

// etcLinks are the symbolic links of etc/ into gem areas that util/initbbs
// made (mbbsd now names those areas directly), which links of man/ may go
// through.
var etcLinks = [][2]string{
	{"etc/SONGBOOK", "man/boards/N/Note/SONGBOOK"},
	{"etc/SONGO", "man/boards/N/Note/SONGO"},
}

// DefaultOldHomes are where BBSHOME was when links were made, besides the
// BBSHOME of now: absolute targets under them are taken as under BBSHOME.
var DefaultOldHomes = []string{"/home/bbs"}

// cutPath cuts the path prefix p (followed by '/' or the end) from s.
func cutPath(s, p string) (string, bool) {
	rest, ok := strings.CutPrefix(s, p)
	if !ok || (rest != "" && rest[0] != '/') {
		return "", false
	}
	return strings.TrimPrefix(rest, "/"), true
}

// LinkTargets maps man/ symbolic link targets (absolute under bbsHome or
// oldHomes, or relative to the link) to gem areas, for storage.ResolveLinks.
func LinkTargets(bbsHome string, oldHomes []string) storage.LinkTarget {
	var homes []string
	for _, h := range append([]string{bbsHome}, oldHomes...) {
		if h = path.Clean(h); path.IsAbs(h) && h != "/" {
			homes = append(homes, h)
		}
	}
	return func(root string, dir []string, target string) (string, []string, bool) {
		var p string
		if path.IsAbs(target) {
			target = path.Clean(target)
			found := false
			for _, h := range homes {
				if rest, ok := cutPath(target, h); ok {
					p, found = rest, true
					break
				}
			}
			if !found {
				return "", nil, false
			}
		} else {
			base := RootDir("", root)
			if base == "" {
				return "", nil, false
			}
			p = path.Join(append([]string{base}, append(dir, target)...)...)
		}
		p = path.Clean(p)
		if p == ".." || strings.HasPrefix(p, "../") {
			return "", nil, false
		}
		for _, l := range etcLinks {
			if rest, ok := cutPath(p, l[0]); ok {
				p = path.Join(l[1], rest)
				break
			}
		}
		return splitManPath(p)
	}
}

// splitManPath splits a path relative to BBSHOME into a gem area and the
// names below it.
func splitManPath(p string) (string, []string, bool) {
	var root string
	var names []string
	parts := strings.Split(p, "/")
	switch {
	case len(parts) >= 4 && parts[0] == "man" && parts[1] == "boards":
		root, names = storage.BoardRoot(parts[3]), parts[4:]
	case len(parts) >= 1 && parts[0] == "man" && (len(parts) == 1 || parts[1] != "boards"):
		root, names = storage.RootSite, parts[1:]
	case len(parts) >= 4 && parts[0] == "home" && parts[3] == "man":
		root, names = storage.UserRoot(parts[2]), parts[4:]
	default:
		return "", nil, false
	}
	if !storage.ValidRoot(root) {
		return "", nil, false
	}
	for _, n := range names {
		if !storage.ValidName(n) {
			return "", nil, false
		}
	}
	return root, names, true
}

// ResolveLinks resolves the man/ links of every gem area of st, and turns
// links to .index into table of contents entries.
func ResolveLinks(st *storage.Store, bbsHome string, oldHomes []string, logf func(format string, args ...any)) (storage.ResolveStats, error) {
	return st.ResolveLinksWith(storage.ResolveOptions{
		Target:      LinkTargets(bbsHome, oldHomes),
		IsIndex:     isIndexLink,
		IndexHeader: setIndexHeader,
		Logf:        logf,
	})
}
