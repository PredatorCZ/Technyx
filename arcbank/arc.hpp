#pragma once
#include "spike/util/supercore.hpp"

struct Header {
  static constexpr uint32 ID_PC = CompileFourCC("ARCC");
  static constexpr uint32 ID_PS2 = CompileFourCC("ARCP");
  static constexpr uint32 ID_XBOX = CompileFourCC("ARCX");
  static constexpr uint32 ID_GC = CompileFourCC("ARCN");
  uint32 id;
  uint32 numEntriesAndVersion;
  uint16 numTextures;
  uint16 numModels;
  uint16 numAttachments;
  uint16 numAttachedModels;
  uint16 numSkeletons;
  uint16 numCameras;
  uint16 unk1;
  uint16 numRigNodes;
  uint16 numMaterials;
  uint16 numMeshes;
  uint16 unk2;
  uint16 numReferencedTextures;
  uint16 unk22[4];
  uint16 numIndexBuffers;
  uint16 numVertexBuffers;
  uint16 unk20[2];
  uint16 numSkinnedModels;
  uint16 numDeformedMeshes;
  uint16 numAnimations;
  uint16 unk3;
  uint16 numAnimatedNodes;
  uint16 unk4;
  uint16 numLightNodes;
};

enum class Type : uint8 {
  PlainData,
  Texture,
  Material,
  Mesh = 9,
  IndexBuffer = 0xf,
  VertexBuffer = 0x10,
  LightmapTexture = 0x11,
  SkinnedMesh = 0x15,
  Attachment = 0x1c,
  Model = 0x1d,
  DeformedModel = 0x1e,
  SkinnedModel = 0x1f,
  DeformedMesh = 0x19,
  AnimatedModel = 0x20,
  Skeleton = 0x21,
  Camera = 0x25,
  RigNode = 0x27,
  InstancedModel = 0x28,
  Animation = 0x29,
  AnimatedNode = 0x31,
  ReferencedTexture = 0x34,
  UnkNode = 0x35,
  LightNode = 0x36,
  EntryNames = 0xfd,
  Group = 0xff,
};

struct Entry {
  uint32 index;
  uint32 offset;
  int32 nameOffset;
  Type type;
  uint8 size[3];

  uint32 Size() const {
    return uint32(size[0]) << 16 | uint32(size[1]) << 8 | size[2];
  }
};
