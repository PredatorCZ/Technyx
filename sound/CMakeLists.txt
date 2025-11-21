project(HDR2WAV)

build_target(
  NAME
  hdr_to_wav
  TYPE
  ESMODULE
  VERSION
  1
  LINKS
  spike-interface
  SOURCES
  hdr_to_wav.cpp
  AUTHOR
  "Lukas Cone"
  DESCR
  "Convert HDR/RAW sound bank to WAVs"
  START_YEAR
  2025)
