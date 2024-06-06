project(LDA2TXT)

build_target(
  NAME
  lda_to_txt
  TYPE
  ESMODULE
  VERSION
  1
  LINKS
  spike-interface
  SOURCES
  lda_to_txt.cpp
  AUTHOR
  "Lukas Cone"
  DESCR
  "Convert LDA to TXT"
  START_YEAR
  2023)
