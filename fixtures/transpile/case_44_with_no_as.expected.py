class CM:
    def __enter__(self):
        return self
    def __exit__(self, et, ev, tb):
        return False
with CM():
    print("body")
