# Array#[](start, len) and Array#[](range) on IntArray.
# Previously returned 0 (single-arg [] dispatch fell through both
# inference and codegen for the slice forms).

a = [10, 20, 30, 40, 50]

# Two-arg slice
b = a[1, 2]
puts b.length    # 2
puts b[0]        # 20
puts b[1]        # 30

# Range slice
c = a[1..3]
puts c.length    # 3
puts c[0]        # 20
puts c[2]        # 40

# Edge: zero length
e = a[2, 0]
puts e.length    # 0

# Edge: clipped at end
f = a[3, 99]
puts f.length    # 2
puts f[0]        # 40
puts f[1]        # 50

# Negative start
g = a[-2, 2]
puts g.length    # 2
puts g[0]        # 40
puts g[1]        # 50

# Single-arg [] still works
puts a[0]        # 10
puts a[-1]       # 50
