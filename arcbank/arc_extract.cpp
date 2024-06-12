/*  ARCExtract
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

#include "nlohmann/json.hpp"
#include "project.h"
#include "spike/app_context.hpp"
#include "spike/except.hpp"
#include "spike/gltf.hpp"
#include "spike/io/binreader_stream.hpp"
#include "spike/master_printer.hpp"
#include "spike/type/flags.hpp"
#include <map>
#include <variant>

#include "arc.hpp"

std::string_view filters[]{
    ".ARC$",
};

static AppInfo_s appInfo{
    .filteredLoad = true,
    .header = ARCExtract_DESC " v" ARCExtract_VERSION ", " ARCExtract_COPYRIGHT
                              "Lukas Cone",
    .filters = filters,
};

AppInfo_s *AppInitModule() { return &appInfo; }

struct GLTFMain : GLTFModel {
  GLTFStream &GetTranslations() {
    if (instTrs < 0) {
      auto &str = NewStream("instance-tms");
      instTrs = str.slot;
      return str;
    }

    return Stream(instTrs);
  }

  int32 instTrs = -1;
};

struct BBOX {
  Vector min;
  Vector max;
};

struct PrimitiveCluster {
  uint32 indexStart;
  uint32 indexCount;
  uint32 vertexStart;
  uint32 vertexCount;
};

using PrimitiveSkin = std::vector<uint32>;

using PrimitiveMod = std::variant<PrimitiveSkin, PrimitiveCluster>;

struct PrimitiveHdr {
  uint32 materialIndex;
  uint32 indexBufferIndex;
  uint32 vertexBufferIndex;
  uint32 vertexBegin;
  uint32 numUsedVertices;
  uint32 offset0;
  uint32 count0;
};

struct Primitive : PrimitiveHdr {
  std::vector<PrimitiveMod> mods;

  void Read(BinReaderRef rd) {
    rd.Read(static_cast<PrimitiveHdr &>(*this));
    uint32 numMods;
    rd.Read(numMods);

    for (uint32 i = 0; i < numMods; i++) {
      uint32 type;
      rd.Read(type);

      switch (type) {
      case 0: {
        PrimitiveCluster mod;
        rd.Read(mod);
        mods.emplace_back(mod);
        break;
      }

      case 1: {
        PrimitiveSkin skin;
        rd.ReadContainer(skin);
        mods.emplace_back(skin);
        break;
      }

      default:
        throw std::runtime_error("Unknown primitive mod type");
      }
    }
  }
};

struct MeshHdr {
  uint32 numCameras;
  uint32 materialBaseIndex;
  uint32 indexBaseIndex;
  uint32 vertexBaseIndex;
  int32 unk0;
  int32 deformedMeshIndex;
  uint32 unk1;
  int32 unk2[5];
  uint32 numPrimitives;
};

struct Mesh : MeshHdr {
  std::vector<Primitive> prims;
  std::string name;
  uint32 index;

  void Read(BinReaderRef rd) {
    rd.Read(static_cast<MeshHdr &>(*this));
    rd.ReadContainer(prims, numPrimitives);
  }
};

enum class VBFlags {
  Position,
  Color,
  Normal,
  Uv0,
  Uv1,
  Uv2,
  BoneWeight,
  DeformCurve,
};

struct Attrs {
  gltf::Attributes base;
  gltf::Attributes deform;
};

Attrs ReadVertexBuffer(GLTFModel &main, BinReaderRef rd) {
  uint32 numVertices;
  uint32 stride;
  es::Flags<VBFlags> flags;
  std::string data;
  gltf::Attributes deform;
  rd.Read(numVertices);
  rd.Read(stride);
  rd.Read(flags);
  rd.ReadContainer(data, numVertices * stride);

  std::vector<Attribute> descs;
  size_t curOffset = 0;

  if (flags == VBFlags::Position) {
    descs.emplace_back(Attribute{
        .type = uni::DataType::R32G32B32,
        .format = uni::FormatType::FLOAT,
        .usage = AttributeType::Position,
    });

    curOffset += 12;
  }

  if (flags == VBFlags::Normal) {
    descs.emplace_back(Attribute{
        .type = uni::DataType::R32G32B32,
        .format = uni::FormatType::FLOAT,
        .usage = AttributeType::Normal,
    });
    curOffset += 12;
  }

  if (flags == VBFlags::Color) {
    descs.emplace_back(Attribute{
        .type = uni::DataType::R8G8B8A8,
        .format = uni::FormatType::UNORM,
        .usage = AttributeType::VertexColor,
    });
    curOffset += 4;
  }

  if (flags == VBFlags::Uv0) {
    descs.emplace_back(Attribute{
        .type = uni::DataType::R32G32,
        .format = uni::FormatType::FLOAT,
        .usage = AttributeType::TextureCoordiante,
    });
    curOffset += 8;
  }

  if (flags == VBFlags::Uv1) {
    descs.emplace_back(Attribute{
        .type = uni::DataType::R32G32,
        .format = uni::FormatType::FLOAT,
        .usage = AttributeType::TextureCoordiante,
    });
    curOffset += 8;
  }

  if (flags == VBFlags::Uv2) {
    descs.emplace_back(Attribute{
        .type = uni::DataType::R32G32,
        .format = uni::FormatType::FLOAT,
        .usage = AttributeType::TextureCoordiante,
    });
    curOffset += 8;
  }

  if (flags == VBFlags::BoneWeight) {
    descs.emplace_back(Attribute{
        .type = uni::DataType::R32G32B32A32,
        .format = uni::FormatType::FLOAT,
        .usage = AttributeType::BoneWeights,
    });
    descs.emplace_back(Attribute{
        .type = uni::DataType::R8G8B8A8,
        .format = uni::FormatType::UINT,
        .usage = AttributeType::BoneIndices,
    });
    curOffset += 20;
  }

  if (flags == VBFlags::DeformCurve) {
    auto &stream = main.GetVt12();
    auto [acc, index] = main.NewAccessor(stream, 4);
    acc.count = numVertices;
    acc.type = gltf::Accessor::Type::Vec3;
    acc.componentType = gltf::Accessor::ComponentType::Float;
    deform["POSITION"] = index;
    auto vertWr = stream.wr;

    for (size_t v = 0; v < numVertices; v++) {
      struct DeformCurve {
        float xin;
        float x;
        float xout;
        float yin;
        float y;
        float yout;
        float zin;
        float z;
        float zout;
      };

      const DeformCurve *curve = reinterpret_cast<const DeformCurve *>(
          data.data() + curOffset + stride * v);

      vertWr.Write(Vector(curve->x, curve->y, curve->z));
    }
  }

  gltf::Attributes attrs =
      main.SaveVertices(data.data(), numVertices, descs, stride);

  return {attrs, deform};
}

struct Indices {
  uint32 acc;
  uint32 size;
};

Indices ReadIndexArray(GLTFModel &main, BinReaderRef rd) {
  uint32 numIndices;
  rd.Read(numIndices);

  std::vector<uint16> data;
  rd.ReadContainer(data, numIndices);
  bool hasResetIndex = false;

  for (size_t i = 0; i < numIndices; i += 3) {
    std::swap(data[i], data[i + 1]);
    hasResetIndex |=
        data[i] == 0xFFFF || data[i + 1] == 0xFFFF || data[i + 2] == 0xFFFF;
  }

  uint32 acc;

  if (hasResetIndex) {
    std::vector<uint32> dataLong(data.begin(), data.end());
    acc = main.SaveIndices(dataLong.data(), numIndices, 4).accessorIndex;
  } else {
    acc = main.SaveIndices(data.data(), numIndices).accessorIndex;
  }

  return {acc, 2U + hasResetIndex * 2U};
}

struct NodeBase {
  size_t glIndex;
  std::string name;
  uint32 entryIndex;
  uint32 unk0[2];
  es::Matrix44 tm0;
  es::Matrix44 tm1;
  BBOX bbox;
  int32 unk1;
  int32 parentBone;
  int32 numChildren;
  int32 startChildIndex;

  void Read(BinReaderRef rd) {
    rd.Read(unk0);
    rd.Read(tm0);
    rd.Read(tm1);
    rd.Read(bbox);
    rd.Read(unk1);
    rd.Read(parentBone);
    rd.Read(numChildren);
    rd.Read(startChildIndex);
  }
};

struct Skeleton : NodeBase {
  int32 meshIndex;
  BBOX bbox1[4];
  uint32 numBones;
  uint32 startBoneEntryIndex;
  uint32 unk1; // num tms2s?
  es::Matrix44 ibm;

  void Read(BinReaderRef rd) {
    rd.Read(static_cast<NodeBase &>(*this));
    rd.Read(meshIndex);
    rd.Read(bbox1);
    rd.Read(numBones);
    rd.Read(startBoneEntryIndex);
    rd.Read(unk1);
    rd.Read(ibm);
  }
};

struct Bone : NodeBase {
  int32 boneSlotIndex;
  es::Matrix44 tm2;
  float radius;
  uint16 unk1[4];
  uint32 null1[4];
  uint16 null2;
  Vector position;
  uint16 null3;
  Vector4A16 rotation;

  void Read(BinReaderRef rd) {
    rd.Read(static_cast<NodeBase &>(*this));
    rd.Read(boneSlotIndex);
    rd.Read(tm2);
    rd.Read(radius);
    rd.Read(unk1);
    rd.Read(null1);
    rd.Read(null2);
    rd.Read(position);
    rd.Read(null3);
    rd.Read(rotation);
  }
};

struct Model : NodeBase {
  int32 meshIndex;
  BBOX bbox1[4];

  void Read(BinReaderRef rd) {
    rd.Read(static_cast<NodeBase &>(*this));
    rd.Read(meshIndex);
    rd.Read(bbox1);
  }
};

struct DeformedModel : Model {
  int32 meshes[16];

  void Read(BinReaderRef rd) {
    rd.Read(static_cast<Model &>(*this));
    rd.Read(meshes);
  }
};

struct InstancedModel : Model {
  uint32 unk;
  std::vector<CVector4> positions;
  std::vector<CVector4> rotations;

  void Read(BinReaderRef rd) {
    rd.Read(static_cast<Model &>(*this));
    uint32 numInstances;
    rd.Read(numInstances);
    rd.Read(unk);
    rd.ReadContainer(positions, numInstances);
    rd.ReadContainer(rotations, numInstances);
  }
};

struct LightNode : NodeBase {
  void Read(BinReaderRef rd) { rd.Read(static_cast<NodeBase &>(*this)); }
};

struct Camera : NodeBase {
  void Read(BinReaderRef rd) { rd.Read(static_cast<NodeBase &>(*this)); }
};

struct SkinnedModel : Model {};

struct Attachment : NodeBase {};

struct UnkNode : NodeBase {};

struct AnimatedModel : DeformedModel {
  // bmt2 only
};

using NodeVariant = std::variant<Skeleton, Bone, Model, InstancedModel,
                                 LightNode, DeformedModel, Camera, SkinnedModel,
                                 Attachment, UnkNode, AnimatedModel>;

struct Texture {
  static constexpr uint32 TYPE_PALETTE = 0x29;
  uint32 width;
  uint32 height;
  uint32 numMips;
  uint32 hash;
  uint32 type;
};

struct MaterialHdr {
  uint32 textureBaseIndex;
  uint16 unk1;
  uint16 unk2;
  uint16 unk3;
  uint16 unk4;
  uint8 unk50;
  uint8 unk5[3];
  uint32 unk6;
  uint32 unk7;
};

struct MaterialParam0 {
  int32 textureIndex;
  int32 d[6];
  void Read(BinReaderRef rd) {
    rd.Read(textureIndex);
    rd.Read(d);
  }
};

struct MaterialParam1 : MaterialParam0 {
  std::vector<uint32> d1;

  void Read(BinReaderRef rd) {
    rd.Read<MaterialParam0>(*this);
    rd.ReadContainer(d1);
  }
};

struct MaterialParam2 {
  Vector data;

  void Read(BinReaderRef rd) { rd.Read(data); }
};

struct MaterialParam3 : MaterialParam0 {
  uint8 d1[4];

  void Read(BinReaderRef rd) {
    rd.Read<MaterialParam0>(*this);
    rd.Read(d1);
  }
};

struct MaterialParam6 : MaterialParam0 {
  std::vector<uint32> d1;
  uint8 d2[4];

  void Read(BinReaderRef rd) {
    rd.Read<MaterialParam0>(*this);
    rd.ReadContainer(d1);
    rd.Read(d2);
  }
};

using MaterialParam =
    std::variant<MaterialParam0, MaterialParam1, MaterialParam2, MaterialParam3,
                 MaterialParam6>;

struct Material : MaterialHdr {
  std::vector<MaterialParam> params;

  void Read(BinReaderRef rd) {
    rd.Read<MaterialHdr>(*this);

    rd.ReadContainerLambda(params, [](BinReaderRef rd, MaterialParam &item) {
      uint32 type;
      rd.Read(type);

      switch (type) {
      case 0:
      case 4: {
        MaterialParam0 p;
        rd.Read(p);
        item = p;

        break;
      }
      case 1:
      case 5: {
        MaterialParam1 p;
        rd.Read(p);
        item = p;

        break;
      }
      case 2: {
        MaterialParam2 p;
        rd.Read(p);
        item = p;

        break;
      }
      case 3: {
        MaterialParam3 p;
        rd.Read(p);
        item = p;

        break;
      }
      case 6: {
        MaterialParam6 p;
        rd.Read(p);
        item = p;

        break;
      }
      case 7:
        break;
      default:
        assert(false);
      }
    });
  }
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

void ExtractTexture(AppContext *actx, BinReaderRef rd, size_t entrySize,
                    const std::string &fileName, TexelOutput *tOut = nullptr) {
  Texture hdr;
  rd.Read(hdr);

  std::string buffer;
  rd.ReadContainer(buffer, entrySize - sizeof(hdr));

  if (hdr.type == hdr.TYPE_PALETTE) {
    uint32 numPalettes;
    memcpy(&numPalettes, buffer.data(), 4);
    // what to do with more palettes?
    uint32 *palette = reinterpret_cast<uint32 *>(buffer.data() + 4);
    uint8 *dataBegin =
        reinterpret_cast<uint8 *>(buffer.data() + numPalettes * 1024 + 4);
    uint8 *dataEnd = reinterpret_cast<uint8 *>(buffer.data() + buffer.size());
    std::span<uint8> data(dataBegin, dataEnd);
    std::vector<uint32> dataOut;
    dataOut.reserve(data.size());

    for (auto d : data) {
      dataOut.emplace_back(palette[d]);
    }

    buffer.resize(dataOut.size() * 4);
    memcpy(buffer.data(), dataOut.data(), dataOut.size() * 4);
  }

  NewTexelContextCreate ctx{
      .width = uint16(hdr.width),
      .height = uint16(hdr.height),
      .baseFormat =
          {
              .type =
                  [&] {
                    switch (hdr.type) {
                    case hdr.TYPE_PALETTE:
                    case 21:
                      return TexelInputFormatType::RGBA8;
                    case CompileFourCC("DXT1"):
                      return TexelInputFormatType::BC1;
                    case CompileFourCC("DXT3"):
                      return TexelInputFormatType::BC2;
                    case 26:
                      return TexelInputFormatType::RGBA4;
                    case 25:
                      return TexelInputFormatType::RGB5A1;
                    default:
                      throw std::runtime_error("Invalid texture format: " +
                                               std::to_string(hdr.type));
                    }
                  }(),
          },
      .numMipmaps = uint8(hdr.numMips),
      .data = buffer.data(),
      .texelOutput = tOut,
      .formatOverride =
          tOut ? TexelContextFormat::UPNG : TexelContextFormat::Config,
  };

  if (tOut) {
    actx->NewImage(ctx);
  } else {
    actx->ExtractContext()->NewImage(fileName, ctx);
  }
}

void AppProcessFile(AppContext *ctx) {
  BinReaderRef rd(ctx->GetStream());
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

  GLTFMain main;
  // main.QuantizeMesh(false);

  struct TexturePtr {
    std::string name;
    int32 offset;
    uint32 size;
    int32 glIndex = -1;
  };

  std::vector<Mesh> meshes;
  std::vector<Mesh> skinnedMeshes;
  std::vector<Indices> indexBuffers(hdr.numIndexBuffers);
  std::vector<Attrs> vertexBuffers(hdr.numVertexBuffers);
  std::vector<TexturePtr> textures;
  std::vector<NodeVariant> nodes;
  std::vector<Animation> animations;
  es::Matrix44 skeletonTm;

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
    case Type::Texture: {
      textures.emplace_back(TexturePtr{
          .name = fileName,
          .offset = int32(e.offset),
          .size = e.Size(),
      });
      // ExtractTexture(ctx->ExtractContext(), rd, e.Size(), fileName);
      // textureNames.emplace_back(fileName);
      break;
    }
    case Type::ReferencedTexture: {
      textures.emplace_back(TexturePtr{
          .name = fileName,
          .offset = -1,
          .size = e.Size(),
      });
      break;
    }
    case Type::LightmapTexture: {
      textures.emplace_back(TexturePtr{
          .name = fileName,
          .offset = int32(e.offset + 4 * 6),
          .size = e.Size(),
      });
      break;
    }

    case Type::Mesh: {
      Mesh data;
      data.name = fileName;
      data.index = e.index;
      rd.Read(data);
      meshes.emplace_back(data);
      break;
    }

    case Type::SkinnedMesh: {
      Mesh data;
      data.name = fileName;
      data.index = std::max(int32(e.index), 0);
      rd.Read(data);
      skinnedMeshes.emplace_back(data);
      break;
    }

    case Type::IndexBuffer: {
      indexBuffers.at(e.index) = ReadIndexArray(main, rd);
      break;
    }

    case Type::VertexBuffer: {
      vertexBuffers.at(e.index) = ReadVertexBuffer(main, rd);
      break;
    }

    case Type::Model: {
      Model nde;
      nde.name = fileName;
      nde.entryIndex = e.index;
      rd.Read(nde);
      nodes.emplace_back(nde);
      break;
    }

    case Type::SkinnedModel: {
      SkinnedModel nde;
      nde.name = fileName;
      nde.entryIndex = e.index;
      rd.Read(nde);
      nodes.emplace_back(nde);
      break;
    }

    case Type::DeformedModel: {
      DeformedModel nde;
      nde.name = fileName;
      nde.entryIndex = e.index;
      rd.Read(nde);
      nodes.emplace_back(nde);
      break;
    }

    case Type::AnimatedModel: {
      AnimatedModel nde;
      nde.name = fileName;
      nde.entryIndex = e.index;
      rd.Read(nde);
      nodes.emplace_back(nde);
      break;
    }

    case Type::InstancedModel: {
      InstancedModel nde;
      nde.name = fileName;
      nde.entryIndex = e.index;
      rd.Read(nde);
      nodes.emplace_back(nde);
      break;
    }

    case Type::Skeleton: {
      Skeleton nde;
      nde.name = fileName;
      nde.entryIndex = e.index;
      rd.Read(nde);
      skeletonTm = nde.ibm;
      nodes.emplace_back(nde);
      break;
    }

    case Type::RigNode: {
      Bone nde;
      nde.name = fileName;
      nde.entryIndex = e.index;
      rd.Read(nde);
      nodes.emplace_back(nde);
      break;
    }

    case Type::LightNode: {
      LightNode nde;
      nde.name = fileName;
      nde.entryIndex = e.index;
      rd.Read(nde);
      nodes.emplace_back(nde);
      break;
    }

    case Type::Camera: {
      Camera nde;
      nde.name = fileName;
      nde.entryIndex = e.index;
      rd.Read(nde);
      nodes.emplace_back(nde);
      break;
    }

    case Type::Attachment: {
      Attachment nde;
      nde.name = fileName;
      nde.entryIndex = e.index;
      rd.Read(nde);
      nodes.emplace_back(nde);
      break;
    }

    case Type::UnkNode: {
      UnkNode nde;
      nde.name = fileName;
      nde.entryIndex = e.index;
      rd.Read(nde);
      nodes.emplace_back(nde);
      break;
    }

    case Type::Material: {
      Material mat;
      rd.Read(mat);
      gltf::Material &gMat = main.materials.emplace_back();
      gMat.name = fileName;
      gMat.doubleSided = true;
      gMat.alphaMode = gltf::Material::AlphaMode::Mask;

      for (auto &p : mat.params) {
        if (std::visit(
                [&](auto &item) {
                  using Type = std::decay_t<decltype(item)>;
                  if constexpr (!std::is_same_v<Type, MaterialParam2>) {
                    if (item.textureIndex > -1) {
                      TexturePtr &ptr =
                          textures.at(mat.textureBaseIndex + item.textureIndex);

                      if (ptr.offset < 0) {
                        return true;
                      }

                      if (ptr.glIndex < 0) {
                        ptr.glIndex = main.textures.size();
                        gltf::Texture &ntex = main.textures.emplace_back();
                        ntex.source = main.images.size();
                        gltf::Image &img = main.images.emplace_back();
                        img.mimeType = "image/png";
                        img.name = ptr.name;
                        GLTFStream &str = main.NewStream(ptr.name);
                        img.bufferView = str.slot;

                        struct TexStream : TexelOutput {
                          GLTFStream &str;

                          TexStream(GLTFStream &str_) : str{str_} {}

                          void SendData(std::string_view data) override {
                            str.wr.WriteContainer(data);
                          }
                          void NewFile(std::string) override {}
                        };

                        TexStream tStr{str};

                        rd.Push();
                        rd.Seek(ptr.offset);
                        ExtractTexture(ctx, rd, ptr.size, ptr.name, &tStr);
                        rd.Pop();
                      }
                      gMat.pbrMetallicRoughness.baseColorTexture.index =
                          ptr.glIndex;
                      return false;
                    }
                  }

                  return true;
                },
                p)) {
          break;
        }
      }
      break;
    }

    default:
      break;
    }

    curEntry++;
  }

  const size_t nodeStartIndex = main.nodes.size();
  std::vector<uint32> bones(hdr.numRigNodes);
  std::vector<es::Matrix44> ibms(hdr.numRigNodes);
  std::map<int32, std::vector<size_t>> models;
  bool useGPUInstances = false;

  for (auto &n : nodes) {
    InstancedModel *iModel = nullptr;
    NodeBase &node = std::visit(
        [&](auto &i) -> NodeBase & {
          using EntryType = std::decay_t<decltype(i)>;
          if constexpr (std::is_same_v<EntryType, Bone>) {
            if (i.boneSlotIndex > -1) {
              bones.at(i.boneSlotIndex) = main.nodes.size();
              ibms.at(i.boneSlotIndex) = i.tm2 * skeletonTm;
            }
          } else if constexpr (std::is_same_v<EntryType, Model> ||
                               std::is_same_v<EntryType, InstancedModel> ||
                               std::is_same_v<EntryType, SkinnedModel> ||
                               std::is_same_v<EntryType, Skeleton>) {
            models[i.meshIndex].emplace_back(main.nodes.size());
          } else if constexpr (std::is_same_v<EntryType, DeformedModel> ||
                               std::is_same_v<EntryType, AnimatedModel>) {
            models[i.meshIndex].emplace_back(main.nodes.size());
            for (int32 m : i.meshes) {
              models[m].emplace_back(main.nodes.size());
            }
          }

          if constexpr (std::is_same_v<EntryType, InstancedModel>) {
            iModel = &i;
          }
          return i;
        },
        n);
    gltf::Node glNode;
    glNode.name = node.name;
    node.glIndex = main.nodes.size();

    if (false) {
      useGPUInstances = true;
      auto &attrs =
          glNode
              .GetExtensionsAndExtras()["extensions"]["EXT_mesh_gpu_instancing"]
                                       ["attributes"];
      {
        auto &str = main.GetTranslations();
        auto [accPos, accPosIndex] = main.NewAccessor(str, 4);
        accPos.type = gltf::Accessor::Type::Vec3;
        accPos.componentType = gltf::Accessor::ComponentType::Float;
        accPos.count = iModel->positions.size();
        Vector4A16 bMin(iModel->bbox.min);
        Vector4A16 bMax(iModel->bbox.max);
        Vector4A16 mid = bMin + bMax / 2;

        for (auto &p : iModel->positions) {
          Vector4A16 normPos(p.Convert<float>());
          Vector4A16 pos = mid + bMax * normPos * (1.f / 0x7f);
          str.wr.Write<Vector>(pos);
        }

        attrs["TRANSLATION"] = accPosIndex;
      }

      /*{
        auto &str = main.GetTranslations();
        auto [accRot, accRotIndex] = main.NewAccessor(str, 2);
        accRot.type = gltf::Accessor::Type::Vec4;
        accRot.componentType = gltf::Accessor::ComponentType::Short;
        accRot.normalized = true;
        accRot.count = iModel->rotations.size();

        attrs["ROTATION"] = accRotIndex;
      }*/
    }

    main.nodes.emplace_back(std::move(glNode));
  }

  for (auto &n : nodes) {
    NodeBase &node = std::visit([&](auto &i) -> NodeBase & { return i; }, n);
    memcpy(main.nodes.at(node.glIndex).matrix.data(), &node.tm0,
           sizeof(es::Matrix44));

    if (node.parentBone > -1) {
      main.nodes.at(nodeStartIndex + node.parentBone)
          .children.emplace_back(node.glIndex);
    } else {
      main.scenes.front().nodes.emplace_back(node.glIndex);
    }
  }

  if (bones.size() > 0) {
    gltf::Skin &skin = main.skins.emplace_back();
    skin.joints = bones;
    auto &str = main.SkinStream();
    auto [acc, id] = main.NewAccessor(str, 16);
    acc.type = gltf::Accessor::Type::Mat4;
    acc.componentType = gltf::Accessor::ComponentType::Float;
    acc.count = bones.size();
    str.wr.WriteContainer(ibms);
    skin.inverseBindMatrices = id;
  }

  auto DoMesh = [&](Mesh &m, uint32 indexOffset) {
    if (m.prims.empty()) {
      return;
    }

    gltf::Mesh mesh;
    bool useSkin = false;

    for (auto &p : m.prims) {
      auto vertexAttrs =
          vertexBuffers.at(m.vertexBaseIndex + p.vertexBufferIndex);
      PrimitiveSkin curSkin;

      for (auto &md : p.mods) {
        std::visit(
            [&](auto &item) {
              using Type = std::decay_t<decltype(item)>;
              if constexpr (std::is_same_v<Type, PrimitiveCluster>) {
                Indices ids =
                    indexBuffers.at(m.indexBaseIndex + p.indexBufferIndex);
                auto indexAccess = main.accessors.at(ids.acc);
                indexAccess.byteOffset += item.indexStart * ids.size;
                indexAccess.count = item.indexCount;

                size_t idxAcc = main.accessors.size();
                main.accessors.emplace_back(indexAccess);

                gltf::Primitive prim;
                prim.material = m.materialBaseIndex + p.materialIndex;
                prim.indices = idxAcc;
                prim.attributes = vertexAttrs.base;
                prim.mode = gltf::Primitive::Mode::Triangles;
                if (vertexAttrs.deform.size()) {
                  prim.targets.emplace_back(vertexAttrs.deform);
                }

                if (curSkin.size() > 0) {
                  /*std::vector<uint16> indices;

                  {
                    BinReaderRef rds(main.Stream(indexAccess.bufferView).str);
                    rds.Push();
                    rds.Seek(indexAccess.byteOffset);
                    rds.ReadContainer(indices, indexAccess.count);
                    rds.Pop();
                  }

                  std::sort(indices.begin(), indices.end());
                  std::unique(indices.begin(), indices.end());
                  std::span<uint16> uniqIndices(
                      indices.begin(),
                      std::unique(indices.begin(), indices.end()));*/

                  std::vector<UCVector4> weights;
                  {
                    gltf::Accessor &acc =
                        main.accessors.at(prim.attributes.at("WEIGHTS_0"));
                    BinReaderRef rds(main.Stream(acc.bufferView).str);
                    rds.Push();
                    rds.Seek(acc.byteOffset + 4 * item.vertexStart);
                    rds.ReadContainer(weights, item.vertexCount);
                    rds.Pop();
                  }

                  gltf::Accessor &acc =
                      main.accessors.at(prim.attributes.at("JOINTS_0"));
                  BinReaderRef rds(main.Stream(acc.bufferView).str);
                  BinWritterRef wrs(main.Stream(acc.bufferView).str);
                  wrs.Push();
                  rds.Push();

                  rds.Seek(acc.byteOffset + 4 * item.vertexStart);
                  wrs.Seek(acc.byteOffset + 4 * item.vertexStart);

                  for (uint32 j = 0; j < item.vertexCount; j++) {
                    uint8 joints[4];
                    rds.Read(joints);
                    UCVector4 wts = weights.at(j);

                    for (uint8 cwt = 0; uint8 & n : joints) {
                      if (wts[cwt++]) {
                        n = curSkin.at(n / 3);
                      }
                    }

                    wrs.Write(joints);
                  }
                  wrs.Pop();
                  rds.Pop();
                }

                mesh.primitives.emplace_back(std::move(prim));
              } else {
                curSkin = item;
                useSkin = true;
              }
            },
            md);
      }
    }

    try {
      for (auto &n : models.at(m.index + indexOffset)) {
        main.nodes.at(n).mesh = main.meshes.size();

        if (useSkin) {
          main.nodes.at(n).skin = main.skins.size() - 1;
        }
      }
    } catch (const std::out_of_range &) {
      PrintWarning("Mesh node: ", m.name, "appears to be unlinked.");
    }

    main.meshes.emplace_back(std::move(mesh));
  };

  for (auto &m : meshes) {
    DoMesh(m, hdr.numSkinnedModels);
  }

  for (auto &m : skinnedMeshes) {
    DoMesh(m, 0);
  }

  if (!main.meshes.empty() || !main.animations.empty()) {
    BinWritterRef wr(
        ctx->NewFile(ctx->workingFile.ChangeExtension2("glb")).str);

    if (useGPUInstances) {
      main.extensionsRequired.emplace_back("EXT_mesh_gpu_instancing");
      main.extensionsUsed.emplace_back("EXT_mesh_gpu_instancing");
    }

    main.FinishAndSave(wr, std::string(ctx->workingFile.GetFolder()));
  }

  for (auto &t : textures) {
    if (t.offset < 0 || t.glIndex > -1) {
      continue;
    }
    rd.Seek(t.offset);
    ExtractTexture(ctx, rd, t.size, t.name);
  }

  std::string buffer;
  std::string currentGroup;

  for (auto &e : entries) {
    switch (e.type) {
    case Type::EntryNames:
    case Type::Texture:
    case Type::ReferencedTexture:
    case Type::LightmapTexture:
    case Type::Mesh:
    case Type::SkinnedMesh:
    case Type::IndexBuffer:
    case Type::VertexBuffer:
    case Type::Model:
    case Type::SkinnedModel:
    case Type::DeformedModel:
    case Type::InstancedModel:
    case Type::Skeleton:
    case Type::RigNode:
    case Type::LightNode:
    case Type::Camera:
    case Type::Attachment:
    case Type::UnkNode:
    case Type::Material:
    case Type::AnimatedNode:
    case Type::Animation:
    case Type::DeformedMesh:
      break;

    case Type::Group:
      currentGroup = entryNames.data() + e.nameOffset;
      currentGroup.push_back('/');
      break;

    default: {
      std::string fileName(currentGroup);

      if (e.nameOffset > -1) {
        fileName.append(std::string(entryNames.data() + e.nameOffset));
      } else {
        fileName.append(std::to_string(curEntry));
      }

      if (e.type != Type::PlainData) {
        fileName.push_back('.');
        fileName.append(std::to_string(uint32(e.type)));
      }

      auto *ectx = ctx->ExtractContext();
      ectx->NewFile(fileName);
      rd.Seek(e.offset);
      rd.ReadContainer(buffer, e.Size());
      ectx->SendData(buffer);

      break;
    }
    }

    curEntry++;
  }
}
