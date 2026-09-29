# A class deriving from Array keeps its elements in an array of its own and
# answers Array's methods through it.
class Stack < Array
  def peek = last
  def push_twice(x)
    push(x)
    push(x)
  end
  def <<(x)
    super(x * 10)
  end
end
s = Stack.new
s << 1 << 2
s.push_twice(3)
p s, s.size, s.peek, s.first, s[1], s.class
p s.map { |x| x + 1 }, s.max, s.include?(20), s.sum
s.each { |x| print x, " " }
puts
p s.select { |x| x > 5 }, s.to_a.class, s == [10, 20, 3, 3]
t = Stack[4, 5]
p t, t.length, Stack.new(2, 0), Stack.new([7, 8]).last
class Row < Array
  def initialize(n)
    super(n) { |i| i * i }
  end
  def total = sum
end
r = Row.new(4)
p r, r.total, r.reverse, r.sort.first(2)
# eql? compares the elements too
p Stack[1, 2].eql?(Stack[1, 2]), Stack[1, 2].eql?(Stack[1, 3]), Stack[1].eql?([1])
# a setter, a call of five arguments, a splat
q = Stack[1, 2, 3, 4, 5, 6]
q[0] = 9
more = [7, 8]
q.push(*more)
p q, q.values_at(0, 1, 2, 3, 4)
# an enumerator over a method the subclass forwards
en = Stack[4, 5, 6].to_enum(:each)
p en.map { |x| x * 3 }
