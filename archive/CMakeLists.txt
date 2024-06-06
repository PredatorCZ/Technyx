project(CDFILESExtract)

build_target(
  NAME
  cdfiles_extract
  TYPE
  ESMODULE
  VERSION
  1
  LINKS
  spike-interface
  SOURCES
  cdfiles_extract.cpp
  AUTHOR
  "Lukas Cone"
  DESCR
  "Extract CDFILES archive"
  START_YEAR
  2023)
