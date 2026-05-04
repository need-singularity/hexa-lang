import tempfile
import os
p1 = tempfile.mktemp()
p2 = tempfile.mktemp()
with open(p1, "w") as a, open(p2, "w") as b:
    a.write("A")
    b.write("B")
print("ok")
os.remove(p1)
os.remove(p2)
