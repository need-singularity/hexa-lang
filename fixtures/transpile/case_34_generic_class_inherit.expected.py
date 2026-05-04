class Base:
    def tag(self):
        return "B"
class Sub(Base):
    def tag(self):
        return "S"
print(Sub().tag())
