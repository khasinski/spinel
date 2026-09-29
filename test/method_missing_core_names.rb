class Img
  include Comparable
  def initialize(v) = @v = v
  def v = @v
  def <=>(o) = @v <=> o.v
  def method_missing(name, *args, **opts)
    "#{name}(#{args.join(",")}) on #{@v}"
  end
  def respond_to_missing?(name, all = false) = true
end
a = [3, 1, 2]
p a.max, a.min, a.flatten
im = Img.new(7)
p im.max
p im.min
p im.flatten
p im.round(2)
p im.to_s.start_with?("#<Img")
p im.clamp(Img.new(1), Img.new(5)).v
x = [im, [1, [2]]].sample
x = im
p x.max
# keywords reach the hook as keywords
class Kw
  def method_missing(name, *args, **opts) = [name, args, opts]
end
p Kw.new.round(ndigits: 2), Kw.new.max(1)
# a subclass of a class the program does not define keeps that class's methods
class E < StandardError
  def method_missing(name, *args) = :hook
end
e = E.new("boom")
p e.backtrace, e.zork
# a hook, and a core name the class answers itself, under a visibility call
class Vis
  public def max = :own
  private def method_missing(name, *args) = [:hook, name]
end
p Vis.new.max, Vis.new.min, Vis.new.flatten
