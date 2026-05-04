def deco(f):
    return f
@deco
def ping():
    return "p"
print(ping())
