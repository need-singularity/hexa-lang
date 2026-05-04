class C:
    @classmethod
    def mk(cls):
        return cls()
    def ping(self):
        return "ok"
print(C.mk().ping())
