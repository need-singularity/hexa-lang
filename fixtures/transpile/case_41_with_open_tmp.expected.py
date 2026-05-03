import tempfile
import os
path = tempfile.mktemp()
with open(path, "w") as f:
    f.write("hi")
with open(path, "r") as f:
    print(f.read())
os.remove(path)
