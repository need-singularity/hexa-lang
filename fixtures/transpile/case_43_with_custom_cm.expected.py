class CM:
    def __enter__(self):
        return "in"
    def __exit__(self, et, ev, tb):
        return False
with CM() as x:
    print(x)
