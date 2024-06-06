/*  CDFILESExtract
    Copyright(C) 2023 Lukas Cone

    This program is free software : you can redistribute it and / or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.If not, see <https://www.gnu.org/licenses/>.
*/

#include "project.h"
#include "spike/app_context.hpp"
#include "spike/except.hpp"
#include "spike/io/binreader_stream.hpp"

std::string_view filters[]{
    "cdfiles*.dat$",
    "CDFILES*.DAT$",
    "CDFILES*.dat$",
};

static AppInfo_s appInfo{
    .filteredLoad = true,
    .header = CDFILESExtract_DESC " v" CDFILESExtract_VERSION
                                  ", " CDFILESExtract_COPYRIGHT "Lukas Cone",
    .filters = filters,
};

AppInfo_s *AppInitModule() { return &appInfo; }

enum class Platform {
  AUTO = CompileFourCC("file"),
  PC = CompileFourCC("filC"),
  PS2 = CompileFourCC("filP"),
  XBOX = CompileFourCC("filX"),
  PS3 = CompileFourCC("fil3"),
  X360 = CompileFourCC("filE"),
  WII = CompileFourCC("filN"),
};

struct HeaderBase {
  Platform id;
  uint32 version;

  void Read(BinReaderRef_e &rd) {
    rd.Read(id);

    switch (id) {
    case Platform::AUTO:
    case Platform::PC:
    case Platform::PS2:
    case Platform::XBOX:
    case Platform::PS3:
    case Platform::X360:
    case Platform::WII:
      break;

    default:
      throw es::InvalidHeaderError(uint32(id));
    }

    rd.Read(version);

    if (version > 0x10000) {
      rd.Skip(-4);
      rd.SwapEndian(true);
      rd.Read(version);
    }
  }
};

struct HeaderV3 {
  float codeVersion;
  uint32 unk0[2]; // ptr size and ptr alignment?
  uint32 numSearchPaths;
  uint32 searchPathsSize;
  uint32 numFiles;
  uint32 archivePathLength;
  uint32 alignment;
  uint32 numEntries;
  uint32 unk3;
  uint32 null0[2];
};

enum class EntryType : uint8 {
  Stream = 0,
  HDDFile = 2,
  StreamFile = 4,
  StreamHdFile = 5,
};

struct FileId {
  uint32 id : 28;
  EntryType type : 4;
};

void FByteswapper(HeaderBase &item) {
  FByteswapper(item.id);
  FByteswapper(item.version);
}

void FByteswapper(HeaderV3 &item) {
  FByteswapper(item.codeVersion);
  FByteswapper(item.unk0);
  FByteswapper(item.numSearchPaths);
  FByteswapper(item.searchPathsSize);
  FByteswapper(item.numFiles);
  FByteswapper(item.archivePathLength);
  FByteswapper(item.alignment);
  FByteswapper(item.numEntries);
  FByteswapper(item.unk3);
  FByteswapper(item.null0);
}

void FByteswapper(FileId &item) {
  FByteswapper(reinterpret_cast<uint32 &>(item));
}

std::string CatName(BinReaderRef rd, const std::vector<std::string> &names) {
  uint8 curChar;
  std::string name;

  // Looks like utf-8 encoding
  while (true) {
    uint32 index = 0;
    rd.Read(curChar);

    if (curChar == 0) {
      break;
    }

    if (curChar & 0x80) {
      index = curChar & 0x7f;
      index <<= 8;
      rd.Read(curChar);
    }

    index |= curChar;
    name.append(names.at(index - 1));
  }

  return name;
}

void ExtractV3(AppContext *ctx, BinReaderRef_e rd, Platform platform) {
  HeaderV3 hdr;
  rd.Read(hdr);

  std::vector<uint32> searchPathsOffsets;
  rd.ReadContainer(searchPathsOffsets, hdr.numSearchPaths);

  std::string searchPathsBuffer;
  rd.ReadContainer(searchPathsBuffer, hdr.searchPathsSize);

  std::string archivePath;
  rd.ReadContainer(archivePath, hdr.archivePathLength);

  std::vector<uint32> fileOffsets;
  rd.ReadContainer(fileOffsets, hdr.numFiles);

  std::vector<uint32> fileSizes;
  rd.ReadContainer(fileSizes, hdr.numFiles);

  std::vector<uint32> treeOffsets;
  rd.ReadContainer(treeOffsets, hdr.numEntries);

  std::vector<FileId> fileIds;
  rd.ReadContainer(fileIds, hdr.numEntries);

  AppContextStream streams[4];
  bool streamParts = false;
  streams[0] = [&] {
    try {
      return ctx->RequestFile("archive.ar");
    } catch (const es::FileNotFoundError &e) {
      try {
        return ctx->RequestFile("ARCHIVE.AR");
      } catch (const es::FileNotFoundError &e) {
        streamParts = true;
        return ctx->RequestFile("archive0.ar");
      }
    }
  }();

  if (platform != Platform::AUTO ||
      (platform == Platform::AUTO && rd.SwappedEndian())) {
    std::vector<uint32> varData;
    rd.ReadContainer(varData, hdr.numEntries);
  }

  std::vector<uint32> streamIds;
  bool usedStreams[4]{};

  if ((platform == Platform::AUTO && !rd.SwappedEndian()) ||
      platform == Platform::XBOX) {
    rd.ReadContainer(streamIds, hdr.numEntries);

    if (streamParts) {
      for (uint32 s : streamIds) {
        if (s > 3) [[unlikely]] {
          throw std::runtime_error("Invalid data");
        }
        usedStreams[s] = true;
      }
    }
  }

  std::vector<std::string> names;

  {
    uint32 numNames;
    uint32 namesBufferSize;
    rd.Read(numNames);
    rd.Read(namesBufferSize);

    names.resize(numNames);
    const size_t namesBegin = rd.Tell() + numNames * sizeof(uint32);

    for (auto &name : names) {
      uint32 offset;
      rd.Read(offset);
      rd.Push();
      rd.Seek(namesBegin + offset);
      rd.ReadString(name);
      rd.Pop();
    }

    rd.Skip(namesBufferSize);
  }

  rd.SetRelativeOrigin(rd.Tell());

  auto ectx = ctx->ExtractContext();

  if (streamParts) {
    for (uint32 p = 1; p < 4; p++) {
      if (usedStreams[p]) {
        streams[p] = ctx->RequestFile("archive" + std::to_string(p) + ".ar");
      }
    }
  }

  BinReaderRef srs[4]{
      *streams[0].Get(),
      streamParts ? *streams[1].Get() : BinReaderRef{},
      streamParts ? *streams[2].Get() : BinReaderRef{},
      streamParts ? *streams[3].Get() : BinReaderRef{},
  };

  std::string buffer;

  for (size_t f = 0; f < hdr.numEntries; f++) {
    FileId id = fileIds[f];
    if (id.type == EntryType::StreamFile) {
      uint32 streamId = streamParts ? streamIds[f] : 0;
      BinReaderRef sr(srs[streamId]);
      sr.Seek(fileOffsets[id.id] * hdr.alignment);
      sr.ReadContainer(buffer, fileSizes[id.id]);
    }

    rd.Seek(treeOffsets[f]);
    std::string fileName = CatName(rd, names);

    if (id.type == EntryType::StreamFile) {
      ectx->NewFile(fileName);
      if (fileName.ends_with(".ARC")) {
        if (rd.SwappedEndian()) {
          buffer.at(4) = 3;
        } else {
          buffer.at(7) = 3;
        }
      }
      ectx->SendData(buffer);
    }
  }
}

struct HeaderV6 {
  uint32 unk1[5];
  uint32 numArchives;
  uint32 numTotalFiles;
  uint32 numTreeNodes;
  uint32 stringBufferSize;
};

void FByteswapper(HeaderV6 &item) { FArraySwapper(item); }

struct File {
  uint32 null0;
  uint32 folderNameOffset;
  uint32 fileNameOffset;
  uint32 dataSize;
  uint32 uncompressedSize;
  uint32 null1;
  uint32 dataOffset;
  uint8 archiveIndex;
  EntryType type;
  uint8 null2;
  uint8 unk4;
};

void FByteswapper(File &item) {
  FByteswapper(item.null0);
  FByteswapper(item.folderNameOffset);
  FByteswapper(item.fileNameOffset);
  FByteswapper(item.dataSize);
  FByteswapper(item.uncompressedSize);
  FByteswapper(item.null1);
  FByteswapper(item.dataOffset);
}

struct Archive {
  uint32 archiveNameOffset;
  uint32 unk1;
};

void FByteswapper(Archive &item) {
  FByteswapper(item.archiveNameOffset);
  FByteswapper(item.unk1);
}

struct TreeNode {
  int32 parentNode;
  int32 unk[7];
  uint32 fileIndex;
  uint32 tailNameOffset;
};

void FByteswapper(TreeNode &item) { FArraySwapper(item); }

void ExtractV6(AppContext *ctx, BinReaderRef_e rd) {
  HeaderV6 hdr;
  rd.Read(hdr);

  std::vector<Archive> archives;
  rd.ReadContainer(archives, hdr.numArchives);
  std::vector<File> files;
  rd.ReadContainer(files, hdr.numTotalFiles);
  std::vector<TreeNode> treeNodes;
  rd.ReadContainer(treeNodes, hdr.numTreeNodes);
  std::string nameBuffer;
  rd.ReadContainer(nameBuffer, hdr.stringBufferSize);

  std::vector<AppContextStream> streams;

  for (auto &a : archives) {
    streams.emplace_back(
        ctx->RequestFile(nameBuffer.data() + a.archiveNameOffset));
  }

  std::string buffer;
  auto ectx = ctx->ExtractContext();

  for (auto &f : files) {
    if (f.type != EntryType::StreamFile && f.type != EntryType::StreamHdFile) {
      continue;
    }

    auto &str = streams.at(f.archiveIndex);
    str->seekg(f.dataOffset);
    buffer.resize(f.dataSize);
    str->read(buffer.data(), buffer.size());
    std::string fileName(nameBuffer.data() + f.folderNameOffset);
    fileName.append(nameBuffer.data() + f.fileNameOffset);
    ectx->NewFile(fileName);

    if (fileName.ends_with(".ARC")) {
      if (rd.SwappedEndian()) {
        buffer.at(4) = 6;
      } else {
        buffer.at(7) = 6;
      }
    }

    ectx->SendData(buffer);
  }
}

void ExtractV1PS2(AppContext *ctx, BinReaderRef rd) {
  uint64 unk0;
  rd.Read(unk0);

  uint32 numSearchPaths;
  uint32 null0;

  rd.Read(numSearchPaths);
  rd.Read(null0);

  std::string searchPaths;
  rd.ReadContainer(searchPaths);

  uint32 numTotalFiles;
  rd.Read(numTotalFiles);

  std::string archivePath;
  rd.ReadContainer(archivePath);

  uint32 alignment;
  rd.Read(alignment);

  struct DataFile {
    uint32 dataBlockOffset;
    uint32 dataSize;
  };

  struct EntryFile {
    uint32 nameOffset;
    FileId fileId;
  };

  std::vector<DataFile> dataFiles;
  rd.ReadContainer(dataFiles, numTotalFiles);

  std::vector<EntryFile> entries;
  rd.ReadContainer(entries);

  std::string nameBuffer;
  rd.ReadContainer(nameBuffer);

  AppContextStream str = ctx->RequestFile(archivePath);

  auto ectx = ctx->ExtractContext();
  std::string buffer;

  for (auto &e : entries) {
    std::string fileName = nameBuffer.data() + e.nameOffset;
    ectx->NewFile(fileName);
    DataFile file = dataFiles.at(e.fileId.id);
    str->seekg(file.dataBlockOffset * alignment);
    buffer.resize(file.dataSize);
    str->read(buffer.data(), buffer.size());
    if (fileName.ends_with(".ARC")) {
      if (rd.SwappedEndian()) {
        buffer.at(4) = 1;
      } else {
        buffer.at(7) = 1;
      }
    }
    ectx->SendData(buffer);
  }
}

struct HeaderV1 {
  uint32 unk10;
  uint32 numSearchPaths;
  uint32 searchPathsSize;
  uint32 numTotalFiles;
  uint32 archivePathLength;
  uint32 alignement;
  uint32 numFiles;
  uint32 nameBufferSize;
};

void FByteswapper(HeaderV1 &item) { FArraySwapper(item); }

void ExtractV1X(AppContext *ctx, BinReaderRef_e rd) {
  HeaderV1 hdr;
  rd.Read(hdr);
  std::string rPath;
  rd.ReadString(rPath);
  rd.ReadString(rPath);

  std::vector<uint32> searchPathsOffsets;
  rd.ReadContainer(searchPathsOffsets, hdr.numSearchPaths);

  std::string searchPathsBuffer;
  rd.ReadContainer(searchPathsBuffer, hdr.searchPathsSize);

  std::string archivePath;
  rd.ReadContainer(archivePath, hdr.archivePathLength);

  std::vector<uint32> fileOffsets;
  rd.ReadContainer(fileOffsets, hdr.numTotalFiles);

  std::vector<uint32> fileSizes;
  rd.ReadContainer(fileSizes, hdr.numTotalFiles);

  std::vector<uint32> nameOffsets;
  rd.ReadContainer(nameOffsets, hdr.numFiles);

  std::vector<FileId> fileIds;
  rd.ReadContainer(fileIds, hdr.numFiles);

  std::string nameBuffer;
  rd.ReadContainer(nameBuffer, hdr.nameBufferSize);

  AppContextStream str = ctx->RequestFile(archivePath);

  auto ectx = ctx->ExtractContext();
  std::string buffer;

  for (uint32 i = 0; i < hdr.numFiles; i++) {
    FileId fileId = fileIds.at(i);

    if (fileId.type != EntryType::StreamFile) {
      continue;
    }

    std::string fileName = nameBuffer.data() + nameOffsets.at(i);
    ectx->NewFile(fileName);
    str->seekg(fileOffsets.at(fileId.id) * hdr.alignement);
    buffer.resize(fileSizes.at(fileId.id));
    str->read(buffer.data(), buffer.size());
    if (fileName.ends_with(".ARC")) {
      if (rd.SwappedEndian()) {
        buffer.at(4) = 1;
      } else {
        buffer.at(7) = 1;
      }
    }
    ectx->SendData(buffer);
  }
}

void ExtractV1(AppContext *ctx, BinReaderRef_e rd) {
  float unk0;
  rd.Read(unk0);
  uint32 unk1;
  rd.Read(unk1);

  if (!rd.SwappedEndian() && unk1 == 1) {
    ExtractV1PS2(ctx, rd);
  } else {
    ExtractV1X(ctx, rd);
  }
}

struct HeaderV4 {
  uint32 unk2;
  uint32 unk4;
  uint32 rootPathSize;
  uint32 unk3;
  uint32 numSearchPaths;
  uint32 workingPathSize;
  uint32 numTotalFiles;
  uint32 archivePathLength;
  uint32 alignment;
  uint32 numFiles;
  uint32 unkSize;
};

void FByteswapper(HeaderV4 &item) { FArraySwapper(item); }

void ExtractV5(AppContext *ctx, BinReaderRef_e rd, Platform platform) {
  uint32 unk1;
  rd.Read(unk1);

  if (unk1 < 4) {
    float unk;
    rd.Read(unk);
  }

  HeaderV4 hdr;
  rd.Read(hdr);

  if (unk1 > 4) {
    std::vector<uint32> searchPathsOffsets;
    rd.ReadContainer(searchPathsOffsets, hdr.numSearchPaths);
  } else {
    std::string rootPath;
    rd.ReadContainer(rootPath, hdr.rootPathSize);
  }

  uint32 unk2[2];
  rd.Read(unk2);

  std::string workingPath;
  rd.ReadContainer(workingPath, hdr.workingPathSize);

  std::string archivePath;
  rd.ReadContainer(archivePath, hdr.archivePathLength);

  if (archivePath.starts_with("#/")) {
    archivePath.erase(0, 2);
  }

  std::vector<uint32> fileOffsets;
  rd.ReadContainer(fileOffsets, hdr.numTotalFiles);

  std::vector<uint32> fileSizes;
  rd.ReadContainer(fileSizes, hdr.numTotalFiles);

  std::vector<uint32> treeOffsets;
  rd.ReadContainer(treeOffsets, hdr.numFiles);

  std::vector<FileId> fileIds;
  rd.ReadContainer(fileIds, hdr.numFiles);

  std::vector<uint32> varData;
  rd.ReadContainer(varData, hdr.numFiles);

  std::vector<uint32> streamIds;

  if (platform == Platform::X360) {
    rd.ReadContainer(streamIds, hdr.numFiles);
    std::vector<uint8> unkData0;
    rd.ReadContainer(unkData0, hdr.numFiles);
  } else if (unk1 < 4) {
    rd.Skip(hdr.numFiles * ((unk1 == 3) + 1) * 4);
  }

  AppContextStream streams[2];
  streams[0] = [&] {
    if (platform != Platform::X360) {
      return ctx->RequestFile(archivePath);
    } else {
      streams[1] = ctx->RequestFile("archive1.ar");
      return ctx->RequestFile("archive0.ar");
    }
  }();

  BinReaderRef srs[2]{
      *streams[0].Get(),
      platform == Platform::X360 ? *streams[1].Get() : BinReaderRef{},
  };

  std::vector<std::string> names;

  {
    uint32 numNames;
    uint32 namesBufferSize;
    rd.Read(numNames);
    rd.Read(namesBufferSize);

    names.resize(numNames);
    const size_t namesBegin = rd.Tell() + numNames * sizeof(uint32);

    for (auto &name : names) {
      uint32 offset;
      rd.Read(offset);
      rd.Push();
      rd.Seek(namesBegin + offset);
      rd.ReadString(name);
      rd.Pop();
    }

    rd.Skip(namesBufferSize);
  }

  uint32 unk666;
  rd.Read(unk666);
  rd.Skip(128);

  rd.SetRelativeOrigin(rd.Tell());
  std::string buffer;
  auto ectx = ctx->ExtractContext();

  for (size_t f = 0; f < hdr.numFiles; f++) {
    FileId id = fileIds[f];
    if (id.type == EntryType::StreamFile) {
      uint32 streamId = platform == Platform::X360 ? streamIds[f] : 0;
      BinReaderRef sr(srs[streamId]);
      sr.Seek(fileOffsets[id.id] * hdr.alignment);
      sr.ReadContainer(buffer, fileSizes[id.id]);
    }

    rd.Seek(treeOffsets[f]);

    std::string fileName = CatName(rd, names);

    if (id.type == EntryType::StreamFile) {
      ectx->NewFile(fileName);
      if (fileName.ends_with(".ARC")) {
        if (rd.SwappedEndian()) {
          buffer.at(4) = 5;
        } else {
          buffer.at(7) = 5;
        }
      }
      ectx->SendData(buffer);
    }
  }
}

void ExtractV4(AppContext *ctx, BinReaderRef_e rd, Platform platform) {
  float unk1;
  rd.Read(unk1);

  HeaderV4 hdr;
  rd.Read(hdr);

  std::vector<uint32> searchPathsOffsets;
  rd.ReadContainer(searchPathsOffsets, hdr.numSearchPaths);

  uint32 unk2[2];
  rd.Read(unk2);

  std::string workingPath;
  rd.ReadContainer(workingPath, hdr.workingPathSize);

  std::string archivePath;
  rd.ReadContainer(archivePath, hdr.archivePathLength);

  std::vector<uint32> fileOffsets;
  rd.ReadContainer(fileOffsets, hdr.numTotalFiles);

  std::vector<uint32> fileSizes;
  rd.ReadContainer(fileSizes, hdr.numTotalFiles);

  std::vector<uint32> treeOffsets;
  rd.ReadContainer(treeOffsets, hdr.numFiles);

  std::vector<FileId> fileIds;
  rd.ReadContainer(fileIds, hdr.numFiles);

  std::vector<uint32> varData;
  rd.ReadContainer(varData, hdr.numFiles);

  std::vector<uint32> streamIds;

  if (platform == Platform::X360) {
    rd.ReadContainer(streamIds, hdr.numFiles);
  } else {
    rd.Skip(hdr.numFiles * hdr.unk2 * 4);
  }

  AppContextStream streams[2];
  streams[0] = [&] {
    if (platform != Platform::X360) {
      return ctx->RequestFile(archivePath);
    } else {
      streams[1] = ctx->RequestFile("archive1.ar");
      return ctx->RequestFile("archive0.ar");
    }
  }();

  BinReaderRef srs[2]{
      *streams[0].Get(),
      platform == Platform::X360 ? *streams[1].Get() : BinReaderRef{},
  };

  std::vector<std::string> names;

  {
    uint32 numNames;
    uint32 namesBufferSize;
    rd.Read(numNames);
    rd.Read(namesBufferSize);

    names.resize(numNames);
    const size_t namesBegin = rd.Tell() + numNames * sizeof(uint32);

    for (auto &name : names) {
      uint32 offset;
      rd.Read(offset);
      rd.Push();
      rd.Seek(namesBegin + offset);
      rd.ReadString(name);
      rd.Pop();
    }

    rd.Skip(namesBufferSize);
  }

  uint32 unk666;
  rd.Read(unk666);
  rd.Skip(128 * (hdr.unk2 + 1));

  rd.SetRelativeOrigin(rd.Tell());
  std::string buffer;
  auto ectx = ctx->ExtractContext();

  for (size_t f = 0; f < hdr.numFiles; f++) {
    FileId id = fileIds[f];
    if (id.type == EntryType::StreamFile) {
      uint32 streamId = platform == Platform::X360 ? streamIds[f] : 0;
      BinReaderRef sr(srs[streamId]);
      sr.Seek(fileOffsets[id.id] * hdr.alignment);
      sr.ReadContainer(buffer, fileSizes[id.id]);
    }

    rd.Seek(treeOffsets[f]);

    std::string fileName = CatName(rd, names);

    if (id.type == EntryType::StreamFile) {
      ectx->NewFile(fileName);
      if (fileName.ends_with(".ARC")) {
        if (rd.SwappedEndian()) {
          buffer.at(4) = 4;
        } else {
          buffer.at(7) = 4;
        }
      }
      ectx->SendData(buffer);
    }
  }
}

void AppProcessFile(AppContext *ctx) {
  BinReaderRef_e rd(ctx->GetStream());
  HeaderBase hdr;
  hdr.Read(rd);

  switch (hdr.version) {
  case 1:
    ExtractV1(ctx, rd);
    break;
  case 3:
    ExtractV3(ctx, rd, hdr.id);
    break;
  case 4:
    ExtractV4(ctx, rd, hdr.id);
    break;
  case 5:
    ExtractV5(ctx, rd, hdr.id);
    break;
  case 6:
    ExtractV6(ctx, rd);
    break;

  default:
    throw es::InvalidVersionError(hdr.version);
  }
}
