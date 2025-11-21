/*  HDR2WAV
    Copyright(C) 2025 Lukas Cone

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
#include "spike/format/WAVE.hpp"
#include <istream>

std::string_view filters[]{
    ".HDR$",
};

static AppInfo_s appInfo{
    .header =
        HDR2WAV_DESC " v" HDR2WAV_VERSION ", " HDR2WAV_COPYRIGHT "Lukas Cone",
    .filters = filters,
};

AppInfo_s *AppInitModule() { return &appInfo; }

struct Item {
  char name[0x10];
  uint16 unk0;
  uint16 unk1;
  uint32 sampleRate;
  uint32 dataStart;
  uint32 null0;
  uint32 dataSize;
  uint16 unk2; // min pitch?
  uint16 unk3; // max pitch?
  uint32 unk4;
  float unk5; // volume or level?
  uint16 unk6;
  uint16 null1[7];
};

struct Header {
  uint32 id;
  float version;
  uint32 numItems;
  uint32 null0[5];
  Item items[1];
};

void AppProcessFile(AppContext *ctx) {
  uint32 id;
  ctx->GetType(id);

  if (id != CompileFourCC("snda")) {
    throw es::InvalidHeaderError(id);
  }

  auto str = ctx->RequestFile(ctx->workingFile.ChangeExtension2("RAW"));
  std::string buffer = ctx->GetBuffer();
  const Header *hdr = reinterpret_cast<const Header *>(buffer.data());
  std::string dataBuffer;
  auto ectx = ctx->ExtractContext();

  for (uint32 i = 0; i < hdr->numItems; i++) {
    const Item &item = hdr->items[i];
    str->seekg(item.dataStart);
    dataBuffer.resize(item.dataSize);
    str->read(dataBuffer.data(), item.dataSize);

    WAVE_fmt wavFormat(WAVE_FORMAT::PCM);
    wavFormat.sampleRate = item.sampleRate;
    wavFormat.CalcData();
    WAVE_data wavData(item.dataSize);
    RIFFHeader wavHdr(sizeof(WAVE_fmt) + sizeof(WAVE_data) + item.dataSize);
    std::string path(item.name);
    path.append(".wav");

    ectx->NewFile(path);
    ectx->SendData({reinterpret_cast<const char *>(&wavHdr), sizeof(wavHdr)});
    ectx->SendData({reinterpret_cast<const char *>(&wavFormat), sizeof(wavFormat)});
    ectx->SendData({reinterpret_cast<const char *>(&wavData), sizeof(wavData)});
    ectx->SendData(dataBuffer);
  }
}
