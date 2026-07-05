# An unresolved method call inside File.join's arguments emits the
# sp_raise_nomethod(...) gate token (a side-effecting sp_RbVal), which the
# File.join component slot -- a `const char *` -- must keep rather than pass
# through raw. emit_str_expr's token match (added by #1713) wraps it in
# sp_poly_to_s so the NoMethodError still raises at runtime and can be rescued.
# The node stays TY_UNKNOWN, so the plain poly-coercion path does not catch it;
# only the token match does. (Copilot follow-up to #1713.)
def maybe(flag)
  flag ? nil : nil
end

x = maybe(true)   # poly nil receiver -> the .no_such_method call is unresolved
begin
  File.join("/tmp", x.no_such_method)
  puts "no raise"
rescue NoMethodError => e
  puts "rescued NoMethodError"
end
puts File.join("a", "b")
