class Holder:
    def __init__(self, v):
        self.v = v
    def __enter__(self):
        return self.v
    def __exit__(self, et, ev, tb):
        return False
with Holder(99) as v:
    print(v)
