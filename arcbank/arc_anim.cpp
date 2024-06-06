/*  ARCAnim
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
#include "spike/gltf.hpp"
#include "spike/io/binreader_stream.hpp"
#include "spike/master_printer.hpp"

#include "arc.hpp"

std::string_view filters[]{
    ".ARC$",
};

std::string_view controlFilters[]{
    ".glb$",
    ".gltf$",
};

static AppInfo_s appInfo{
    .filteredLoad = true,
    .header =
        ARCAnim_DESC " v" ARCAnim_VERSION ", " ARCAnim_COPYRIGHT "Lukas Cone",
    .filters = filters,
    .batchControlFilters = controlFilters,
};

AppInfo_s *AppInitModule() { return &appInfo; }

struct GLTFMain : GLTF {
  GLTFStream &AnimStream() {
    if (anims < 0) {
      auto &str = NewStream("animations");
      anims = str.slot;
      return str;
    }

    return Stream(anims);
  }

  int32 anims = -1;
};

struct AnimationNode {
  uint16 numFrames;
  uint16 null0[9];

  std::string nodeName;
  std::vector<uint16> posFrames;
  std::vector<Vector> positions;
  std::vector<uint16> rotFrames;
  std::vector<Vector4A16> rotations;

  void Read(BinReaderRef rd) {
    uint16 numPosFrames;
    uint16 numRotFrames;
    rd.Read(numFrames);
    rd.Read(numPosFrames);
    rd.Read(numRotFrames);
    rd.Read(null0);
    rd.ReadContainer(posFrames, numPosFrames);
    rd.ReadContainer(positions, numPosFrames);
    rd.ReadContainer(rotFrames, numRotFrames);
    rd.ReadContainer(rotations, numRotFrames);
  }
};

struct Animation {
  uint32 endFrame;
  uint32 frameRate;
  uint32 numNodes;
  std::vector<uint32> nulls;
  std::vector<AnimationNode> nodes;
  std::string name;

  void Read(BinReaderRef rd) {
    rd.Read(endFrame);
    rd.Read(frameRate);
    rd.Read(numNodes);
    rd.ReadContainer(nulls, numNodes);
  }
};

void LoadAnimation(const Animation &anim, GLTFMain &main) {
  gltf::Animation &ganim = main.animations.emplace_back();
  ganim.name = anim.name;
  const float invFrameRate = 1.f / anim.frameRate;
  GLTFStream &str = main.AnimStream();

  for (auto &n : anim.nodes) {
    int32 nodeId = [&] {
      for (int32 curId = 0; auto &m : main.nodes) {
        if (m.name == n.nodeName) {
          return curId;
        }

        curId++;
      }

      return -1;
    }();

    if (nodeId < 0) {
      PrintWarning("Cannot find node: ", n.nodeName);
      continue;
    }

    if (n.posFrames.size() > 0) {
      auto &chan = ganim.channels.emplace_back();
      chan.target.node = nodeId;
      chan.target.path = "translation";
      chan.sampler = ganim.samplers.size();

      auto &sampler = ganim.samplers.emplace_back();

      {
        auto [acc, accId] = main.NewAccessor(str, 4);
        acc.count = n.posFrames.size();
        acc.type = gltf::Accessor::Type::Scalar;
        acc.componentType = gltf::Accessor::ComponentType::Float;
        acc.min.emplace_back(0);
        acc.max.emplace_back(n.posFrames.back() * invFrameRate);
        sampler.input = accId;

        for (auto &f : n.posFrames) {
          str.wr.Write(f * invFrameRate);
        }
      }

      {
        auto [acc, accId] = main.NewAccessor(str, 4);
        acc.count = n.posFrames.size();
        acc.type = gltf::Accessor::Type::Vec3;
        acc.componentType = gltf::Accessor::ComponentType::Float;
        sampler.output = accId;

        str.wr.WriteContainer(n.positions);
      }
    }

    if (n.rotFrames.size() > 0) {
      auto &chan = ganim.channels.emplace_back();
      chan.target.node = nodeId;
      chan.target.path = "rotation";
      chan.sampler = ganim.samplers.size();

      auto &sampler = ganim.samplers.emplace_back();

      {
        auto [acc, accId] = main.NewAccessor(str, 4);
        acc.count = n.rotFrames.size();
        acc.type = gltf::Accessor::Type::Scalar;
        acc.componentType = gltf::Accessor::ComponentType::Float;
        acc.min.emplace_back(0);
        acc.max.emplace_back(n.rotFrames.back() * invFrameRate);
        sampler.input = accId;

        for (auto &f : n.rotFrames) {
          str.wr.Write(f * invFrameRate);
        }
      }

      {
        auto [acc, accId] = main.NewAccessor(str, 4);
        acc.count = n.rotFrames.size();
        acc.type = gltf::Accessor::Type::Vec4;
        acc.componentType = gltf::Accessor::ComponentType::Short;
        acc.normalized = true;
        sampler.output = accId;

        for (auto r : n.rotations) {
          r = r.QConjugate() * 0x7fff;
          r = Vector4A16(_mm_round_ps(r._data, _MM_ROUND_NEAREST));
          str.wr.Write(r.Convert<int16>());
        }
      }
    }
  }
}

void DoArc(BinReaderRef rd, GLTFMain &main) {
  Header hdr;
  rd.Read(hdr);

  if (hdr.id != hdr.ID_PC) {
    throw es::InvalidHeaderError(hdr.id);
  }

  if (uint8 version = hdr.numEntriesAndVersion >> 24; version != 3) {
    throw es::InvalidVersionError(version);
  }

  std::vector<Entry> entries;
  rd.Seek(0x80);
  rd.ReadContainer(entries, hdr.numEntriesAndVersion & 0xffffff);
  rd.SetRelativeOrigin(rd.Tell());

  std::string entryNames;

  for (auto &e : entries) {
    if (e.type == Type::EntryNames) {
      rd.Seek(e.offset);
      rd.ReadContainer(entryNames, e.Size());
      break;
    }
  }

  std::vector<Animation> animations;

  size_t curEntry = 0;

  for (auto &e : entries) {
    if (e.type == Type::EntryNames || e.type == Type::Group) {
      continue;
    }

    std::string fileName;

    if (e.nameOffset > -1) {
      fileName.append(std::string(entryNames.data() + e.nameOffset));
    } else {
      fileName.append(std::to_string(curEntry));
    }

    rd.Seek(e.offset);

    switch (e.type) {
    case Type::Animation: {
      rd.Read(animations.emplace_back());
      animations.back().name = fileName;
      break;
    }

    case Type::AnimatedNode: {
      rd.Read(animations.back().nodes.emplace_back());
      animations.back().nodes.back().nodeName = fileName;
      break;
    }

    default:
      break;
    }

    curEntry++;
  }

  for (auto &a : animations) {
    LoadAnimation(a, main);
  }
}

void AppProcessFile(AppContext *ctx) {
  GLTFMain main(gltf::LoadFromBinary(ctx->GetStream(), ""));

  auto &arcs = ctx->SupplementalFiles();

  for (auto &arcBank : arcs) {
    auto arcStream = ctx->RequestFile(arcBank);
    DoArc(*arcStream.Get(), main);
  }

  if (main.animations.empty()) {
    return;
  }

  BinWritterRef wr(
      ctx->NewFile(ctx->workingFile.ChangeExtension("_out.glb")).str);
  main.FinishAndSave(wr, std::string(ctx->workingFile.GetFolder()));
}
