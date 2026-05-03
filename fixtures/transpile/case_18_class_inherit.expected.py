class Base:
    def greet(self):
        return "base"
class Child(Base):
    def greet(self):
        return "child"
print(Child().greet())
print(Base().greet())
