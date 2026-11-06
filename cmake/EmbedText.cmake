function(signals_embed_text input output symbol)
  # A char array rather than a string literal: MSVC caps a single literal at about 16 KB (C2026).
  file(READ ${input} hex HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "'\\\\x\\1'," bytes "${hex}")
  string(REPEAT "'[^']+'," 24 line)
  string(REGEX REPLACE "(${line})" "\\1\n" bytes "${bytes}")
  file(WRITE ${output} "extern const char ${symbol}[];\nconst char ${symbol}[] = {\n${bytes}'\\0'};\n")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${input})
endfunction()
