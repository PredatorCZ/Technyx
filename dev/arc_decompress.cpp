/*  ARCDecompress
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
#include "spike/io/binwritter_stream.hpp"

#include "lzo1x.h"

std::string_view filters[]{
    ".ARC$",
};

static AppInfo_s appInfo{
    .filteredLoad = true,
    .header = ARCDecompress_DESC " v" ARCDecompress_VERSION
                                 ", " ARCDecompress_COPYRIGHT "Lukas Cone",
    .filters = filters,
};

AppInfo_s *AppInitModule() { return &appInfo; }

void AppProcessFile(AppContext *ctx) {
  BinReaderRef rd(ctx->GetStream());
  uint32 id;
  rd.Read(id);

  if (id != CompileFourCC("ARCN")) {
    throw es::InvalidHeaderError(id);
  }

  rd.Seek(0x74);
  uint32 compId;
  uint32 compressedSize;
  uint32 uncompressedSize;
  rd.Read(compId);
  rd.Read(compressedSize);
  rd.Read(uncompressedSize);

  if (compId != 0xC0DEC0DE) {
    return;
  }

  std::string buffer;
  rd.ReadContainer(buffer, compressedSize);

  std::string outBuffer;
  outBuffer.resize(uncompressedSize);
  size_t outSize = outBuffer.size();

  int status = lzo1x_decompress_safe(
      reinterpret_cast<const uint8 *>(buffer.data()), compressedSize,
      reinterpret_cast<uint8 *>(outBuffer.data()), &outSize);

  if (status != LZO_E_OK) {
    throw std::runtime_error("Failed to decompress lzo stream, code: " +
                             std::to_string(status));
  }

  char header[0x74];
  rd.Seek(0);
  rd.Read(header);

  BinWritterRef wr(ctx->NewFile(ctx->workingFile.ChangeExtension2("arcd")).str);
  wr.Write(header);
  wr.Skip(0xC);
  wr.WriteContainer(outBuffer);
}
