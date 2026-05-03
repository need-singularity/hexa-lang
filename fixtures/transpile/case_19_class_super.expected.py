class A:
    def name(self):
        return "A"
class B(A):
    def name(self):
        return (super().name() + "B")
print(B().name())
