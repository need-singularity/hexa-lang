class P:
    def __init__(self, x):
        self.__x = x
    @property
    def x(self):
        return self.__x
print(P(7).x)
