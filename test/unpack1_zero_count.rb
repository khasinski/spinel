# String#unpack1 with an explicit 0-count directive ("V0") decodes no value:
# sp_str_unpack yields an empty array and Ruby's unpack1 returns nil. The
# analyzer previously typed the literal integer directive as TY_INT regardless
# of count, so codegen unboxed the (absent) element via sp_poly_to_i, coercing
# nil -> 0 and silently diverging from Ruby. A statically-detectable 0-count now
# stays TY_POLY so the nil survives. (Copilot follow-up to #1708.)
p "".unpack1("V0")
p "abcd".unpack1("V0")
p [1, 2].pack("V2").unpack1("V0")
p "abcd".unpack1("s<0")

# A normal (nonzero) literal directive still fixes the type and unboxes.
p [7].pack("V").unpack1("V")
p [7, 9].pack("V2").unpack1("V")
