/*  LDA2TXT
    Copyright(C) 2023-2024 Lukas Cone

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
#include "spike/master_printer.hpp"
#include "spike/util/unicode.hpp"

std::string_view filters[]{
    ".LDA$",
};

static AppInfo_s appInfo{
    .filteredLoad = true,
    .header =
        LDA2TXT_DESC " v" LDA2TXT_VERSION ", " LDA2TXT_COPYRIGHT "Lukas Cone",
    .filters = filters,
};

AppInfo_s *AppInitModule() { return &appInfo; }

struct Header {
  uint32 id;
  uint32 fileSize;
  uint32 id0;
  uint32 id1;
  uint32 numItems;
  uint32 items[1];
};

void AppProcessFile(AppContext *ctx) {
  uint32 id;
  ctx->GetType(id);
  const bool isUtf16 = id == CompileFourCC("lda1");

  if (id != CompileFourCC("lda0") && !isUtf16) {
    throw es::InvalidHeaderError(id);
  }

  std::string buffer = ctx->GetBuffer();
  const Header *hdr = reinterpret_cast<const Header *>(buffer.data());

  auto &str = ctx->NewFile(ctx->workingFile.ChangeExtension2("txt")).str;
  const char *bufferStart =
      reinterpret_cast<const char *>(hdr->items + hdr->numItems + 1);

  if (isUtf16) {
    for (uint32 i = 0; i < hdr->numItems; i++) {
      str << es::ToUTF8(
                 reinterpret_cast<const uint16 *>(bufferStart + hdr->items[i]))
          << '\n';
    }
  } else {
    for (uint32 i = 0; i < hdr->numItems; i++) {
      str << bufferStart + hdr->items[i] << '\n';
    }
  }
}
