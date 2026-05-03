class Box:
    def __init__(self, v):
        self.v = v
    def get(self):
        return self.v
print(Box(11).get())
