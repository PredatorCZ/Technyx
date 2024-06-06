project(ARCExtract)

build_target(
  NAME
  arc_extract
  TYPE
  ESMODULE
  VERSION
  1
  LINKS
  gltf-interface
  spike-interface
  SOURCES
  arc_extract.cpp
  AUTHOR
  "Lukas Cone"
  DESCR
  "Extract ARC archive"
  START_YEAR
  2023)

project(ARCAnim)

build_target(
  NAME
  arc_anim
  TYPE
  ESMODULE
  VERSION
  1
  LINKS
  gltf-interface
  spike-interface
  SOURCES
  arc_anim.cpp
  AUTHOR
  "Lukas Cone"
  DESCR
  "Apply animations from ARC archive"
  START_YEAR
  2023)
