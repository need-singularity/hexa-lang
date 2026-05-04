class Reg:
    def __init__(self):
        self.d = {}
    def put(self, k, v):
        self.d[k] = v
        return self
    def get(self, k):
        return self.d[k]
print(Reg().put("a", 1).put("b", 2).get("b"))
