/* lmp-import - a Doom-engine demo (.lmp) as a Chimera project for the DSDA
 * core: the core's own importer (waterbox/lmp-import.cpp, its ImportMovie
 * export's) with what a command line adds - finding the IWAD and the PWADs in
 * folders, the package's identity, and the .chimeraProject written out.
 *
 * usage: lmp-import <demo.lmp> [-o <out.chimeraProject>] [--version <id> | --iwad <file>]
 *                   [--wads <dir>]... [--pwad <file>]... [--no-pwads] [--longtics]
 *                   [--respawn] [--fast] [--nomonsters] [--package <dsda.chimeraCore>]
 *                   [--title <text>] [--info]
 *
 *   --version, --iwad  the IWAD's release (a version id, or the file itself, found
 *                      by its hash); without them, the footer's -iwad and the
 *                      format decide when they can
 *   --wads             folders where the demo's PWADs and patches (by the footer's
 *                      names) and its IWAD are looked for
 *   --pwad             the PWADs and patches, in order, when the footer does not
 *                      name them (it wins over the footer)
 *   --no-pwads         none, whatever the footer names (an IWAD's own demos: a fix
 *                      their authors recorded with, now in the IWAD)
 *   --longtics         a Heretic or Hexen demo recorded with -longtics whose header
 *                      does not say so (Hexen+'s, before vvHeretic's flag)
 *   --respawn, --fast, --nomonsters
 *                      a 1.2 demo's monster flags, which it does not hold
 *   --package          the core package: its version and hash pin the project
 *   --info             prints the imported parts (JSON) and writes nothing
 */
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <map>
#include <string>
#include <sys/stat.h>
#include <vector>

#include <zlib.h>

#include "lmp-import.h"

namespace
{

bool read_file(const std::string &path, std::vector<unsigned char> *out)
{
	FILE *f = fopen(path.c_str(), "rb");
	if (!f) return false;
	out->clear();
	unsigned char buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof buf, f)) > 0) out->insert(out->end(), buf, buf + n);
	fclose(f);
	return true;
}

bool is_file(const std::string &path)
{
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

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

/* the files the importer asks for: given paths as they are, other names found
 * in the folders whatever their case */
struct Files
{
	std::vector<std::string> dirs;
	std::vector<std::string> paths;   /* given: --iwad, --pwad */
	std::map<std::string, std::vector<unsigned char>> cache;

	std::string find(const std::string &name) const
	{
		for (auto &p : paths)
			if (p == name) return p;
		const std::string want = lower(basename_of(name));
		for (auto &d : dirs)
		{
			DIR *dir = opendir(d.c_str());
			if (!dir) continue;
			std::vector<std::string> names;
			while (struct dirent *e = readdir(dir)) names.push_back(e->d_name);
			closedir(dir);
			std::sort(names.begin(), names.end());
			for (auto &n : names)
				if (lower(n) == want && is_file(d + "/" + n)) return d + "/" + n;
		}
		return std::string();
	}

	static const unsigned char *read(void *ctx, const char *name, size_t *size, const char **found_name)
	{
		Files *self = (Files *)ctx;
		const std::string path = self->find(name);
		if (path.empty()) return nullptr;
		auto it = self->cache.find(path);
		if (it == self->cache.end())
		{
			std::vector<unsigned char> bytes;
			if (!read_file(path, &bytes)) return nullptr;
			it = self->cache.emplace(path, std::move(bytes)).first;
		}
		*size = it->second.size();
		if (found_name) *found_name = it->first.c_str();   /* the path; the importer takes its file name */
		return it->second.data();
	}
};

/* a package's build.json (a zip entry, stored or deflated) */
std::string zip_entry(const std::vector<unsigned char> &z, const char *want)
{
	auto u16 = [&](size_t at) { return (unsigned)z[at] | (unsigned)z[at + 1] << 8; };
	auto u32 = [&](size_t at) { return (unsigned long)u16(at) | (unsigned long)u16(at + 2) << 16; };
	if (z.size() < 22) return std::string();
	size_t eocd = std::string::npos;
	for (size_t i = z.size() - 22 + 1; i-- > 0 && z.size() - i < 22 + 65536;)
		if (u32(i) == 0x06054b50) { eocd = i; break; }
	if (eocd == std::string::npos) return std::string();
	size_t at = u32(eocd + 16);
	const unsigned count = u16(eocd + 10);
	for (unsigned k = 0; k < count && at + 46 <= z.size(); k++)
	{
		if (u32(at) != 0x02014b50) break;
		const unsigned method = u16(at + 10), nlen = u16(at + 28), xlen = u16(at + 30), clen = u16(at + 32);
		const unsigned long csize = u32(at + 20), usize = u32(at + 24), local = u32(at + 42);
		const std::string name((const char *)&z[at + 46], nlen);
		at += 46 + nlen + xlen + clen;
		if (name != want || local + 30 > z.size()) continue;
		const size_t data = local + 30 + u16(local + 26) + u16(local + 28);
		if (data + csize > z.size()) return std::string();
		if (method == 0) return std::string((const char *)&z[data], csize);
		if (method != 8) return std::string();
		std::string out(usize, '\0');
		z_stream s;
		memset(&s, 0, sizeof s);
		if (inflateInit2(&s, -15) != Z_OK) return std::string();
		s.next_in = (Bytef *)&z[data];
		s.avail_in = (uInt)csize;
		s.next_out = (Bytef *)&out[0];
		s.avail_out = (uInt)usize;
		const int r = inflate(&s, Z_FINISH);
		inflateEnd(&s);
		return r == Z_STREAM_END ? out : std::string();
	}
	return std::string();
}

/* "version" at a JSON object's top level (build.json's, written by build-package.sh) */
std::string top_string(const std::string &json, const char *key)
{
	int depth = 0;
	const std::string want = std::string("\"") + key + "\"";
	for (size_t i = 0; i < json.size(); i++)
	{
		const char c = json[i];
		if (c == '"')
		{
			size_t end = i + 1;
			while (end < json.size() && json[end] != '"') end += json[end] == '\\' ? 2 : 1;
			if (depth == 1 && json.compare(i, want.size(), want) == 0)
			{
				size_t v = json.find('"', json.find(':', end) + 1);
				if (v == std::string::npos) return std::string();
				size_t w = v + 1;
				while (w < json.size() && json[w] != '"') w += json[w] == '\\' ? 2 : 1;
				return json.substr(v + 1, w - v - 1);
			}
			i = end;
		}
		else if (c == '{' || c == '[') depth++;
		else if (c == '}' || c == ']') depth--;
	}
	return std::string();
}

void usage()
{
	fprintf(stderr, "usage: lmp-import <demo.lmp> [-o <out.chimeraProject>] [--version <id> | --iwad <file>]\n"
		"                  [--wads <dir>]... [--pwad <file>]... [--no-pwads] [--longtics]\n"
		"                  [--respawn] [--fast] [--nomonsters] [--package <dsda.chimeraCore>]\n"
		"                  [--title <text>] [--info]\n");
	exit(2);
}

} /* namespace */

int main(int argc, char **argv)
{
	const char *demo_path = nullptr, *out = nullptr, *version = nullptr, *iwad = nullptr, *package = nullptr, *title = nullptr;
	std::vector<std::string> pwads;
	bool have_pwads = false, info = false;
	struct lmpi_options o;
	memset(&o, 0, sizeof o);
	Files files;
	for (int i = 1; i < argc; i++)
	{
		const std::string a = argv[i];
		const bool more = i + 1 < argc;
		if ((a == "-o" || a == "--out") && more) out = argv[++i];
		else if (a == "--version" && more) version = argv[++i];
		else if (a == "--iwad" && more) iwad = argv[++i];
		else if (a == "--wads" && more) files.dirs.push_back(argv[++i]);
		else if (a == "--pwad" && more) pwads.push_back(argv[++i]), have_pwads = true;
		else if (a == "--no-pwads") o.no_pwads = 1;
		else if (a == "--longtics") o.longtics = 1;
		else if (a == "--respawn") o.respawn = 1;
		else if (a == "--fast") o.fast = 1;
		else if (a == "--nomonsters") o.nomonsters = 1;
		else if (a == "--package" && more) package = argv[++i];
		else if (a == "--title" && more) title = argv[++i];
		else if (a == "--info") info = true;
		else if (a[0] == '-' || demo_path) usage();
		else demo_path = argv[i];
	}
	if (!demo_path) usage();
	std::vector<unsigned char> demo;
	if (!read_file(demo_path, &demo))
	{
		fprintf(stderr, "%s: cannot be read\n", demo_path);
		return 1;
	}
	if (iwad) files.paths.push_back(iwad);
	for (auto &p : pwads) files.paths.push_back(p);
	std::vector<const char *> pwad_names;
	for (auto &p : pwads) pwad_names.push_back(p.c_str());
	o.version = version;
	o.iwad = iwad;
	o.pwads = have_pwads ? pwad_names.data() : nullptr;
	o.npwads = (int)pwad_names.size();
	o.missing_hint = "in none of the folders given (--wads), or give --pwad";
	o.read = Files::read;
	o.ctx = &files;

	std::string core_version, core_sha1;
	if (package)
	{
		std::vector<unsigned char> z;
		if (!read_file(package, &z))
		{
			fprintf(stderr, "%s: cannot be read\n", package);
			return 1;
		}
		char sha1[41];
		lmpi_sha1(z.data(), z.size(), sha1);
		core_sha1 = sha1;
		core_version = top_string(zip_entry(z, "build.json"), "version");
	}
	const std::string base = basename_of(demo_path);
	std::string stem = base.substr(0, base.find_last_of('.') == std::string::npos ? base.size() : base.find_last_of('.'));
	struct lmpi_project project = { title ? title : stem.c_str(), base.c_str(), "DSDA-Doom", core_version.c_str(), core_sha1.c_str() };
	struct lmpi_summary sum;
	char err[1024];
	char *json = lmpi_import(demo.data(), demo.size(), &o, info ? nullptr : &project, &sum, err, sizeof err);
	if (!json)
	{
		fprintf(stderr, "%s: %s\n", demo_path, err);
		return 1;
	}
	fprintf(stderr, "%s: %s, %ld tics, player%s %s, %s%s%s\n", base.c_str(), sum.format, sum.tics, strchr(sum.players, ',') ? "s" : "",
		sum.players, sum.version, sum.complevel[0] ? ", " : "", sum.complevel);
	if (sum.footer[0]) fprintf(stderr, "  footer: %s\n", sum.footer);
	for (char *note = sum.notes; *note;)
	{
		char *nl = strchr(note, '\n');
		if (nl) *nl = 0;
		fprintf(stderr, "  note: %s\n", note);
		if (!nl) break;
		note = nl + 1;
	}
	if (info)
	{
		fputs(json, stdout);
		free(json);
		return 0;
	}
	std::string path = out ? out : std::string(demo_path).substr(0, std::string(demo_path).size() - (base.size() - stem.size())) + ".chimeraProject";
	FILE *f = fopen(path.c_str(), "wb");
	if (!f || fputs(json, f) < 0 || fclose(f) != 0)
	{
		fprintf(stderr, "%s: cannot be written\n", path.c_str());
		free(json);
		return 1;
	}
	fprintf(stderr, "  wrote %s\n", path.c_str());
	free(json);
	return 0;
}
