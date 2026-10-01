/* lmp-import.cpp - the demo importer (lmp-import.h): every format dsda-doom
 * plays, as its playback reads it (G_ReadDemoHeaderEx, G_ReadOneTick,
 * dsda/demo.c, dsda/exdemo.c), and what each dictates as this core's settings.
 *
 *   Doom 1.0-1.2      no version byte, a 7-byte header (complevel 0); its
 *                     monster flags are not in it - the footer's, or the caller's
 *   Doom 1.4-1.9      104-109 (G_GetOriginalDoomCompatLevel: the footer's
 *                     -complevel, else 1 below 1.7, 3 on a game with a fourth
 *                     episode, 4 on Final Doom, 2 otherwise)
 *   TASDoom           110 (complevel 6; a tic's bytes in its own order)
 *   Doom longtics     111
 *   Boom 2.00-2.02    200-202 (complevel 9, 8, or 7 with its compatibility flag)
 *   LxDoom, MBF       203 (10 or 11, by the signature)
 *   PrBoom            210-214 (13-17; 214 with 16-bit turning)
 *   MBF21             221 (21; 16-bit turning)
 *   DSDA              255 + "DSDA": dsda's own header before one of the above -
 *                     extended commands each tic, casual features, a start from
 *                     a key frame (refused)
 *   PrBoom+um         255 + "PR+UM": a UMAPINFO header before one of the above
 *   Heretic, Hexen    no version byte; respawn, longtics and no monsters as bits
 *                     of player one's byte (vvHeretic's); Hexen's eight players'
 *                     classes, and its map as MAPINFO's warp number
 *
 * and the footer after the end marker (a WAD of PORTNAME, FEATURES, CMDLINE).
 * A movie has the melt off, as BizHawk's importers have it: a demo's tics are
 * the game's, and the melt's steps would be frames of their own. */
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "lmp-import.h"

extern "C" {
#include "dsda-input.h"
#include "dsda-games.h"
}
#include "dsda-options.h"

namespace
{

const int DEMOMARKER = 0x80;
const int BT_ATTACK = 1, BT_USE = 2, BT_CHANGE = 4, BT_SPECIAL = 128, BT_SPECIALMASK = 3, BT_PAUSE = 1;
const int XC_JUMP = 0x01, XC_SAVE = 0x02, XC_LOAD = 0x04, XC_GOD = 0x08, XC_NOCLIP = 0x10, XC_LOOK = 0x20;
const int AFLAG_JUMP = 0x80, AFLAG_SUICIDE = 0x40, AFLAG_MASK = 0x3F;
const int DEMOHEADER_RESPAWN = 0x20, DEMOHEADER_LONGTICS = 0x10, DEMOHEADER_NOMONSTERS = 0x02;
const int DF_FROM_KEYFRAME = 0x01, DF_CASUAL_FEATURES = 0x02;
const int MBF_GAME_OPTION_SIZE = 64;

struct DemoError
{
	std::string what;
};

[[noreturn]] void refuse(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
[[noreturn]] void refuse(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	throw DemoError{ buf };
}

int s8(int b) { return b >= 128 ? b - 256 : b; }

std::string lower(std::string s)
{
	for (char &c : s) c = (char)tolower((unsigned char)c);
	return s;
}

std::string basename_of(const std::string &path)
{
	size_t cut = path.find_last_of("/\\");
	return cut == std::string::npos ? path : path.substr(cut + 1);
}

bool ends_with(const std::string &s, const char *tail)
{
	size_t n = strlen(tail);
	return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

std::string join(const std::vector<std::string> &v, const char *sep)
{
	std::string out;
	for (size_t i = 0; i < v.size(); i++) out += (i ? sep : "") + v[i];
	return out;
}

/* ------------------------------------------------------------------ the demo */

struct Tic
{
	int fwd = 0, side = 0, turn = 0, frac = 0, buttons = 0, lookfly = 0, arti = 0;
	bool has_ex = false;
	int ex = 0, look = 0;
};

struct Demo
{
	std::string format;            /* a name, for the reader */
	int version = -1;              /* the version byte (-1: a versionless header) */
	std::string family = "doom";   /* doom, heretic or hexen: the tic's layout */
	int complevel = -1;            /* -1: the 1.4-1.9 rule decides with the game */
	bool longtics = false, tasdoom = false;
	int skill = 0, episode = 1, map = 1, deathmatch = 0, consoleplayer = 0;
	int respawn = -1, fast = -1, nomonsters = -1;  /* -1: not in the header */
	bool players[4] = { false, false, false, false };
	int classes[4] = { 0, 0, 0, 0 };
	std::vector<uint8_t> options;  /* a Boom-or-later header's option block, as it is */
	bool mbf21_options = false;
	bool has_rngseed = false;
	uint32_t rngseed = 0;
	bool excmd = false, casual = false, from_keyframe = false, umapinfo = false;
	std::vector<std::vector<Tic>> tics;   /* each tic: one for each player present */
	std::map<std::string, std::string> footer;  /* the footer's lumps, by name */
	std::vector<std::string> footer_args;
	std::string footer_text;
	int raven_bits = 0;
	bool truncated = false;        /* no end marker: the tics ran to the file's end */
};

void need(size_t size, size_t at, size_t n, const char *what)
{
	if (at + n > size) refuse("the demo ends inside its %s", what);
}

std::string guess_versionless_family(const uint8_t *data, size_t size, size_t start)
{
	/* which of Doom 1.2, Heretic and Hexen a versionless demo is, by where its
	 * end marker falls (dsda's own test, made exact); the game says it when it can */
	struct { const char *family; size_t header; size_t bpt; } k[] = { { "doom", 7, 4 }, { "heretic", 7, 6 }, { "hexen", 19, 6 } };
	std::vector<std::string> fits;
	for (auto &c : k)
	{
		if (start + c.header > size) continue;
		int players = 0;
		if (!strcmp(c.family, "hexen"))
			for (int i = 0; i < 8; i++) players += data[start + 3 + 2 * i] != 0;
		else
			for (int i = 0; i < 4; i++) players += data[start + 3 + i] != 0;
		const bool longtics = strcmp(c.family, "doom") && (data[start + 3] & DEMOHEADER_LONGTICS);
		const size_t step = (c.bpt + (longtics ? 1 : 0)) * (size_t)(players ? players : 1);
		size_t q = start + c.header;
		while (q < size && data[q] != DEMOMARKER) q += step;
		if (q < size && data[q] == DEMOMARKER) fits.push_back(c.family);
	}
	if (fits.size() == 1) return fits[0];
	refuse("its header has no version byte, and it could be %s: say the game (its IWAD, or its release)",
		fits.empty() ? "none of Doom 1.2, Heretic or Hexen" : join(fits, " or ").c_str());
}

std::map<std::string, std::string> read_footer(const uint8_t *data, size_t size, size_t p)
{
	/* dsda's and PrBoom+'s footer: a WAD after the end marker, its lumps by name */
	std::map<std::string, std::string> lumps;
	size_t at = std::string::npos;
	for (size_t i = p; i + 4 <= size; i++)
		if (!memcmp(data + i, "PWAD", 4)) { at = i; break; }
	if (at == std::string::npos || at + 12 > size) return lumps;
	int32_t n, ofs;
	memcpy(&n, data + at + 4, 4);
	memcpy(&ofs, data + at + 8, 4);
	for (int32_t i = 0; i < n; i++)
	{
		size_t e = at + (size_t)ofs + 16 * (size_t)i;
		if (ofs < 0 || e + 16 > size) return std::map<std::string, std::string>();
		int32_t pos, len;
		char name[9] = { 0 };
		memcpy(&pos, data + e, 4);
		memcpy(&len, data + e + 4, 4);
		memcpy(name, data + e + 8, 8);
		if (!name[0]) continue;
		size_t from = at + (size_t)(pos < 0 ? 0 : pos);
		size_t to = from + (size_t)(len < 0 ? 0 : len);
		if (from > size) from = size;
		if (to > size) to = size;
		lumps[name] = std::string((const char *)data + from, to - from);
	}
	return lumps;
}

/* a command line as words: shell quoting, as Python's shlex (double and single
 * quotes group); unbalanced, plain whitespace splitting */
std::vector<std::string> split_args(const std::string &text)
{
	std::vector<std::string> out;
	std::string cur;
	bool in_word = false;
	char quote = 0;
	for (size_t i = 0; i < text.size(); i++)
	{
		const char c = text[i];
		if (quote)
		{
			if (c == quote) quote = 0;
			else if (quote == '"' && c == '\\' && i + 1 < text.size() && strchr("\\\"$`\n", text[i + 1])) cur += text[++i];
			else cur += c;
		}
		else if (c == '"' || c == '\'') { quote = c; in_word = true; }
		else if (c == '\\' && i + 1 < text.size()) { cur += text[++i]; in_word = true; }
		else if (isspace((unsigned char)c))
		{
			if (in_word) out.push_back(cur);
			cur.clear();
			in_word = false;
		}
		else { cur += c; in_word = true; }
	}
	if (quote)
	{
		out.clear();
		std::string w;
		for (char c : text)
		{
			if (isspace((unsigned char)c)) { if (!w.empty()) out.push_back(w); w.clear(); }
			else w += c;
		}
		if (!w.empty()) out.push_back(w);
		return out;
	}
	if (in_word) out.push_back(cur);
	return out;
}

Demo parse_demo(const uint8_t *data, size_t size, const std::string &family_hint, bool longtics_flag)
{
	Demo d;
	size_t p = 0;
	need(size, p, 1, "header");
	int ver = data[p++];

	if (ver == 255)
	{
		/* dsda's header, or PrBoom+um's UMAPINFO one, before the real one */
		need(size, p, 7, "extended header");
		if (data[p] == 0x1D && !memcmp(data + p + 1, "DSDA", 4) && data[p + 5] == 0xE6)
		{
			const int dsda_version = data[p + 6];
			p += 7;
			if (dsda_version > 3) refuse("its DSDA header is version %d, newer than dsda-doom 0.30's (3)", dsda_version);
			const size_t hsize = dsda_version == 1 ? 8 : dsda_version == 2 ? 9 : dsda_version == 3 ? 10 : 0;
			need(size, p, hsize, "DSDA header");
			const int flags = dsda_version >= 2 ? data[p + 8] : 0;
			p += hsize;
			d.excmd = true;
			d.casual = flags & DF_CASUAL_FEATURES;
			d.from_keyframe = flags & DF_FROM_KEYFRAME;
			d.format = "DSDA (" + std::to_string(dsda_version) + "), ";
		}
		else if (size >= p + 5 && !memcmp(data + p, "PR+UM", 5))
		{
			p += 6;
			need(size, p, 1 + 2 + 1 + 8 + 8, "UMAPINFO header");
			if (data[p] != 1 || data[p + 1] != 1 || data[p + 2] != 0 || data[p + 3] != 8 || memcmp(data + p + 4, "UMAPINFO", 8))
				refuse("its PrBoom+um extended header is not the one dsda-doom knows");
			p += 4 + 8 + 8;
			d.umapinfo = true;
			d.format = "PrBoom+um UMAPINFO, ";
		}
		else
			refuse("its extended header (version byte 255) is neither dsda's nor PrBoom+um's");
		need(size, p, 1, "header");
		ver = data[p++];
	}

	if (!((ver >= 0 && ver <= 4) || (ver >= 104 && ver <= 111) || (ver >= 200 && ver <= 214) || ver == 221))
		refuse("its version byte %d is no demo format dsda-doom knows", ver);

	if (ver >= 100 && ver < 200)
	{
		/* Doom 1.4 - 1.9, TASDoom, longtics */
		d.version = ver;
		d.longtics = ver >= 111;
		d.tasdoom = ver == 110;
		need(size, p, 12, "header");
		d.skill = data[p];
		d.episode = data[p + 1];
		d.map = data[p + 2];
		d.deathmatch = data[p + 3];
		d.respawn = data[p + 4] != 0;
		d.fast = data[p + 5] != 0;
		d.nomonsters = data[p + 6] != 0;
		d.consoleplayer = data[p + 7];
		p += 8;
		for (int i = 0; i < 4; i++) d.players[i] = data[p + i] != 0;
		p += 4;
		d.format += ver == 110 ? "TASDoom" : ver == 111 ? "Doom 1.9 longtics" : "Doom 1." + std::to_string(ver - 100);
	}
	else if (ver < 100)
	{
		/* versionless: Doom 1.0-1.2, Heretic, Hexen */
		const std::string family = family_hint.empty() ? guess_versionless_family(data, size, p - 1) : family_hint;
		d.family = family;
		d.skill = ver;
		need(size, p, 2, "header");
		d.episode = data[p];
		d.map = data[p + 1];
		p += 2;
		std::vector<bool> ingame;
		std::vector<int> classes;
		int bits;
		if (family == "hexen")
		{
			need(size, p, 16, "header");
			for (int i = 0; i < 8; i++)
			{
				ingame.push_back(data[p + 2 * i] != 0);
				classes.push_back(data[p + 2 * i + 1]);
			}
			bits = data[p];
			p += 16;
		}
		else
		{
			need(size, p, 4, "header");
			for (int i = 0; i < 4; i++)
			{
				ingame.push_back(data[p + i] != 0);
				classes.push_back(0);
			}
			bits = data[p];
			p += 4;
		}
		std::vector<std::string> extra;
		for (size_t i = 4; i < ingame.size(); i++)
			if (ingame[i]) extra.push_back(std::to_string(i + 1));
		if (!extra.empty()) refuse("players %s are in the game; the core has four", join(extra, ", ").c_str());
		for (int i = 0; i < 4; i++)
		{
			d.players[i] = ingame[i];
			d.classes[i] = classes[i];
		}
		if (family == "doom")
		{
			d.complevel = 0;
			d.format += "Doom 1.2 (or earlier)";
		}
		else
		{
			/* vvHeretic's special bits on player one's byte, OR'd with the
			 * footer's flags (the monster flags are not in the header otherwise) */
			d.raven_bits = bits;
			d.longtics = (bits & DEMOHEADER_LONGTICS) || longtics_flag;
			d.format += family == "heretic" ? "Heretic" : "Hexen";
		}
	}
	else
	{
		/* Boom and later: a signature, then the header, then the options */
		d.version = ver;
		const size_t sig_at = p;
		need(size, p, 6, "signature");
		p += 6;
		if (ver >= 200 && ver <= 202)
		{
			need(size, p, 1, "header");
			const int compat = data[p++];
			d.complevel = compat ? 7 : ver == 202 ? 9 : 8;
			d.format += "Boom 2.0" + std::to_string(ver - 200);
		}
		else if (ver == 203)
		{
			if (data[sig_at + 1] == 'B')
			{
				d.complevel = 10;
				d.format += "LxDoom";
			}
			else if (data[sig_at + 1] == 'M')
			{
				d.complevel = 11;
				p++;
				d.format += "MBF";
			}
			else
				refuse("its version 203 signature is neither LxDoom's nor MBF's");
		}
		else if (ver >= 210 && ver <= 214)
		{
			d.complevel = 13 + (ver - 210);
			d.longtics = ver == 214;
			p++;
			d.format += "PrBoom (complevel " + std::to_string(d.complevel) + ")";
		}
		else
		{
			d.complevel = 21;
			d.longtics = true;
			d.format += "MBF21";
		}
		need(size, p, 5, "header");
		d.skill = data[p];
		d.episode = data[p + 1];
		d.map = data[p + 2];
		d.deathmatch = data[p + 3];
		d.consoleplayer = data[p + 4];
		p += 5;
		size_t osize;
		if (d.complevel == 21)
		{
			need(size, p, 21, "options");
			const int count = data[p + 20];
			if (count > 25) refuse("its MBF21 options name %d comp flags; dsda-doom 0.30 knows 25", count);
			osize = 21 + (size_t)count;
			d.mbf21_options = true;
		}
		else
			osize = MBF_GAME_OPTION_SIZE;
		need(size, p, osize, "options");
		d.options.assign(data + p, data + p + osize);
		/* where its monster flags and its seed are */
		const size_t r_at = d.mbf21_options ? 3 : 6, seed_at = d.mbf21_options ? 6 : 10;
		d.respawn = d.options[r_at] != 0;
		d.fast = d.options[r_at + 1] != 0;
		d.nomonsters = d.options[r_at + 2] != 0;
		d.has_rngseed = true;
		d.rngseed = (uint32_t)d.options[seed_at] << 24 | (uint32_t)d.options[seed_at + 1] << 16 |
			(uint32_t)d.options[seed_at + 2] << 8 | d.options[seed_at + 3];
		p += osize;
		if (ver == 200) p += 256 - MBF_GAME_OPTION_SIZE;   /* killough: 2.00 kept 256 bytes of options */
		need(size, p, 32, "players");
		std::vector<std::string> extra;
		for (int i = 4; i < 32; i++)
			if (data[p + i]) extra.push_back(std::to_string(i + 1));
		if (!extra.empty()) refuse("players %s are in the game; the core has four", join(extra, ", ").c_str());
		for (int i = 0; i < 4; i++) d.players[i] = data[p + i] != 0;
		p += 32;
	}

	if (!d.players[0] && !d.players[1] && !d.players[2] && !d.players[3]) refuse("no player is in the game");
	if (d.from_keyframe) refuse("it starts from a key frame (a saved game inside the demo); a movie starts with the game");

	/* the tics, to the end marker - or, as dsda's playback ends too
	 * (dsda_EndOfPlaybackStream), where a whole tic no longer fits */
	int present = 0;
	for (int i = 0; i < 4; i++) present += d.players[i];
	const size_t base = (size_t)((d.tasdoom ? 4 : d.longtics ? 5 : 4) + (d.family != "doom" ? 2 : 0) + (d.excmd ? 1 : 0));
	for (;;)
	{
		if (p >= size || data[p] == DEMOMARKER)
		{
			p++;
			break;
		}
		if (p + base * (size_t)present > size)
		{
			d.truncated = true;
			break;
		}
		std::vector<Tic> tic;
		for (int k = 0; k < present; k++)
		{
			Tic t;
			if (d.tasdoom)
			{
				need(size, p, 4, "tics");
				t.buttons = data[p];
				t.fwd = s8(data[p + 1]);
				t.side = s8(data[p + 2]);
				t.turn = s8(data[p + 3]);
				p += 4;
			}
			else
			{
				need(size, p, d.longtics ? 5 : 4, "tics");
				t.fwd = s8(data[p]);
				t.side = s8(data[p + 1]);
				p += 2;
				if (d.longtics)
				{
					t.frac = data[p];
					t.turn = s8(data[p + 1]);
					p += 2;
				}
				else
					t.turn = s8(data[p++]);
				t.buttons = data[p++];
			}
			if (d.family != "doom")
			{
				need(size, p, 2, "tics");
				t.lookfly = data[p];
				t.arti = data[p + 1];
				p += 2;
			}
			if (d.excmd)
			{
				need(size, p, 1, "tics");
				t.has_ex = true;
				t.ex = data[p++];
				if (t.ex & (XC_SAVE | XC_LOAD))
					refuse("tic %zu saves or loads a game (dsda's casual extended commands); a movie cannot", d.tics.size());
				if (t.ex & XC_LOOK)
				{
					need(size, p, 2, "tics");
					t.look = (int16_t)(data[p] | data[p + 1] << 8);
					p += 2;
				}
			}
			tic.push_back(t);
		}
		d.tics.push_back(tic);
	}

	d.footer = read_footer(data, size, p);
	auto cmd = d.footer.find("CMDLINE");
	if (cmd != d.footer.end())
	{
		d.footer_text = cmd->second;
		for (char &c : d.footer_text)
			if (c == '\0') c = ' ';
		d.footer_args = split_args(d.footer_text);
	}
	return d;
}

/* ------------------------------------------------------------------ the footer's arguments */

struct Footer
{
	std::string iwad;
	std::vector<std::string> files, deh;
	std::vector<std::string> flags;
	std::string complevel, emulate, spechit;
	bool has_complevel = false, has_emulate = false, has_spechit = false;
	std::vector<std::pair<std::string, int>> overruns;
	bool flag(const char *f) const
	{
		for (auto &x : flags)
			if (x == f) return true;
		return false;
	}
	int overrun(const char *name, bool *found) const
	{
		int v = 0;
		*found = false;
		for (auto &o : overruns)
			if (o.first == name) v = o.second, *found = true;
		return v;
	}
};

bool is_word(char c) { return isalnum((unsigned char)c) || c == '_'; }

Footer footer_values(const std::vector<std::string> &args, const std::string &text)
{
	/* what the footer's command line says, as dsda reads it (DemoEx_GetParams;
	 * the overflows' -set by sscanf on the text, "-set name = value") */
	Footer f;
	for (size_t i = 0; i < args.size(); i++)
	{
		const std::string a = lower(args[i]);
		if (a == "-iwad" || a == "-file" || a == "-deh")
		{
			std::vector<std::string> vals;
			while (i + 1 < args.size() && (args[i + 1].empty() || args[i + 1][0] != '-')) vals.push_back(args[++i]);
			if (a == "-iwad") f.iwad = vals.empty() ? "" : vals[0];
			else (a == "-file" ? f.files : f.deh).insert((a == "-file" ? f.files : f.deh).end(), vals.begin(), vals.end());
		}
		else if ((a == "-complevel" || a == "-emulate" || a == "-spechit") && i + 1 < args.size())
		{
			if (a == "-complevel") f.complevel = args[i + 1], f.has_complevel = true;
			else if (a == "-emulate") f.emulate = args[i + 1], f.has_emulate = true;
			else f.spechit = args[i + 1], f.has_spechit = true;
			i++;
		}
		else if (a == "-solo-net" || a == "-coop_spawns" || a == "-chain_episodes" || a == "-respawn" || a == "-fast" || a == "-nomonsters")
			f.flags.push_back(a);
	}
	for (size_t at = text.find("-set"); at != std::string::npos; at = text.find("-set", at + 1))
	{
		size_t q = at + 4;
		if (q >= text.size() || !isspace((unsigned char)text[q])) continue;
		while (q < text.size() && isspace((unsigned char)text[q])) q++;
		size_t w = q;
		while (q < text.size() && is_word(text[q])) q++;
		const std::string name = text.substr(w, q - w);
		if (name.size() <= 16 || name.compare(0, 8, "overrun_") || !ends_with(name, "_emulate")) continue;
		while (q < text.size() && isspace((unsigned char)text[q])) q++;
		if (q >= text.size() || text[q] != '=') continue;
		q++;
		while (q < text.size() && isspace((unsigned char)text[q])) q++;
		size_t v = q;
		if (q < text.size() && text[q] == '-') q++;
		size_t digits = q;
		while (q < text.size() && isdigit((unsigned char)text[q])) q++;
		if (q == digits) continue;
		f.overruns.emplace_back(name, atoi(text.substr(v, q - v).c_str()));
	}
	return f;
}

/* ------------------------------------------------------------------ the declaration */

const struct dsda_game *game_by_id(const std::string &id)
{
	for (auto &g : k_games)
		if (id == g.id) return &g;
	return nullptr;
}

std::string family_of(const struct dsda_game *g)
{
	return g->format == FORMAT_HERETIC ? "heretic" : g->format == FORMAT_HEXEN ? "hexen" : "doom";
}

std::string option_leading(const char *const *options, const char *display, int leading)
{
	for (int i = 0; options[i]; i++)
	{
		char *end;
		long v = strtol(options[i], &end, 10);
		if (end != options[i] && v == leading && (*end == 0 || !isalnum((unsigned char)*end))) return options[i];
	}
	refuse("the core has no %s %d", display, leading);
}

bool declared(const std::string &name)
{
	for (int i = 0; k_setting_names[i]; i++)
		if (name == k_setting_names[i]) return true;
	return false;
}

std::string sha1_hex(const unsigned char *data, size_t size)
{
	char out[41];
	lmpi_sha1(data, size, out);
	return out;
}

/* ------------------------------------------------------------------ JSON */

struct Value
{
	enum { BOOL, INT, STR } kind;
	bool b = false;
	long long i = 0;
	std::string s;
	static Value of(bool x) { Value v; v.kind = BOOL; v.b = x; return v; }
	static Value of(long long x) { Value v; v.kind = INT; v.i = x; return v; }
	static Value of(int x) { return of((long long)x); }
	static Value of(const std::string &x) { Value v; v.kind = STR; v.s = x; return v; }
	static Value of(const char *x) { return of(std::string(x)); }
};

typedef std::vector<std::pair<std::string, Value>> Settings;

void set(Settings &s, const std::string &key, const Value &v)
{
	for (auto &kv : s)
		if (kv.first == key) { kv.second = v; return; }
	s.emplace_back(key, v);
}

std::string quote(const std::string &s)
{
	/* as Python's json.dumps: ASCII, a byte past it as its Latin-1 code point */
	std::string out = "\"";
	char buf[8];
	for (unsigned char c : s)
	{
		switch (c)
		{
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		case '\b': out += "\\b"; break;
		case '\f': out += "\\f"; break;
		default:
			if (c < 0x20 || c >= 0x7f)
			{
				snprintf(buf, sizeof buf, "\\u%04x", c);
				out += buf;
			}
			else
				out += (char)c;
		}
	}
	return out + "\"";
}

std::string json_value(const Value &v)
{
	if (v.kind == Value::BOOL) return v.b ? "true" : "false";
	if (v.kind == Value::INT) return std::to_string(v.i);
	return quote(v.s);
}

std::string json_settings(const Settings &s, const char *indent)
{
	std::string out = "{";
	for (size_t i = 0; i < s.size(); i++)
		out += std::string(i ? "," : "") + "\n" + indent + "  " + quote(s[i].first) + ": " + json_value(s[i].second);
	return out + "\n" + indent + "}";
}

/* ------------------------------------------------------------------ the input log */

std::string mnemonic(const std::string &name)
{
	std::string tail = name;
	if (name.size() > 3 && name[0] == 'P' && isdigit((unsigned char)name[1]) && name[2] == ' ') tail = name.substr(3);
	if (tail == "Fire") return "F";
	if (tail == "Use") return "U";
	if (tail == "Pause") return "P";
	if (tail == "Jump") return "J";
	if (tail == "End Player") return "E";
	if (tail == "God") return "G";
	if (tail == "No Clip") return "N";
	return tail.substr(0, 1);
}

struct Column
{
	std::string name;
	const struct dsda_input *axis;   /* NULL: a button */
};

/* the controller's active inputs, in the engine's order: a group a port (the
 * machine's first), each group's axes then its buttons, as the core declares
 * them; what a setting leaves out is inactive (dsda_input_active, the driver's) */
std::vector<std::vector<Column>> active_groups(int format, const struct dsda_activity &a)
{
	const struct dsda_controller *ctl = dsda_controller(format);
	int ngroups = 1;
	for (int i = 0; i < ctl->naxes; i++)
		if (dsda_input_active(&ctl->axes[i], 1, &a) && ctl->axes[i].port + 1 > ngroups) ngroups = ctl->axes[i].port + 1;
	for (int i = 0; i < ctl->nbuttons; i++)
		if (dsda_input_active(&ctl->buttons[i], 0, &a) && ctl->buttons[i].port + 1 > ngroups) ngroups = ctl->buttons[i].port + 1;
	std::vector<std::vector<Column>> groups(ngroups);
	for (int i = 0; i < ctl->naxes; i++)
		if (dsda_input_active(&ctl->axes[i], 1, &a)) groups[ctl->axes[i].port].push_back({ ctl->axes[i].name, &ctl->axes[i] });
	for (int i = 0; i < ctl->nbuttons; i++)
		if (dsda_input_active(&ctl->buttons[i], 0, &a)) groups[ctl->buttons[i].port].push_back({ ctl->buttons[i].name, nullptr });
	return groups;
}

std::string input_log(const Demo &demo, int format, const struct dsda_activity &a, long *frames)
{
	const std::vector<std::vector<Column>> groups = active_groups(format, a);
	std::unordered_map<std::string, bool> active;
	std::string key;
	for (auto &g : groups)
	{
		key += "#";
		for (auto &c : g)
		{
			key += c.name + "|";
			active[c.name] = true;
		}
	}
	const bool hexen = format == FORMAT_HEXEN;
	std::string out = "[Input]\nLogKey:" + key + "\n";
	std::unordered_map<std::string, int> axes;
	std::unordered_map<std::string, bool> buttons;
	std::vector<int> present;
	for (int i = 0; i < 4; i++)
		if (demo.players[i]) present.push_back(i);
	char num[32];
	for (size_t ticno = 0; ticno < demo.tics.size(); ticno++)
	{
		axes.clear();
		buttons.clear();
		const std::vector<Tic> &tic = demo.tics[ticno];
		for (size_t k = 0; k < tic.size(); k++)
		{
			const Tic &t = tic[k];
			const std::string P = "P" + std::to_string(present[k] + 1) + " ";
			axes[P + "Run Speed"] = t.fwd;
			axes[P + "Strafe Speed"] = t.side;
			axes[P + "Turn Speed"] = t.turn;
			axes[P + "Turn Speed Frac."] = t.frac;
			const int b = t.buttons;
			if (b & BT_SPECIAL)
			{
				/* a special command: in Doom only its pause does anything (dsda's
				 * G_Ticker), and it has no buttons after; the Raven games keep its
				 * low bits until the player thinks (a dead player's use, the
				 * intermission's skip read them) - Special */
				if (demo.family == "doom" || (b & 0x7F) == 0 || (b & 0x7F) == 1)
				{
					if ((b & BT_SPECIALMASK) == BT_PAUSE) buttons[P + "Pause"] = true;
				}
				else
					axes[P + "Special"] = b & 0x7F;
			}
			else
			{
				if (b & BT_ATTACK) buttons[P + "Fire"] = true;
				if (b & BT_USE) buttons[P + "Use"] = true;
				/* the weapon's bits as they are (BT_WEAPONMASK, four), + 1 */
				if (b & BT_CHANGE) axes[P + "Weapon Select"] = ((b >> 3) & 15) + 1;
			}
			if (demo.family != "doom")
			{
				const int look = t.lookfly & 15, fly = t.lookfly >> 4;
				axes[P + "Look"] = look > 8 ? look - 16 : look;
				axes[P + "Fly"] = fly > 8 ? fly - 16 : fly;
				if (!hexen && (t.arti & ~AFLAG_MASK)) refuse("tic %zu: Heretic's artifact byte %d has Hexen's flags", ticno, t.arti);
				axes[P + "Artifact"] = t.arti & AFLAG_MASK;
				if (hexen)
				{
					if (t.arti & AFLAG_JUMP) buttons[P + "Jump"] = true;
					if (t.arti & AFLAG_SUICIDE) buttons[P + "End Player"] = true;
				}
			}
			if (t.has_ex)
			{
				if (t.ex & XC_JUMP)
				{
					if (hexen) refuse("tic %zu: an extended jump in Hexen, which jumps with its artifact flag", ticno);
					buttons[P + "Jump"] = true;
				}
				if (t.ex & XC_LOOK) axes[P + "Free Look"] = t.look;
				if (t.ex & XC_GOD) buttons[P + "God"] = true;
				if (t.ex & XC_NOCLIP) buttons[P + "No Clip"] = true;
			}
		}
		std::string line = "|";
		for (auto &g : groups)
		{
			for (auto &c : g)
			{
				if (c.axis)
				{
					auto v = axes.find(c.name);
					const int value = v == axes.end() ? c.axis->neutral : v->second;
					if (value < c.axis->min || value > c.axis->max)
						refuse("tic %zu: %s is %d, past the core's %d..%d", ticno, c.name.c_str(), value, (int)c.axis->min, (int)c.axis->max);
					snprintf(num, sizeof num, "%5d,", value);
					line += num;
				}
				else
					line += buttons.count(c.name) ? mnemonic(c.name) : ".";
			}
			line += "|";
		}
		out += line + "\n";
		/* every value the tic set must have a column, or the movie loses it */
		for (auto &v : axes)
			if (v.second && !active.count(v.first))
				refuse("tic %zu: %s is %d, and the core's controller has no such input active", ticno, v.first.c_str(), v.second);
		for (auto &v : buttons)
			if (!active.count(v.first))
				refuse("tic %zu: %s is pressed, and the core's controller has no such input active", ticno, v.first.c_str());
	}
	*frames = (long)demo.tics.size();
	return out + "[/Input]\n";
}

/* ------------------------------------------------------------------ files */

struct Reader
{
	const struct lmpi_options *o;
	const unsigned char *read(const std::string &name, size_t *size, std::string *found = nullptr) const
	{
		*size = 0;
		const char *as = nullptr;
		const unsigned char *b = o->read ? o->read(o->ctx, name.c_str(), size, found ? &as : nullptr) : nullptr;
		if (found) *found = as ? as : name;
		return b;
	}
};

/* a WAD's last lump of this name */
bool wad_lump(const unsigned char *data, size_t size, const char *name, std::string *out)
{
	if (size < 12 || (memcmp(data, "IWAD", 4) && memcmp(data, "PWAD", 4))) return false;
	int32_t n, ofs;
	memcpy(&n, data + 4, 4);
	memcpy(&ofs, data + 8, 4);
	bool found = false;
	for (int32_t i = 0; i < n; i++)
	{
		size_t e = (size_t)ofs + 16 * (size_t)i;
		if (ofs < 0 || e + 16 > size) break;
		int32_t pos, len;
		char nm[9] = { 0 };
		memcpy(&pos, data + e, 4);
		memcpy(&len, data + e + 4, 4);
		memcpy(nm, data + e + 8, 8);
		std::string upper = nm;
		for (char &c : upper) c = (char)toupper((unsigned char)c);
		if (upper == name && pos >= 0 && len >= 0 && (size_t)pos + (size_t)len <= size)
		{
			*out = std::string((const char *)data + pos, (size_t)len);
			found = true;
		}
	}
	return found;
}

/* the warp number that reaches Hexen's map gamemap: the engine reads the last
 * MAPINFO loaded (a PWAD's replaces the IWAD's), where each map's warptrans
 * defaults to its number, and -warp N goes to the first map whose warptrans is
 * N (P_TranslateMap) */
int hexen_warp(int gamemap, const std::vector<std::pair<const unsigned char *, size_t>> &wads)
{
	std::string text;
	bool have = false;
	for (auto &w : wads)
	{
		std::string lump;
		if (wad_lump(w.first, w.second, "MAPINFO", &lump)) text = lump, have = true;
	}
	if (!have) refuse("no MAPINFO in the IWAD or the PWADs: Hexen's maps are reached by their warp numbers");
	/* comments (';' to the line's end) out, then words and quoted strings */
	std::string clean;
	bool comment = false;
	for (char c : text)
	{
		if (c == ';') comment = true;
		if (c == '\n') comment = false;
		if (!comment) clean += c;
	}
	std::vector<std::string> tokens;
	for (size_t i = 0; i < clean.size();)
	{
		if (isspace((unsigned char)clean[i])) { i++; continue; }
		size_t j = i;
		if (clean[i] == '"')
		{
			size_t close = clean.find('"', i + 1);
			if (close != std::string::npos) j = close + 1;
			else while (j < clean.size() && !isspace((unsigned char)clean[j])) j++;
		}
		else
			while (j < clean.size() && !isspace((unsigned char)clean[j])) j++;
		tokens.push_back(clean.substr(i, j - i));
		i = j;
	}
	auto digits = [](const std::string &s) { return !s.empty() && s.find_first_not_of("0123456789") == std::string::npos; };
	std::map<int, int> trans;
	int current = -1;
	for (size_t i = 0; i < tokens.size();)
	{
		const std::string t = lower(tokens[i]);
		if (t == "map" && i + 1 < tokens.size() && digits(tokens[i + 1]))
		{
			current = atoi(tokens[i + 1].c_str());
			trans[current] = current;
			i += 2;
			continue;
		}
		const std::string &next = i + 1 < tokens.size() ? tokens[i + 1] : std::string();
		if (t == "warptrans" && current >= 0 && (digits(next) || (next.size() > 1 && next[0] == '-' && digits(next.substr(1)))))
		{
			trans[current] = atoi(next.c_str());
			i += 2;
			continue;
		}
		i++;
	}
	if (!trans.count(gamemap)) refuse("MAPINFO has no map %d", gamemap);
	const int warp = trans[gamemap];
	int first = -1;
	for (int m = 1; m < 99; m++)
		if (trans.count(m) && trans[m] == warp) { first = m; break; }
	if (first != gamemap)
		refuse("Hexen's -warp %d goes to map %d, not the demo's %d: the core starts a game by its warp number", warp, first, gamemap);
	return warp;
}

/* ------------------------------------------------------------------ the import */

const char *const k_final_games[] = { "tnt", "plutonia" };

/* retail: the IWAD has a fourth episode (E4M1), upstream's gamemode retail */
int complevel_of(const Demo &demo, const std::string &game, bool retail, const Footer &f)
{
	if (!(demo.version >= 104 && demo.version <= 111)) return demo.complevel;  /* 1.2's 0, a Boom-or-later header's own */
	/* 1.4-1.9, TASDoom, longtics: G_GetOriginalDoomCompatLevel - the footer's
	 * -complevel first, then the version and the game */
	if (f.has_complevel)
	{
		const std::string &c = f.complevel;
		size_t from = c.size() && c[0] == '-' ? 1 : 0;
		if (from < c.size() && c.find_first_not_of("0123456789", from) == std::string::npos) return atoi(c.c_str());
	}
	if (demo.version == 110) return 6;
	if (demo.version < 107) return 1;
	if (retail) return 3;
	for (auto g : k_final_games)
		if (game == g) return 4;
	return 2;
}

struct Result
{
	Settings settings;
	std::string game, iwad, firmware_id, firmware_sha1;
	std::vector<std::pair<std::string, std::string>> files;  /* name, sha1 */
	std::vector<std::string> notes;
	std::string input;
	long frames = 0;
	Demo demo;
	std::string port;
};

/* the game an IWAD is: a dump the core has been tested with (by its hash), else its file name (upstream's
 * AddIWAD's names), else its lumps; "" none. label: what it is, for the summary */
std::string game_of_iwad(const unsigned char *b, size_t n, const std::string &name, std::string *label)
{
	const std::string sha1 = sha1_hex(b, n);
	for (auto &k : k_known_iwads)
		if (sha1 == k.sha1) { *label = k.label; return k.game; }
	*label = "an IWAD the core has not been tested with";
	static const char *const names[][2] = {
		{ "doom2.wad", "doom2" }, { "doom2f.wad", "doom2" }, { "doom.wad", "doom" }, { "doomu.wad", "doom" },
		{ "doom1.wad", "doom" }, { "tnt.wad", "tnt" }, { "plutonia.wad", "plutonia" }, { "heretic.wad", "heretic" },
		{ "heretic1.wad", "heretic" }, { "hexen.wad", "hexen" }, { "chex.wad", "chex" },
		{ "freedoom1.wad", "freedoom1" }, { "freedoom2.wad", "freedoom2" }, { "freedm.wad", "freedoom2" } };
	const std::string base = lower(basename_of(name));
	for (auto &e : names)
		if (base == e[0]) return e[1];
	std::string lump;
	auto has = [&](const char *l) { return wad_lump(b, n, l, &lump); };
	if (has("FREEDOOM")) return has("MAP01") ? "freedoom2" : "freedoom1";
	if (has("MAPINFO") && has("STARTUP") && has("MAP01")) return "hexen";
	if (has("ADVISOR") && has("E1M1")) return "heretic";
	if (has("E1M1")) return "doom";
	if (has("MAP01")) return "doom2";
	return "";
}

Result import(const unsigned char *data, size_t size, const struct lmpi_options *o)
{
	Reader rd{ o };
	Result r;
	/* the IWAD - the one given, else the footer's -iwad - says the game; any release of it plays, the
	 * compatibility level says the rules, and the project pins the file's own hash */
	const unsigned char *iwad = nullptr;
	size_t iwad_size = 0;
	std::string iwad_name, iwad_label, game;
	if (o->iwad)
	{
		iwad = rd.read(o->iwad, &iwad_size);
		iwad_name = o->iwad;
		if (iwad) game = game_of_iwad(iwad, iwad_size, iwad_name, &iwad_label);
	}
	/* the demo first: its own refusals do not depend on the IWAD */
	Demo demo = parse_demo(data, size, game.empty() ? std::string() : family_of(game_by_id(game)), o->longtics);
	if (o->iwad && !iwad) refuse("%s cannot be read", o->iwad);
	if (o->iwad && game.empty()) refuse("%s is the IWAD of none of the games the core plays: its name and its lumps say none", o->iwad);
	if (o->longtics && demo.family == "doom")
		refuse("longtics are for Heretic and Hexen demos; a Doom demo's format says its turning");
	const Footer f = footer_values(demo.footer_args, demo.footer_text);
	if (!iwad && !f.iwad.empty())
	{
		iwad = rd.read(f.iwad, &iwad_size);
		if (iwad)
		{
			iwad_name = f.iwad;
			game = game_of_iwad(iwad, iwad_size, iwad_name, &iwad_label);
			if (game.empty()) refuse("its footer's IWAD, %s, is the IWAD of none of the games the core plays", f.iwad.c_str());
		}
	}
	if (!iwad)
		refuse("the demo does not say which IWAD it is for%s: give its IWAD",
			f.iwad.empty() ? "" : (" beyond its footer's name, " + f.iwad + ", which is " + (o->missing_hint ? o->missing_hint : "not at hand")).c_str());
	const struct dsda_game *gm = game_by_id(game);
	const std::string family = family_of(gm);
	if (family != demo.family)
	{
		std::string fam = demo.family;
		fam[0] = (char)toupper((unsigned char)fam[0]);
		refuse("it is a %s demo, and %s is %s's IWAD", fam.c_str(), basename_of(iwad_name).c_str(), game.c_str());
	}
	std::string e4;
	const bool retail = wad_lump(iwad, iwad_size, "E4M1", &e4);

	Settings &s = r.settings;
	set(s, "game", Value::of(game));
	if (family == "doom")
		set(s, "compatibilityLevel", Value::of(option_leading(k_options_compatibilityLevel, "Compatibility Level", complevel_of(demo, game, retail, f))));
	if (demo.skill > 4) refuse("its skill %d is past Nightmare (4)", demo.skill);
	set(s, "skillLevel", Value::of(k_options_skillLevel[demo.skill]));
	set(s, "initialEpisode", Value::of(demo.episode));
	set(s, "initialMap", Value::of(demo.map));
	set(s, "multiplayerMode", Value::of(k_options_multiplayerMode[demo.deathmatch < 2 ? demo.deathmatch : 2]));
	std::vector<std::string> flags = f.flags;
	if (o->respawn) flags.push_back("-respawn");
	if (o->fast) flags.push_back("-fast");
	if (o->nomonsters) flags.push_back("-nomonsters");
	auto has_flag = [&](const char *x) {
		for (auto &y : flags)
			if (y == x) return true;
		return false;
	};
	bool respawn, fast, nomonsters;
	if (demo.respawn >= 0)
		respawn = demo.respawn, fast = demo.fast, nomonsters = demo.nomonsters;
	else
	{
		/* 1.2 and the Raven games: the footer's flags (or the caller's), and the
		 * Raven games' bits on player one's byte */
		respawn = has_flag("-respawn") || (demo.raven_bits & DEMOHEADER_RESPAWN);
		fast = has_flag("-fast");
		nomonsters = has_flag("-nomonsters") || (demo.raven_bits & DEMOHEADER_NOMONSTERS);
		if (family == "doom" && !has_flag("-respawn") && !has_flag("-fast") && !has_flag("-nomonsters"))
			r.notes.push_back("a 1.2 demo does not hold its monster flags: none assumed (respawn, fast, no monsters: say them)");
	}
	set(s, "monstersRespawn", Value::of(respawn));
	set(s, "fastMonsters", Value::of(fast));
	set(s, "noMonsters", Value::of(nomonsters));
	for (int i = 0; i < 4; i++)
	{
		set(s, "player" + std::to_string(i + 1) + "Present", Value::of(demo.players[i]));
		if (family == "hexen")
		{
			if (demo.classes[i] > 2) refuse("player %d's class is %d; Hexen's are 0-2", i + 1, demo.classes[i]);
			set(s, "player" + std::to_string(i + 1) + "Class", Value::of(k_options_playerClass[demo.classes[i]]));
		}
	}
	int display = 0;
	if (demo.consoleplayer < 4 && demo.players[demo.consoleplayer]) display = demo.consoleplayer + 1;
	else
		for (int i = 0; i < 4 && !display; i++)
			if (demo.players[i]) display = i + 1;
	set(s, "displayPlayer", Value::of(display));
	set(s, "turningResolution", Value::of(k_options_turningResolution[demo.longtics ? 0 : 1]));
	set(s, "renderWipescreen", Value::of(false));
	set(s, "strafe50Turns", Value::of("Allow"));
	set(s, "preventLevelExit", Value::of(false));
	set(s, "preventGameEnd", Value::of(false));
	set(s, "pistolStart", Value::of(false));
	if (family == "doom")
	{
		if (demo.has_rngseed) set(s, "rngSeed", Value::of((long long)(int32_t)demo.rngseed));
		std::string hex;
		char h[3];
		for (uint8_t b : demo.options)
		{
			snprintf(h, sizeof h, "%02X", b);
			hex += h;
		}
		set(s, "demoOptions", Value::of(hex));
		set(s, "spechitAddress", Value::of(f.has_spechit ? (long long)strtoll(f.spechit.c_str(), nullptr, 0) : 0LL));
		static const char *const overruns[6][2] = {
			{ "overrun_spechit_emulate", "overrunSpechit" }, { "overrun_reject_emulate", "overrunReject" },
			{ "overrun_intercept_emulate", "overrunIntercept" }, { "overrun_playeringame_emulate", "overrunPlayeringame" },
			{ "overrun_donut_emulate", "overrunDonut" }, { "overrun_missedbackside_emulate", "overrunMissedBackside" } };
		for (auto &ov : overruns)
		{
			bool found;
			int v = f.overrun(ov[0], &found);
			if (found) set(s, ov[1], Value::of(v != 0));
		}
	}
	set(s, "soloNet", Value::of(has_flag("-solo-net")));
	set(s, "coopSpawns", Value::of(has_flag("-coop_spawns")));
	set(s, "chainEpisodes", Value::of(has_flag("-chain_episodes")));
	set(s, "emulatePrBoom", Value::of(f.has_emulate ? f.emulate : std::string()));
	set(s, "extendedCommands", Value::of(k_options_extendedCommands[(demo.excmd ? 1 : 0) + (demo.casual ? 1 : 0)]));
	for (auto &kv : s)
		if (!declared(kv.first)) refuse("the core declares no setting %s", kv.first.c_str());

	/* the firmware: the game's IWAD, the file given (its own hash) */
	r.firmware_id = gm->iwad;
	r.firmware_sha1 = sha1_hex(iwad, iwad_size);

	/* the files: the ones given, else the footer's -file and -deh */
	std::vector<std::string> names;
	if (o->no_pwads)
	{
		if (!f.files.empty() || !f.deh.empty())
		{
			std::vector<std::string> all = f.files;
			all.insert(all.end(), f.deh.begin(), f.deh.end());
			r.notes.push_back("its footer names " + join(all, ", ") + "; none taken (no PWADs)");
		}
	}
	else if (o->pwads)
		for (int i = 0; i < o->npwads; i++) names.push_back(o->pwads[i]);
	else
	{
		std::vector<std::string> all = f.files;
		all.insert(all.end(), f.deh.begin(), f.deh.end());
		for (auto &n : all)
		{
			const std::string base = lower(basename_of(n));
			if (base == "dsda-doom.wad" || base == "prboom-plus.wad" || base == "prboom.wad") continue;  /* the port's own data, the core's */
			names.push_back(n);
		}
	}
	std::vector<std::string> wads, patches;
	for (auto &n : names)
	{
		const std::string l = lower(n);
		(ends_with(l, ".deh") || ends_with(l, ".bex") ? patches : wads).push_back(n);
	}
	std::vector<std::pair<const unsigned char *, size_t>> wad_bytes;
	for (auto *list : { &wads, &patches })
		for (auto &n : *list)
		{
			size_t sz;
			std::string found;
			const unsigned char *b = rd.read(n, &sz, &found);
			if (!b)
				refuse("its footer names %s, which is %s", n.c_str(), o->missing_hint ? o->missing_hint : "not at hand");
			r.files.emplace_back(basename_of(found), sha1_hex(b, sz));
			if (list == &wads) wad_bytes.emplace_back(b, sz);
		}
	if (r.files.empty() && (!f.files.empty() || !f.deh.empty()) && !o->pwads && !o->no_pwads)
		r.notes.push_back("its footer names no PWAD the core needs");

	if (game == "hexen")
	{
		/* the core's initial map is what -warp takes, as BizHawk's */
		std::vector<std::pair<const unsigned char *, size_t>> all;
		all.emplace_back(iwad, iwad_size);
		all.insert(all.end(), wad_bytes.begin(), wad_bytes.end());
		const int warp = hexen_warp(demo.map, all);
		set(s, "initialMap", Value::of(warp));
		if (warp != demo.map) r.notes.push_back("Hexen's map " + std::to_string(demo.map) + " is warp " + std::to_string(warp));
	}

	if (demo.truncated) r.notes.push_back("it has no end marker: its tics run to the file's end, as dsda plays them");

	struct dsda_activity a;
	for (int i = 0; i < 4; i++) a.present[i] = demo.players[i];
	a.longtics = demo.longtics;
	a.extended_commands = (demo.excmd ? 1 : 0) + (demo.casual ? 1 : 0);
	r.input = input_log(demo, gm->format, a, &r.frames);
	r.game = game;
	r.iwad = game + " (" + iwad_label + ")";
	auto port = demo.footer.find("PORTNAME");
	if (port != demo.footer.end())
	{
		/* as Python's strip("\0\n ") */
		const std::string &p = port->second;
		auto edge = [](char c) { return c == '\0' || c == '\n' || c == ' '; };
		size_t b0 = 0, e0 = p.size();
		while (b0 < e0 && edge(p[b0])) b0++;
		while (e0 > b0 && edge(p[e0 - 1])) e0--;
		r.port = p.substr(b0, e0 - b0);
	}
	r.demo = std::move(demo);
	return r;
}

std::string players_text(const Demo &d)
{
	std::vector<std::string> p;
	for (int i = 0; i < 4; i++)
		if (d.players[i]) p.push_back(std::to_string(i + 1));
	return join(p, ", ");
}

std::string parts_json(const Result &r)
{
	std::string out = "{\n";
	out += "  \"format\": " + quote(r.demo.format) + ",\n";
	out += "  \"tics\": " + std::to_string(r.demo.tics.size()) + ",\n";
	out += "  \"players\": [" + players_text(r.demo) + "],\n";
	out += "  \"footer\": " + quote(join(r.demo.footer_args, " ")) + ",\n";
	out += "  \"port\": " + quote(r.port) + ",\n";
	out += "  \"game\": " + quote(r.game) + ",\n";
	out += "  \"settings\": " + json_settings(r.settings, "  ") + ",\n";
	out += "  \"firmware\": [{\"id\": " + quote(r.firmware_id) + ", \"sha1\": " + quote(r.firmware_sha1) + "}],\n";
	out += "  \"files\": [";
	for (size_t i = 0; i < r.files.size(); i++)
		out += std::string(i ? ", " : "") + "{\"name\": " + quote(r.files[i].first) + ", \"sha1\": " + quote(r.files[i].second) + ", \"slot\": \"pwad\"}";
	out += "],\n  \"notes\": [";
	for (size_t i = 0; i < r.notes.size(); i++) out += std::string(i ? ", " : "") + quote(r.notes[i]);
	out += "],\n  \"frames\": " + std::to_string(r.frames) + ",\n";
	out += "  \"input\": " + quote(r.input) + "\n}\n";
	return out;
}

std::string project_json(const Result &r, const struct lmpi_project *p)
{
	std::string description = "Imported from " + std::string(p->source ? p->source : "a demo") + ": " + r.demo.format + ", " +
		std::to_string(r.demo.tics.size()) + " tics" + (r.port.empty() ? "" : ", recorded with " + r.port) + ".";
	std::string out = "{\n";
	out += "  \"title\": " + quote(p->title ? p->title : "") + ",\n";
	out += "  \"description\": " + quote(description) + ",\n";
	out += "  \"core\": {\n    \"name\": " + quote(p->core_name ? p->core_name : "DSDA-Doom") + ",\n    \"version\": " +
		quote(p->core_version ? p->core_version : "") + ",\n    \"sha1\": " + quote(p->core_sha1 ? p->core_sha1 : "") + "\n  },\n";
	out += "  \"rerecords\": 0,\n  \"files\": [";
	for (size_t i = 0; i < r.files.size(); i++)
		out += std::string(i ? "," : "") + "\n    {\n      \"name\": " + quote(r.files[i].first) + ",\n      \"sha1\": " +
			quote(r.files[i].second) + ",\n      \"slot\": \"pwad\"\n    }";
	out += r.files.empty() ? "],\n" : "\n  ],\n";
	out += "  \"settings\": " + json_settings(r.settings, "  ") + ",\n";
	out += "  \"firmware\": [\n    {\n      \"id\": " + quote(r.firmware_id) + ",\n      \"sha1\": " + quote(r.firmware_sha1) + "\n    }\n  ],\n";
	out += "  \"input\": " + quote(r.input) + ",\n";
	out += "  \"markers\": [],\n  \"branches\": [],\n";
	out += "  \"headers\": {\n    \"MovieVersion\": \"Chimera Tasproj v1.1\",\n    \"Platform\": \"Doom\",\n";
	out += "    \"LastInputFrame\": " + quote(std::to_string(r.frames > 0 ? r.frames - 1 : 0)) + ",\n";
	out += "    \"VsyncNumerator\": \"35\",\n    \"VsyncDenominator\": \"1\"\n  }\n}\n";
	return out;
}

} /* namespace */

extern "C" char *lmpi_import(const unsigned char *demo, size_t size, const struct lmpi_options *o,
	const struct lmpi_project *project, struct lmpi_summary *summary, char *err, size_t errsize)
{
	try
	{
		Result r = import(demo, size, o);
		if (summary)
		{
			memset(summary, 0, sizeof *summary);
			snprintf(summary->format, sizeof summary->format, "%s", r.demo.format.c_str());
			snprintf(summary->iwad, sizeof summary->iwad, "%s", r.iwad.c_str());
			for (auto &kv : r.settings)
				if (kv.first == "compatibilityLevel") snprintf(summary->complevel, sizeof summary->complevel, "%s", kv.second.s.c_str());
			snprintf(summary->players, sizeof summary->players, "%s", players_text(r.demo).c_str());
			snprintf(summary->footer, sizeof summary->footer, "%s", join(r.demo.footer_args, " ").c_str());
			snprintf(summary->port, sizeof summary->port, "%s", r.port.c_str());
			snprintf(summary->notes, sizeof summary->notes, "%s", join(r.notes, "\n").c_str());
			summary->tics = (long)r.demo.tics.size();
		}
		const std::string json = project ? project_json(r, project) : parts_json(r);
		char *out = (char *)malloc(json.size() + 1);
		if (!out)
		{
			snprintf(err, errsize, "out of memory");
			return nullptr;
		}
		memcpy(out, json.c_str(), json.size() + 1);
		return out;
	}
	catch (const DemoError &e)
	{
		snprintf(err, errsize, "%s", e.what.c_str());
		return nullptr;
	}
}

/* ------------------------------------------------------------------ SHA1 (FIPS 180-1) */

extern "C" void lmpi_sha1(const unsigned char *data, size_t size, char out[41])
{
	uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
	auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
	auto block = [&](const unsigned char *b) {
		uint32_t w[80];
		for (int i = 0; i < 16; i++) w[i] = (uint32_t)b[4 * i] << 24 | (uint32_t)b[4 * i + 1] << 16 | (uint32_t)b[4 * i + 2] << 8 | b[4 * i + 3];
		for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
		uint32_t a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
		for (int i = 0; i < 80; i++)
		{
			uint32_t f, k;
			if (i < 20) f = (bb & c) | (~bb & d), k = 0x5A827999;
			else if (i < 40) f = bb ^ c ^ d, k = 0x6ED9EBA1;
			else if (i < 60) f = (bb & c) | (bb & d) | (c & d), k = 0x8F1BBCDC;
			else f = bb ^ c ^ d, k = 0xCA62C1D6;
			uint32_t t = rol(a, 5) + f + e + k + w[i];
			e = d;
			d = c;
			c = rol(bb, 30);
			bb = a;
			a = t;
		}
		h[0] += a;
		h[1] += bb;
		h[2] += c;
		h[3] += d;
		h[4] += e;
	};
	size_t i = 0;
	for (; i + 64 <= size; i += 64) block(data + i);
	unsigned char tail[128] = { 0 };
	size_t rest = size - i;
	memcpy(tail, data + i, rest);
	tail[rest] = 0x80;
	size_t len = rest + 1 + 8 <= 64 ? 64 : 128;
	const uint64_t bits = (uint64_t)size * 8;
	for (int k = 0; k < 8; k++) tail[len - 1 - k] = (unsigned char)(bits >> (8 * k));
	block(tail);
	if (len == 128) block(tail + 64);
	for (int k = 0; k < 5; k++) snprintf(out + 8 * k, 9, "%08X", h[k]);
}
