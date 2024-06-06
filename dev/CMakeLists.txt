cmake_minimum_required(VERSION 3.12)

project(ARCDecompress VERSION 1.0)

build_target(
  NAME
  arc_decompress
  TYPE
  ESMODULE
  LINKS
  gltf-interface
  spike-interface
  SOURCES
  arc_decompress.cpp
  lzo1x.c
  AUTHOR
  "Lukas Cone"
  DESCR
  "Extract ARC archive"
  START_YEAR
  2023)

install(TARGETS arc_extract DESTINATION bin)
