#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>
#include "ext_lod.h"

namespace
{
	constexpr size_t LOD_HEADER_SIZE = 92;
	constexpr size_t LOD_ENTRY_SIZE  = 32;

	struct LodEntry
	{
		char  name[16];
		DWORD size;
		DWORD type;
		DWORD csize;
		std::string data;
	};

	bool ReadFileBytes(const char* path, std::string& out)
	{
		FILE* f = nullptr;
		if (fopen_s(&f, path, "rb") != 0 || !f)
			return false;
		fseek(f, 0, SEEK_END);
		const long size = ftell(f);
		fseek(f, 0, SEEK_SET);
		out.resize(size > 0 ? size_t(size) : 0);
		const size_t read = out.empty() ? 0 : fread(&out[0], 1, out.size(), f);
		fclose(f);
		return read == out.size();
	}

	DWORD U32(const std::string& bytes, size_t offset)
	{
		DWORD v = 0;
		memcpy(&v, bytes.data() + offset, 4);
		return v;
	}

	int ReadLod(const char* path, std::vector<LodEntry>& entries)
	{
		std::string bytes;
		if (!ReadFileBytes(path, bytes) || bytes.size() < LOD_HEADER_SIZE || memcmp(bytes.data(), "LOD\0", 4) != 0)
			return -1;

		const DWORD count = U32(bytes, 8);
		if (LOD_HEADER_SIZE + size_t(count) * LOD_ENTRY_SIZE > bytes.size())
			return -1;

		int added = 0;
		for (DWORD i = 0; i < count; ++i)
		{
			const size_t at = LOD_HEADER_SIZE + size_t(i) * LOD_ENTRY_SIZE;
			LodEntry e = {};
			memcpy(e.name, bytes.data() + at, 16);
			e.name[15] = 0;
			const DWORD offset = U32(bytes, at + 16);
			e.size  = U32(bytes, at + 20);
			e.type  = U32(bytes, at + 24);
			e.csize = U32(bytes, at + 28);
			const DWORD stored = e.csize ? e.csize : e.size;
			if (size_t(offset) + stored > bytes.size())
				continue;

			const bool present = std::any_of(entries.begin(), entries.end(),
				[&](const LodEntry& x) { return _stricmp(x.name, e.name) == 0; });
			if (present)
				continue;

			e.data.assign(bytes.data() + offset, stored);
			entries.push_back(std::move(e));
			++added;
		}
		return added;
	}

	std::string WriteLod(std::vector<LodEntry>& entries)
	{
		std::sort(entries.begin(), entries.end(),
			[](const LodEntry& a, const LodEntry& b) { return _stricmp(a.name, b.name) < 0; });

		std::string out(LOD_HEADER_SIZE + entries.size() * LOD_ENTRY_SIZE, '\0');
		memcpy(&out[0], "LOD\0", 4);
		const DWORD version = 200;
		const DWORD count = DWORD(entries.size());
		memcpy(&out[4], &version, 4);
		memcpy(&out[8], &count, 4);

		for (size_t i = 0; i < entries.size(); ++i)
		{
			const LodEntry& e = entries[i];
			const DWORD offset = DWORD(out.size());
			char* row = &out[LOD_HEADER_SIZE + i * LOD_ENTRY_SIZE];
			memcpy(row, e.name, 16);
			memcpy(row + 16, &offset, 4);
			memcpy(row + 20, &e.size, 4);
			memcpy(row + 24, &e.type, 4);
			memcpy(row + 28, &e.csize, 4);
			out += e.data;
		}
		return out;
	}
}

void BuildExtLod(const char* gameDir, char packs[][MAX_PATH], int packCount, LogFn log)
{
	std::vector<LodEntry> entries;
	char buf[MAX_PATH + 128];

	for (int i = 0; i < packCount; ++i)
	{
		char pattern[MAX_PATH] = {};
		sprintf_s(pattern, "%s_HD3_Data\\Packs\\%s\\*.lod", gameDir, packs[i]);
		WIN32_FIND_DATAA fd = {};
		HANDLE find = FindFirstFileA(pattern, &fd);
		if (find == INVALID_HANDLE_VALUE)
			continue;
		do
		{
			char path[MAX_PATH] = {};
			sprintf_s(path, "%s_HD3_Data\\Packs\\%s\\%s", gameDir, packs[i], fd.cFileName);
			const int added = ReadLod(path, entries);
			if (added < 0)
				sprintf_s(buf, "[HotaLoader] %s is not a readable LOD", path);
			else
				sprintf_s(buf, "[HotaLoader] pack %s: %d entries from %s into HotA_ext.lod", packs[i], added, fd.cFileName);
			log(buf);
		} while (FindNextFileA(find, &fd));
		FindClose(find);
	}

	const std::string merged = WriteLod(entries);
	char target[MAX_PATH] = {};
	sprintf_s(target, "%sData\\HotA_ext.lod", gameDir);

	std::string current;
	if (ReadFileBytes(target, current) && current == merged)
		return;

	FILE* f = nullptr;
	if (fopen_s(&f, target, "wb") != 0 || !f)
	{
		log("[HotaLoader] cannot write Data\\HotA_ext.lod");
		return;
	}
	const size_t written = fwrite(merged.data(), 1, merged.size(), f);
	fclose(f);
	sprintf_s(buf, "[HotaLoader] wrote Data\\HotA_ext.lod: %u entries, %u bytes", unsigned(entries.size()), unsigned(written));
	log(buf);
}
