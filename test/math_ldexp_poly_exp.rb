# Math.ldexp's exponent argument must unbox a poly-typed value (an Integer|Float
# union) to an mrb_int before the (int) cast. Previously it was emitted with a
# plain emit_expr, so a poly exponent became `(int)<sp_RbVal>` -- invalid C,
# mirroring the float-arg coercion bug #1704 fixed for the mantissa.
# (Copilot follow-up to #1704.)
def pick(flag)
  flag ? 2 : 5
end

e = pick(true)    # poly: Integer union
f = pick(false)
puts Math.ldexp(1.5, e)
puts Math.ldexp(4.0, f)

vals = [1, 2.0, 3]  # poly-array element flows in as the exponent
puts Math.ldexp(3.0, vals[0])
puts Math.ldexp(3.0, vals[2])
