#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include "libcommon.h"
#include "keymap.h"
#include "loadkeys.h"
#include "xkbsupport.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static const int layout_switch[] = {
	0,
	(1 << KG_SHIFTL),
	(1 << KG_SHIFTR),
	(1 << KG_SHIFTL) | (1 << KG_SHIFTR)
};

/*
 * number    |
 * of groups | group
 * --------------------------
 *         0 | { 0, 0, 0, 0 }
 *         1 | { 0, 1, 1, 0 }
 *         2 | { 0, 1, 2, 0 }
 *         3 | { 0, 1, 3, 2 }
 */
static const unsigned int layouts[4][4] = {
	{ 0, 0, 0, 0 },
	{ 0, 1, 1, 0 },
	{ 0, 1, 2, 0 },
	{ 0, 1, 3, 2 },
};

struct xkeymap {
	struct xkb_context *xkb;
	struct xkb_keymap *keymap;
	struct lk_ctx *ctx;
};

/*
 * Offset between evdev keycodes (where KEY_ESCAPE is 1), and the evdev XKB
 * keycode set (where ESC is 9).
 */
#define EVDEV_OFFSET 8

/*
 * Convert xkb keycode to kernel keycode.
 */
#define KERN_KEYCODE(keycode) (keycode - EVDEV_OFFSET)

/*
 * Copied from libxkbcommon.
 * Don't allow more modifiers than we can hold in xkb_mod_mask_t.
 */
#define XKB_MAX_MODS ((xkb_mod_index_t)(sizeof(xkb_mod_mask_t) * 8))

struct xkb_mask {
	xkb_mod_mask_t mask[XKB_MAX_MODS];
	size_t num;
};

/*
 * From xkbcommon documentation:
 *
 * The following table lists the usual modifiers present in the [standard
 * keyboard configuration][xkeyboard-config]. Note that this is provided for
 * information only, as it may change depending on the user configuration.
 *
 * | Modifier     | Type    | Usual mapping    | Comment                             |
 * | ------------ | ------- | ---------------- | ----------------------------------- |
 * | `Shift`      | Real    | `Shift`          | The usual [Shift]                   |
 * | `Lock`       | Real    | `Lock`           | The usual [Caps Lock][Lock]         |
 * | `Control`    | Real    | `Control`        | The usual [Control]                 |
 * | `Mod1`       | Real    | `Mod1`           | Not conventional                    |
 * | `Mod2`       | Real    | `Mod2`           | Not conventional                    |
 * | `Mod3`       | Real    | `Mod3`           | Not conventional                    |
 * | `Mod4`       | Real    | `Mod4`           | Not conventional                    |
 * | `Mod5`       | Real    | `Mod5`           | Not conventional                    |
 * | `Alt`        | Virtual | `Mod1`           | The usual [Alt]                     |
 * | `Meta`       | Virtual | `Mod1` or `Mod4` | The legacy [Meta] key               |
 * | `NumLock`    | Virtual | `Mod2`           | The usual [NumLock]                 |
 * | `Super`      | Virtual | `Mod4`           | The usual [Super]/GUI               |
 * | `LevelThree` | Virtual | `Mod3`           | [ISO][ISO9995] level 3, aka [AltGr] |
 * | `LevelFive`  | Virtual | `Mod5`           | [ISO][ISO9995] level 5              |
 *
 * [Shift]: https://en.wikipedia.org/wiki/Control_key
 * [Lock]: https://en.wikipedia.org/wiki/Caps_Lock
 * [Control]: https://en.wikipedia.org/wiki/Control_key
 * [Alt]: https://en.wikipedia.org/wiki/Alt_key
 * [AltGr]: https://en.wikipedia.org/wiki/AltGr_key
 * [NumLock]: https://en.wikipedia.org/wiki/Num_Lock
 * [Meta]: https://en.wikipedia.org/wiki/Meta_key
 * [Super]: https://en.wikipedia.org/wiki/Super_key_(keyboard_button)
 *
 * See: https://github.com/xkbcommon/libxkbcommon/blob/master/doc/keymap-format-text-v1.md
 */
struct modifier_mapping {
	const char *xkb_mod;
	const char *krn_mod;
	const int bit;
};

static struct modifier_mapping modifier_mapping[] = {
	{ "Shift",		"shift",	(1u << KG_SHIFT)	},
	{ "Lock",		"capslock",	(1u << KG_CAPSSHIFT)	},
	{ "Control",		"control",	(1u << KG_CTRL)		},
	{ "Mod1",		"alt",		(1u << KG_ALT)		},
	{ "Mod2",		"<numlock>",	0			},
	{ "Mod3",		"altgr",	(1u << KG_ALTGR)	},
	{ "Mod4",		"<super>",	0			},
	{ "Mod5",		"alt",		(1u << KG_ALT)		},
	{ "Alt",		"alt",		(1u << KG_ALT)		},
	{ "Meta",		"<meta>",	0			},
	{ "NumLock",		"<numlock>",	0			},
	{ "Super",		"<super>",	0			},
	{ "LevelThree",		"altgr",	(1u << KG_ALTGR)	},
	{ "LevelFive",		"alt",		(1u << KG_ALT)		},
	{ NULL,			NULL,		0			},
};

static struct modifier_mapping *convert_modifier(const char *xkb_name)
{
	struct modifier_mapping *map = modifier_mapping;

	while (map->xkb_mod) {
		if (!strcasecmp(map->xkb_mod, xkb_name))
			return map;
		map++;
	}

	return NULL;
}

struct symbols_mapping {
	const char *xkb_sym;
	const char *krn_sym;
};

static struct symbols_mapping symbols_mapping[] = {
	{ "0", "zero" },
	{ "1", "one" },
	{ "2", "two" },
	{ "3", "three" },
	{ "4", "four" },
	{ "5", "five" },
	{ "6", "six" },
	{ "7", "seven" },
	{ "8", "eight" },
	{ "9", "nine" },
	{ "KP_Insert", "KP_0" },
	{ "KP_End", "KP_1" },
	{ "KP_Down", "KP_2" },
	{ "KP_Next", "KP_3" },
	{ "KP_Left", "KP_4" },
	{ "KP_Right", "KP_6" },
	{ "KP_Home", "KP_7" },
	{ "KP_Up", "KP_8" },
	{ "KP_Prior", "KP_9" },
	{ "KP_Begin", "VoidSymbol" },
	{ "KP_Delete", "VoidSymbol" },
	{ "Alt_R", "Alt" },
	{ "Alt_L", "Alt" },
	{ "Control_R", "Control" },
	{ "Control_L", "Control" },
	{ "Super_R", "Alt" },
	{ "Super_L", "Alt" },
	{ "Hyper_R", "Alt" },
	{ "Hyper_L", "Alt" },
	{ "Mode_switch", "AltGr" },
	{ "ISO_Group_Shift", "AltGr" },
	{ "ISO_Group_Latch", "AltGr" },
	{ "ISO_Group_Lock", "AltGr_Lock" },
	{ "ISO_Next_Group", "AltGr_Lock" },
	{ "ISO_Next_Group_Lock", "AltGr_Lock" },
	{ "ISO_Prev_Group", "AltGr_Lock" },
	{ "ISO_Prev_Group_Lock", "AltGr_Lock" },
	{ "ISO_First_Group", "AltGr_Lock" },
	{ "ISO_First_Group_Lock", "AltGr_Lock" },
	{ "ISO_Last_Group", "AltGr_Lock" },
	{ "ISO_Last_Group_Lock", "AltGr_Lock" },
	{ "ISO_Level3_Shift", "AltGr" },
	{ "ISO_Left_Tab", "Meta_Tab" },
	{ "XF86Switch_VT_1", "Console_1" },
	{ "XF86Switch_VT_2", "Console_2" },
	{ "XF86Switch_VT_3", "Console_3" },
	{ "XF86Switch_VT_4", "Console_4" },
	{ "XF86Switch_VT_5", "Console_5" },
	{ "XF86Switch_VT_6", "Console_6" },
	{ "XF86Switch_VT_7", "Console_7" },
	{ "XF86Switch_VT_8", "Console_8" },
	{ "XF86Switch_VT_9", "Console_9" },
	{ "XF86Switch_VT_10", "Console_10" },
	{ "XF86Switch_VT_11", "Console_11" },
	{ "XF86Switch_VT_12", "Console_12" },
	{ "Sys_Req", "Last_Console" },
	{ "Print", "Control_backslash" },
	{ NULL, NULL },
};

static const char *map_xkbsym_to_ksym(const char *xkb_sym)
{
	struct symbols_mapping *map = symbols_mapping;

	while (map->xkb_sym) {
		if (!strcmp(map->xkb_sym, xkb_sym))
			return map->krn_sym;
		map++;
	}

	return NULL;
}

static void print_modifiers(struct xkb_keymap *keymap, struct xkb_mask *mask)
{
	xkb_mod_index_t num_mods = xkb_keymap_num_mods(keymap);

	for (size_t m = 0; m < mask->num; m++) {
		for (xkb_mod_index_t mod = 0; mod < num_mods; mod++) {
			if ((mask->mask[m] & (1u << mod)) == 0)
				continue;

			const char *modname = xkb_keymap_mod_get_name(keymap, mod);

			printf(" %ld:%s(%d)", m, modname, mod);
		}
	}
}

static void xkeymap_keycode_mask(struct xkb_keymap *keymap,
                                 xkb_layout_index_t layout, xkb_level_index_t level, xkb_keycode_t keycode,
                                 struct xkb_mask *out)
{
	memset(out->mask, 0, sizeof(out->mask));
	out->num = xkb_keymap_key_get_mods_for_level(keymap, keycode, layout, level, out->mask, ARRAY_SIZE(out->mask));
}

static void xkeymap_walk_printer(struct xkeymap *xkeymap,
                                 xkb_layout_index_t layout, xkb_level_index_t level,
				 xkb_keycode_t keycode, xkb_keysym_t sym)
{
	switch (sym) {
		case XKB_KEY_ISO_Next_Group:
			printf("* ");
			break;
		default:
			printf("  ");
			break;
	}

	printf("{%-12s} layout=%d level=%d ",
		xkb_keymap_layout_get_name(xkeymap->keymap, layout), layout, level);

	char s[255];
	const char *symname = NULL;
	int ret, kunicode;
	uint32_t unicode = xkb_keysym_to_utf32(sym);

	ret = xkb_keysym_get_name(sym, s, sizeof(s));

	if (ret < 0 || (size_t)ret >= sizeof(s)) {
		kbd_warning(0, "failed to get name of keysym");
		return;
	}

	if (!(symname = map_xkbsym_to_ksym(s)))
		symname = s;

	if (unicode == 0) {
		if ((kunicode = lk_ksym_to_unicode(xkeymap->ctx, symname)) < 0) {
			printf("keycode %3d = %-39s", KERN_KEYCODE(keycode), symname);
		} else {
			unicode = (uint32_t) kunicode;
		}
	}

	if (unicode > 0)
		printf("keycode %3d = U+%04x %-32s", KERN_KEYCODE(keycode), unicode, symname);

	struct xkb_mask keycode_mask;
	xkeymap_keycode_mask(xkeymap->keymap, layout, level, keycode, &keycode_mask);

	printf("\txkb-mods=[");
	print_modifiers(xkeymap->keymap, &keycode_mask);
	printf(" ]");

	printf("\tkernel-mods=[");
	if (keycode_mask.num > 0) {
		xkb_mod_index_t num_mods = xkb_keymap_num_mods(xkeymap->keymap);

		for (xkb_mod_index_t mod = 0; mod < num_mods; mod++) {
			if ((keycode_mask.mask[0] & (1u << mod)) == 0)
				continue;

			const struct modifier_mapping *map = convert_modifier(xkb_keymap_mod_get_name(xkeymap->keymap, mod));

			printf(" %s", map->krn_mod);
		}
	}
	printf(" ]");

	printf("\n");

	fflush(stdout);
	fflush(stderr);
}

static int parse_hexcode(struct lk_ctx *ctx, const char *symname)
{
	int value;
	size_t ret = strlen(symname);

	if (ret >= 4 && symname[0] == '0' && symname[1] == 'x') {
		errno = 0;
		value = (int) strtol(symname + 2, NULL, 16);
		if (errno == ERANGE) {
			kbd_warning(0, "unable to convert unnamed non-Unicode xkb symbol `%s'", symname);
			return -1;
		}
		return lk_convert_code(ctx, value, TO_UNICODE);
	}

	if (ret >= 5 && symname[0] == 'U' &&
	   isxdigit(symname[1]) && isxdigit(symname[2]) &&
	   isxdigit(symname[3]) && isxdigit(symname[4])) {
		errno = 0;
		value = (int) strtol(symname + 1, NULL, 16);
		if (errno == ERANGE) {
			kbd_warning(0, "unable to convert unnamed unicode xkb symbol `%s'", symname);
			return -1;
		}
		return lk_convert_code(ctx, value ^ 0xf000, TO_UNICODE);
	}

	return 0;
}

static int xkeymap_get_code(struct lk_ctx *ctx, xkb_keysym_t symbol)
{
	uint32_t xkb_unicode;
	int ret;
	const char *symname;
	char symbuf[BUFSIZ];

	symbuf[0] = 0;

	ret = xkb_keysym_get_name(symbol, symbuf, sizeof(symbuf));

	if (ret < 0 || (size_t)ret >= sizeof(symbuf)) {
		kbd_warning(0, "failed to get name of keysym");
		return K_HOLE;
	}

	/*
	 * First. If the symbol name is known to us, that is, it matches
	 * the kbd being used, then we use it.
	 */
	if (lk_valid_ksym(ctx, symbuf, TO_UNICODE))
		return lk_ksym_to_unicode(ctx, symbuf);

	/*
	 * Second. Let's try to translate the xkb name into the kbd name.
	 */
	if ((symname = map_xkbsym_to_ksym(symbuf)) != NULL &&
	    lk_valid_ksym(ctx, symname, TO_UNICODE))
		return lk_ksym_to_unicode(ctx, symname);

	/*
	 * Third. Let's try to use utf32 code.
	 */
	if ((xkb_unicode = xkb_keysym_to_utf32(symbol)) > 0)
		return (int) (xkb_unicode ^ 0xf000);

	/*
	 * Last chance.
	 */
	if ((ret = parse_hexcode(ctx, symbuf)) > 0)
		return ret;

	return K_HOLE;
}

static int xkeymap_get_symbol(struct xkb_keymap *keymap,
		xkb_keycode_t keycode, xkb_layout_index_t layout, xkb_level_index_t level,
		xkb_keysym_t *symbol)
{
	int num_syms;
	const xkb_keysym_t *syms = NULL;

	if (!(num_syms = xkb_keymap_key_get_syms_by_level(keymap, keycode, layout, level, &syms)))
		return 0;

	if (num_syms > 1) {
		kbd_warning(0, "fixme: more %d symbol on level", num_syms);
		return -1;
	}

	*symbol = syms[0];
	return 1;
}

static void xkeymap_add_value(struct xkeymap *xkeymap, int modifier, int code, int keyvalue[MAX_NR_KEYMAPS])
{
	if (!modifier || (modifier & (1 << KG_SHIFT)))
		code = lk_add_capslock(xkeymap->ctx, code);

	keyvalue[modifier] = code;
}

static int xkeymap_walk(struct xkeymap *xkeymap)
{
	struct xkb_mask keycode_mask;
	xkb_mod_index_t num_mods = xkb_keymap_num_mods(xkeymap->keymap);
	xkb_keycode_t min_keycode = xkb_keymap_min_keycode(xkeymap->keymap);
	xkb_keycode_t max_keycode = xkb_keymap_max_keycode(xkeymap->keymap);

	if (KERN_KEYCODE(min_keycode) >= NR_KEYS) {
		kbd_warning(0, "keymap defines more keycodes than the kernel can handle.");
		min_keycode = (NR_KEYS - 1) + EVDEV_OFFSET;
	}

	if (KERN_KEYCODE(max_keycode) >= NR_KEYS) {
		kbd_warning(0, "keymap defines more keycodes than the kernel can handle.");
		max_keycode = (NR_KEYS - 1) + EVDEV_OFFSET;
	}

	int shiftl_lock = lk_ksym_to_unicode(xkeymap->ctx, "ShiftL_Lock");
	int shiftr_lock = lk_ksym_to_unicode(xkeymap->ctx, "ShiftR_Lock");

	xkb_layout_index_t num_layouts = xkb_keymap_num_layouts(xkeymap->keymap);

	for (xkb_keycode_t keycode = min_keycode; keycode <= max_keycode; keycode++) {
		unsigned short kbd_switch = 0;
		int keyvalue[MAX_NR_KEYMAPS];

		memset(keyvalue, 0, sizeof(keyvalue));

		/*
		 * A mapping of keycodes to symbols, actions and key types.
		 *
		 * A user who deals with multiple languages may need two or more
		 * different layouts: e.g. a layout for Arabic and another one for
		 * English. In this context, layouts are called _groups_ in XKB,
		 * as defined in the [standard ISO/IEC&nbsp;9995][ISO9995].
		 *
		 * Layouts are ordered and identified by their index. Example:
		 *
		 * - Layout 1: Arabic
		 * - Layout 2: English
		 *
		 * See: https://github.com/xkbcommon/libxkbcommon/blob/master/doc/keymap-format-text-v1.md
		 */
		for (xkb_layout_index_t layout = 0; layout < num_layouts; layout++) {
			xkb_level_index_t num_levels = xkb_keymap_num_levels_for_key(xkeymap->keymap, keycode, layout);

			/*
			 * A key type defines the levels available for a key and
			 * how to derive the active level from the modifiers states. Examples:
			 * - `ONE_LEVEL`: the key has only one level, i.e. it is not affected
			 *    by any modifiers. Example: the modifiers themselves.
			 * - `TWO_LEVEL`: the key has two levels:
			 *   - Level 1: default level, active when the `Shift` modifier is _not_ active.
			 *   - Level 2: level activated with the `Shift` modifier.
			 * - `FOUR_LEVEL`: see the example in the previous section.
			 *
			 * See: https://github.com/xkbcommon/libxkbcommon/blob/master/doc/keymap-format-text-v1.md
			*/
			for (xkb_level_index_t level = 0; level < num_levels; level++) {
				xkb_keysym_t sym;
				int ret, value;

				/*
				 * In XKB world, a key action defines the effect a key
				 * has on the state of the keyboard or the state of the display server.
				 * Examples:
				 *
				 * - Change the state of a modifier.
				 * - Change the active group.
				 * - Move the mouse pointer.
				 *
				 * See: https://github.com/xkbcommon/libxkbcommon/blob/master/doc/keymap-format-text-v1.md
				 */
				if (!(ret = xkeymap_get_symbol(xkeymap->keymap, keycode, layout, level, &sym)))
					continue;
				else if (ret < 0)
					goto err;

				if (getenv("LK_XKB_DEBUG")) {
					xkeymap_walk_printer(xkeymap, layout, level, keycode, sym);
					continue;
				}

				if (sym == XKB_KEY_ISO_Next_Group) {
					xkeymap_add_value(xkeymap, layout_switch[0], shiftl_lock, keyvalue);
					xkeymap_add_value(xkeymap, layout_switch[1], shiftr_lock, keyvalue);
					xkeymap_add_value(xkeymap, layout_switch[2], shiftr_lock, keyvalue);
					xkeymap_add_value(xkeymap, layout_switch[3], shiftl_lock, keyvalue);

					goto process_keycode;
				}

				if ((value = xkeymap_get_code(xkeymap->ctx, sym)) < 0)
					continue;

				xkeymap_keycode_mask(xkeymap->keymap, layout, level, keycode, &keycode_mask);

				for (unsigned short i = 0; i < ARRAY_SIZE(layout_switch); i++) {
					const struct modifier_mapping *map;
					int modifier = layout_switch[i];

					if (layout != layouts[num_layouts - 1][i])
						continue;

					for (xkb_mod_index_t mod = 0; mod < num_mods; mod++) {
						if (!(keycode_mask.mask[0] & (1u << mod)))
							continue;

						map = convert_modifier(xkb_keymap_mod_get_name(xkeymap->keymap, mod));
						if (map)
							modifier |= map->bit;
					}

					xkeymap_add_value(xkeymap, modifier, value, keyvalue);
				}
			}
		}

		if (getenv("LK_XKB_DEBUG"))
			continue;

process_keycode:
		for (unsigned short i = 0; i < ARRAY_SIZE(keyvalue); i++) {
			if (kbd_switch < ARRAY_SIZE(layout_switch) && i == layout_switch[kbd_switch + 1])
				kbd_switch++;

			if (!keyvalue[i]) {
				if ((i & (1 << KG_SHIFT)))
					keyvalue[i] = keyvalue[layout_switch[kbd_switch] | (1 << KG_SHIFT)];
				if (!keyvalue[i])
					keyvalue[i] = keyvalue[layout_switch[kbd_switch]];
			}

			if (lk_add_key(xkeymap->ctx, i, (int) KERN_KEYCODE(keycode), keyvalue[i]) < 0)
				goto err;
		}
	}

	return 0;
err:
	return -1;
}


int convert_xkb_keymap(struct lk_ctx *ctx, struct xkeymap_params *params, int options)
{
	struct xkeymap xkeymap;
	int ret = -1;

	struct xkb_rule_names names = {
		.rules = "evdev",
		.model = params->model,
		.layout = params->layout,
		.variant = params->variant,
		.options = params->options,
	};

	xkeymap.ctx = ctx;

	lk_set_keywords(ctx, LK_KEYWORD_ALTISMETA | LK_KEYWORD_STRASUSUAL);

	xkeymap.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!xkeymap.xkb) {
		kbd_warning(0, "xkb_context_new failed");
		goto end;
	}

	xkeymap.keymap = xkb_keymap_new_from_names(xkeymap.xkb, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (!xkeymap.keymap) {
		kbd_warning(0, "xkb_keymap_new_from_names failed");
		goto end;
	}

	if (xkb_keymap_num_layouts(xkeymap.keymap) > ARRAY_SIZE(layout_switch)) {
		kbd_warning(0, "too many layouts specified. At the moment, you can use no more than %ld", ARRAY_SIZE(layout_switch));
		goto end;
	}

	if (!(ret = xkeymap_walk(&xkeymap))) {
		if (options & OPT_P)
			lk_dump_keymap(ctx, stdout, LK_SHAPE_SEPARATE_LINES, 0);
	}

end:
	xkb_keymap_unref(xkeymap.keymap);
	xkb_context_unref(xkeymap.xkb);

	return ret;
}
