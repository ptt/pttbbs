/*
 * vform.h
 * Generic Form & Wizard Framework for VT100 / pttbbs
 *
 * Author: Hung-Te Lin (piaip)
 */

#ifndef _VFORM_H_
#define _VFORM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "common.h"
#include "vtuikit.h"

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Field Types
// -----------------------------------------------------------------------------
typedef enum {
    VFORM_TEXT = 0,     // Single-line text input (char buffer)
    VFORM_PASSWORD,     // Masked password input (displays as '*')
    VFORM_INT,          // Integer numeric input (int8, int16, int32, int64)
    VFORM_BOOL,         // Boolean toggle (bool / uint8_t, [Y/n] or [X]/[ ])
    VFORM_SELECT,       // Enumerated selection (int index into options array)
    VFORM_NOTE,         // Read-only info row / prompt note
    VFORM_CUSTOM,       // Custom input callback (e.g. usercomplete, picker)
    VFORM_USER,         // User ID input with usercomplete, requiring an existing user
} vform_type_t;

// Form execution modes
typedef enum {
    VFORM_MODE_AUTO = 0,   // Wizard if any field has notes_file; else in-place
    VFORM_MODE_WIZARD,     // Step-by-step wizard with backtracking (UP/ESC)
    VFORM_MODE_INPLACE,    // Interactive full-screen table / dialog
} vform_mode_t;

// Return codes for custom input callbacks
enum {
    VFORM_RET_OK    = 0,
    VFORM_RET_NEXT  = 1,
    VFORM_RET_PREV  = 2,
    VFORM_RET_ABORT = -1,
};

struct vform_field;
struct vform_def;

// Field validator callback: return NULL if valid, or an error message string.
typedef const char *(*vform_validator_t)(
    const struct vform_field *field, const void *val_ptr, void *priv);

// Custom input widget callback
typedef int (*vform_custom_input_t)(
    const struct vform_field *field, void *data, void *priv);

// Field visibility condition callback
typedef bool (*vform_visible_t)(
    const struct vform_field *field, const void *data, void *priv);

// Form validator callback (cross-field validation before submission)
typedef const char *(*vform_form_validator_t)(
    const struct vform_def *def, const void *data, void *priv);

// -----------------------------------------------------------------------------
// Field Definition
// -----------------------------------------------------------------------------
typedef struct vform_field {
    const char          *label;         // Field display label
    const char          *hint;          // Short hint / format description
    const char          *notes_file;    // Optional detailed notes file path
    vform_type_t        type;           // Field input type

    // Data Binding
    size_t              offset;         // offsetof(DataType, member)
    size_t              size;           // sizeof(member)

    // Modifiers & Constraints
    int                 vget_flags;     // VGET_* flags (e.g. VGET_LOWERCASE)
    int                 min_val;        // Minimum value (for VFORM_INT)
    int                 max_val;        // Maximum value (for VFORM_INT)
    const char * const  *options;       // Options array (for VFORM_SELECT)
    int                 num_options;    // Number of options (for VFORM_SELECT)

    // Callbacks
    vform_validator_t   validator;      // Field value validator
    vform_visible_t     is_visible;     // Dynamic visibility predicate
    vform_custom_input_t custom_fn;     // Custom input callback (for VFORM_CUSTOM)
} vform_field_t;

// -----------------------------------------------------------------------------
// Form Definition (Schema)
// -----------------------------------------------------------------------------
typedef struct vform_def {
    const char              *title;         // Form header title
    const char              *subtitle;      // Optional subtitle / guidance
    vform_mode_t            mode;          // Wizard, In-place, or Auto
    const vform_field_t     *fields;        // Array of field definitions
    int                     num_fields;     // Number of fields in array
    vform_form_validator_t  form_validator; // Final cross-field validator
    const char              *submit_prompt; // Final confirm prompt
    bool                    show_summary;   // Show summary review before confirm
} vform_def_t;

// -----------------------------------------------------------------------------
// Helper Declarative Macros
// -----------------------------------------------------------------------------
#define VFORM_FIELD_TEXT(lbl, hnt, notes, struct_t, member, flags, val_fn)     { .label = (lbl), .hint = (hnt), .notes_file = (notes), .type = VFORM_TEXT,       .offset = offsetof(struct_t, member), .size = MEMBER_SIZE(struct_t, member),       .vget_flags = (flags), .validator = (val_fn) }

#define VFORM_FIELD_PASSWORD(lbl, hnt, struct_t, member, val_fn)     { .label = (lbl), .hint = (hnt), .type = VFORM_PASSWORD,       .offset = offsetof(struct_t, member), .size = MEMBER_SIZE(struct_t, member),       .vget_flags = VGET_PASSWORD, .validator = (val_fn) }

#define VFORM_FIELD_USER(lbl, hnt, notes, struct_t, member, val_fn)     { .label = (lbl), .hint = (hnt), .notes_file = (notes), .type = VFORM_USER,       .offset = offsetof(struct_t, member), .size = MEMBER_SIZE(struct_t, member),       .validator = (val_fn) }

#define VFORM_FIELD_INT(lbl, hnt, struct_t, member, min_v, max_v, val_fn)     { .label = (lbl), .hint = (hnt), .type = VFORM_INT,       .offset = offsetof(struct_t, member), .size = MEMBER_SIZE(struct_t, member),       .min_val = (min_v), .max_val = (max_v), .validator = (val_fn) }

#define VFORM_FIELD_BOOL(lbl, hnt, struct_t, member)     { .label = (lbl), .hint = (hnt), .type = VFORM_BOOL,       .offset = offsetof(struct_t, member), .size = MEMBER_SIZE(struct_t, member) }

#define VFORM_FIELD_BOOL_NOTES(lbl, hnt, notes, struct_t, member)     { .label = (lbl), .hint = (hnt), .notes_file = (notes), .type = VFORM_BOOL,       .offset = offsetof(struct_t, member), .size = MEMBER_SIZE(struct_t, member) }

#define VFORM_FIELD_SELECT(lbl, hnt, struct_t, member, opts, num_opts)     { .label = (lbl), .hint = (hnt), .type = VFORM_SELECT,       .offset = offsetof(struct_t, member), .size = MEMBER_SIZE(struct_t, member),       .options = (opts), .num_options = (num_opts) }

#define VFORM_FIELD_NOTE(lbl, hnt)     { .label = (lbl), .hint = (hnt), .type = VFORM_NOTE }

// -----------------------------------------------------------------------------
// Form APIs
// -----------------------------------------------------------------------------

// Main entry point: runs the form according to def->mode (or auto-selects mode).
// Returns 1 on successful submit, 0 on cancel / abort.
int vform_run(const vform_def_t *def, void *data, void *priv);

// Runs the form in step-by-step wizard mode with full backtracking (UP / ESC).
int vform_run_wizard(const vform_def_t *def, void *data, void *priv);

// Runs the form in full-screen interactive table mode.
int vform_run_inplace(const vform_def_t *def, void *data, void *priv);

// Format a single field value into a readable string.
int vform_format_field(const vform_field_t *field, const void *data,
                       char *buf, size_t bufsz);

// Validate a single field; returns NULL if valid, or error string.
const char *vform_validate_field(const vform_field_t *field, const void *data,
                                 void *priv);

#ifdef __cplusplus
}
#endif

#endif // _VFORM_H_
