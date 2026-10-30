package model

import (
	"fmt"
	"strconv"
	"strings"
)

// Deletion is how a post or comment was taken down, as in Lemmy: either
// deleted by its creator, or removed by a moderator (or the system), who
// may give a reason.
type Deletion struct {
	Deleted       bool     `json:"deleted,omitempty"`         // deleted by its creator
	Removed       bool     `json:"removed,omitempty"`         // removed by a moderator or the system
	DeletedAt     int64    `json:"deleted_at,omitempty"`      // when it was deleted or removed (Lemmy keeps it in the modlog)
	ModPersonName string   `json:"mod_person_name,omitempty"` // who removed it
	ModPersonID   PersonID `json:"mod_person_id,omitempty"`
	Reason        string   `json:"reason,omitempty"` // why it was removed
}

// DeletedOrRemoved reports whether it has been taken down at all.
func (d *Deletion) DeletedOrRemoved() bool {
	return d.Deleted || d.Removed
}

// Delete takes it down at time at, on behalf of byName / byID: as deleted
// if that is its creator, and as removed (for reason) otherwise.
func (d *Deletion) Delete(creatorName string, creatorID PersonID, byName string, byID PersonID, reason string, at int64) {
	self := byName == creatorName
	if byID != 0 && creatorID != 0 {
		self = byID == creatorID
	}
	*d = Deletion{DeletedAt: at}
	if self {
		d.Deleted = true
	} else {
		d.Removed = true
		d.ModPersonName, d.ModPersonID, d.Reason = byName, byID, reason
	}
}

// Restore undoes Delete.
func (d *Deletion) Restore() {
	*d = Deletion{}
}

func (d *Deletion) writeRFC822(b *strings.Builder) {
	if d.Deleted {
		b.WriteString("Deleted: true\n")
	}
	if d.Removed {
		b.WriteString("Removed: true\n")
	}
	if d.DeletedAt > 0 {
		fmt.Fprintf(b, "DeletedAt: %d\n", d.DeletedAt)
	}
	if d.ModPersonName != "" {
		fmt.Fprintf(b, "ModPersonName: %s\n", d.ModPersonName)
	}
	if d.ModPersonID != 0 {
		fmt.Fprintf(b, "ModPersonID: %d\n", d.ModPersonID)
	}
	if d.Reason != "" {
		fmt.Fprintf(b, "Reason: %s\n", d.Reason)
	}
}

// DeletionFromRFC822 reads the headers written for a post or comment.
func DeletionFromRFC822(meta map[string]string) Deletion {
	d := Deletion{
		Deleted:       meta["Deleted"] == "true",
		Removed:       meta["Removed"] == "true",
		ModPersonName: meta["ModPersonName"],
		ModPersonID:   ParsePersonID(meta["ModPersonID"]),
		Reason:        meta["Reason"],
	}
	d.DeletedAt, _ = strconv.ParseInt(meta["DeletedAt"], 10, 64)
	return d
}
