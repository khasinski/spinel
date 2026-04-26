# File.binread: binary-safe read returning a byte IntArray.
#
# Spinel's File.read returns a NUL-terminated String which truncates
# at the first NUL byte in the file. binread bypasses String entirely
# and returns sp_IntArray with one entry per byte (0..255). This
# lets you parse binary formats (WAD/PNG/etc) without losing data.
#
# Note: this is a Spinel-specific extension. CRuby's File.binread
# returns a String, so this test isn't run by `make test` (the
# runner only compiles top-level test/*.rb and compares to CRuby).
#
# Manual verification:
#   echo -ne "\xFFhello\x00world" > /tmp/sp_bin.bin
#   spinel test/bm_file_binread/main.rb && ./main
#   # expected: 12 / 255 / 104 / 0 / 119

bytes = File.binread("/tmp/sp_bin.bin")
puts bytes.length
puts bytes[0]    # 0xFF
puts bytes[1]    # 'h' = 104
puts bytes[6]    # NUL = 0
puts bytes[7]    # 'w' = 119
