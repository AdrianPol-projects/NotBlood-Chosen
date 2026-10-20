//-------------------------------------------------------------------------
/*
This file is part of NotBlood Chosen.

NotBlood is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.
*/
//-------------------------------------------------------------------------

// Blood Extra Crispy's definitions (see becdefs.h). BloodEX.kpf is a zip: its central directory lists
// the files, each stored or deflated (miniz's tinfl inflates them). The text is KEX's definition
// format: C comments, #include, #define NAME value (the multi-line macros are skipped), and blocks
// "Actor NAME { key value ... }" or "AISTATE_NAME { key value ... }".

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "build.h"
#include "compat.h"
#include "miniz_tinfl.h"
#include "common_game.h"
#include "becdefs.h"

namespace
{

struct KEYVALUE
{
    std::string key, value;
};

enum { kKindActor, kKindState, kKindVector, kKindCount };

struct BLOCKLIST
{
    std::vector<BECBLOCK> blocks;
    std::vector<std::string> names;  // the blocks' names (BECBLOCK::pzName points into them)
    std::unordered_map<std::string, int> index;
};

struct DEFS
{
    std::unordered_map<std::string, int> defines;
    std::vector<KEYVALUE> values;
    BLOCKLIST lists[kKindCount];
};

DEFS *pDefs;

uint32_t le32(uint8_t const *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t le16(uint8_t const *p) { return p[0] | (p[1] << 8); }

bool readAt(buildvfs_kfd h, int nOffset, void *pBuf, int nSize)
{
    return klseek(h, nOffset, SEEK_SET) == nOffset && kread(h, pBuf, nSize) == nSize;
}

// the zip's files that hold definitions: types, actors, AI states and vectors
bool wanted(std::string const &name)
{
    auto starts = [&](char const *p) { return !Bstrncasecmp(name.c_str(), p, strlen(p)); };
    if (name.size() < 5 || Bstrcasecmp(name.c_str() + name.size() - 4, ".txt"))
        return false;
    return starts("defs/types/") || starts("defs/actors/") || starts("defs/aiStates/") || starts("defs/vectorDef");
}

void stripComments(std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/')
        {
            while (i < s.size() && s[i] != '\n')
                i++;
            if (i < s.size())
                out += '\n';
        }
        else if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < s.size() && !(s[i] == '*' && s[i + 1] == '/'))
            {
                if (s[i] == '\n')
                    out += '\n';
                i++;
            }
            i++;
        }
        else if (s[i] != '\r')
            out += s[i];
    }
    s.swap(out);
}

std::vector<std::string> splitWords(std::string const &line)
{
    std::vector<std::string> w;
    size_t i = 0;
    while (i < line.size())
    {
        while (i < line.size() && isspace((unsigned char)line[i]))
            i++;
        if (i >= line.size())
            break;
        size_t j = i;
        if (line[i] == '"')
        {
            j = line.find('"', i + 1);
            j = j == std::string::npos ? line.size() : j + 1;
        }
        else
            while (j < line.size() && !isspace((unsigned char)line[j]))
                j++;
        w.push_back(line.substr(i, j - i));
        i = j;
    }
    return w;
}

void parse(std::string text)
{
    stripComments(text);
    std::vector<std::string> lines;
    size_t p = 0;
    while (p <= text.size())
    {
        size_t e = text.find('\n', p);
        if (e == std::string::npos)
            e = text.size();
        lines.push_back(text.substr(p, e - p));
        p = e + 1;
    }
    bool bMacro = false;  // inside a multi-line #define
    std::string blockName;
    int nKind = -1;
    bool bInBlock = false, bWantBrace = false;
    BECBLOCK block = {};
    for (auto &line : lines)
    {
        while (!line.empty() && isspace((unsigned char)line.back()))
            line.pop_back();
        if (bMacro)
        {
            bMacro = !line.empty() && line.back() == '\\';
            continue;
        }
        auto w = splitWords(line);
        if (w.empty())
            continue;
        if (w[0][0] == '#')
        {
            if (line.back() == '\\')
                bMacro = true;
            else if (w[0] == "#define" && w.size() >= 3)
            {
                char *pEnd;
                long n = strtol(w[2].c_str(), &pEnd, 0);
                if (*pEnd == '\0')
                    pDefs->defines[w[1]] = (int)n;
                else
                {
                    auto it = pDefs->defines.find(w[2]);
                    if (it != pDefs->defines.end())
                        pDefs->defines[w[1]] = it->second;
                }
            }
            continue;
        }
        if (!bInBlock)
        {
            if (w[0] == "{" && bWantBrace)
            {
                bInBlock = true, bWantBrace = false;
                block.nFirst = (int)pDefs->values.size();
                block.nCount = 0;
                continue;
            }
            bWantBrace = false;
            if ((w[0] == "Actor" || w[0] == "Vector" || w[0] == "Callback") && w.size() >= 2)
            {
                blockName = w[1], bWantBrace = true;
                nKind = w[0] == "Actor" ? kKindActor : w[0] == "Vector" ? kKindVector : -1;
                if (nKind < 0)  // a callback: not used (its function is named in a "decl" key)
                    blockName.clear();
            }
            else if (!Bstrncmp(w[0].c_str(), "AISTATE_", 8))
                blockName = w[0], nKind = kKindState, bWantBrace = true;
            if (bWantBrace && w.back() == "{")
            {
                bInBlock = true, bWantBrace = false;
                block.nFirst = (int)pDefs->values.size();
                block.nCount = 0;
            }
            continue;
        }
        if (w[0] == "}")
        {
            bInBlock = false;
            if (blockName.empty() || nKind < 0)
                continue;
            auto &list = pDefs->lists[nKind];
            list.names.push_back(blockName);
            block.pzName = NULL;
            list.index[blockName] = (int)list.blocks.size();
            list.blocks.push_back(block);
            continue;
        }
        if (w.size() >= 2)
        {
            std::string value = w[1];
            for (size_t i = 2; i < w.size(); i++)
                value += " " + w[i];
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                value = value.substr(1, value.size() - 2);
            pDefs->values.push_back({ w[0], value });
            block.nCount++;
        }
    }
}

bool loadZip(char const *pzKpf)
{
    buildvfs_kfd h = kopen4load(pzKpf, 0);
    if (h == buildvfs_kfd_invalid)
        return false;
    int const nLength = kfilelength(h);
    // the end of central directory record, in the last 64 KB
    int const nTail = min(nLength, 65536 + 22);
    std::vector<uint8_t> tail(nTail);
    if (!readAt(h, nLength - nTail, tail.data(), nTail))
    {
        kclose(h);
        return false;
    }
    int nEnd = -1;
    for (int i = nTail - 22; i >= 0; i--)
        if (le32(&tail[i]) == 0x06054b50)
        {
            nEnd = i;
            break;
        }
    if (nEnd < 0)
    {
        kclose(h);
        return false;
    }
    int const nFiles = le16(&tail[nEnd + 10]);
    int const nDirSize = le32(&tail[nEnd + 12]), nDirOffset = le32(&tail[nEnd + 16]);
    std::vector<uint8_t> dir(nDirSize);
    if (nDirSize <= 0 || !readAt(h, nDirOffset, dir.data(), nDirSize))
    {
        kclose(h);
        return false;
    }
    struct ENTRY { std::string name; int nMethod, nPacked, nSize, nOffset; };
    std::vector<ENTRY> types, others;  // the type files first: the others use their #defines
    size_t p = 0;
    for (int i = 0; i < nFiles && p + 46 <= dir.size(); i++)
    {
        uint8_t const *e = &dir[p];
        if (le32(e) != 0x02014b50)
            break;
        int const nNameLen = le16(e + 28), nExtraLen = le16(e + 30), nCommentLen = le16(e + 32);
        ENTRY ent;
        ent.name.assign((char const *)e + 46, min<size_t>(nNameLen, dir.size() - p - 46));
        for (auto &c : ent.name)
            if (c == '\\')
                c = '/';
        ent.nMethod = le16(e + 10);
        ent.nPacked = le32(e + 20);
        ent.nSize = le32(e + 24);
        ent.nOffset = le32(e + 42);
        p += 46 + nNameLen + nExtraLen + nCommentLen;
        if (!wanted(ent.name))
            continue;
        (!Bstrncasecmp(ent.name.c_str(), "defs/types/", 11) ? types : others).push_back(ent);
    }
    // common.txt (the numbers) before callbacks.txt and the rest
    std::sort(types.begin(), types.end(), [](ENTRY const &a, ENTRY const &b) {
        bool ac = !Bstrcasecmp(a.name.c_str(), "defs/types/common.txt"), bc = !Bstrcasecmp(b.name.c_str(), "defs/types/common.txt");
        return ac != bc ? ac : a.name < b.name;
    });
    int nRead = 0;
    for (auto *pList : { &types, &others })
    {
        for (auto const &ent : *pList)
        {
            uint8_t local[30];
            if (!readAt(h, ent.nOffset, local, 30) || le32(local) != 0x04034b50)
                continue;
            int const nData = ent.nOffset + 30 + le16(local + 26) + le16(local + 28);
            std::vector<uint8_t> packed(ent.nPacked);
            if (ent.nPacked > 0 && !readAt(h, nData, packed.data(), ent.nPacked))
                continue;
            std::string text;
            if (ent.nMethod == 0)
                text.assign((char const *)packed.data(), packed.size());
            else if (ent.nMethod == 8)
            {
                text.resize(ent.nSize);
                size_t const n = tinfl_decompress_mem_to_mem(&text[0], ent.nSize, packed.data(), packed.size(),
                                                             TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
                if (n == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED)
                    continue;
                text.resize(n);
            }
            else
                continue;
            parse(text);
            nRead++;
        }
    }
    kclose(h);
    // the blocks' names, now that the list of them won't move
    for (auto &list : pDefs->lists)
        for (size_t i = 0; i < list.blocks.size(); i++)
            list.blocks[i].pzName = list.names[i].c_str();
    return nRead > 0;
}

} // namespace

bool becDefsLoad(char const *pzKpf)
{
    becDefsFree();
    if (!pzKpf)
        return false;
    pDefs = new DEFS;
    if (!loadZip(pzKpf) || pDefs->lists[kKindActor].blocks.empty())
    {
        becDefsFree();
        return false;
    }
    LOG_F(INFO, "Blood Extra Crispy: %d definitions, %d actors, %d AI states, %d vectors from %s",
          (int)pDefs->defines.size(), (int)pDefs->lists[kKindActor].blocks.size(),
          (int)pDefs->lists[kKindState].blocks.size(), (int)pDefs->lists[kKindVector].blocks.size(), pzKpf);
    return true;
}

void becDefsFree(void)
{
    delete pDefs;
    pDefs = NULL;
}

bool becDefsLoaded(void)
{
    return pDefs != NULL;
}

int becDefine(char const *pzName, int nDefault)
{
    if (!pDefs || !pzName)
        return nDefault;
    auto it = pDefs->defines.find(pzName);
    return it != pDefs->defines.end() ? it->second : nDefault;
}

static BECBLOCK const *findBlock(int nKind, char const *pzName)
{
    if (!pDefs || !pzName)
        return NULL;
    auto const &list = pDefs->lists[nKind];
    auto it = list.index.find(pzName);
    return it != list.index.end() ? &list.blocks[it->second] : NULL;
}

BECBLOCK const *becDefActor(char const *pzName)
{
    return findBlock(kKindActor, pzName);
}

BECBLOCK const *becDefState(char const *pzName)
{
    return findBlock(kKindState, pzName);
}

BECBLOCK const *becDefVector(char const *pzName)
{
    return findBlock(kKindVector, pzName);
}

int becDefStateCount(void)
{
    return pDefs ? (int)pDefs->lists[kKindState].blocks.size() : 0;
}

BECBLOCK const *becDefStateAt(int nIndex)
{
    return nIndex >= 0 && nIndex < becDefStateCount() ? &pDefs->lists[kKindState].blocks[nIndex] : NULL;
}

char const *becValue(BECBLOCK const *pBlock, char const *pzKey)
{
    if (!pDefs || !pBlock)
        return NULL;
    char const *pzFound = NULL;
    for (int i = 0; i < pBlock->nCount; i++)  // the last one counts
    {
        auto const &kv = pDefs->values[pBlock->nFirst + i];
        if (!Bstrcasecmp(kv.key.c_str(), pzKey))
            pzFound = kv.value.c_str();
    }
    return pzFound;
}

int becNumber(char const *pzText, int nDefault)
{
    if (!pzText || !*pzText)
        return nDefault;
    if (!Bstrcasecmp(pzText, "TRUE"))
        return 1;
    if (!Bstrcasecmp(pzText, "FALSE"))
        return 0;
    char *pEnd;
    long n = strtol(pzText, &pEnd, 0);
    if (*pEnd == '\0')
        return (int)n;
    if (*pEnd == '.' || *pEnd == 'f')
        return (int)(strtod(pzText, NULL) * 65536.0);
    return becDefine(pzText, nDefault);
}

int becInt(BECBLOCK const *pBlock, char const *pzKey, int nDefault)
{
    return becNumber(becValue(pBlock, pzKey), nDefault);
}
