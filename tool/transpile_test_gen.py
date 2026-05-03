#!/usr/bin/env python3
"""
Generates 50+ round-trip fixture pairs (case_NN.hexa, case_NN.expected.py)
under fixtures/transpile/.

The expected.py is computed by running the transpiler itself and capturing
its current output, then "freezing" that as the round-trip target. This
makes the test a strict regression: any future change to codegen that
alters output is detected; AND the produced .py is then executed to
ensure it imports/runs.

Each .hexa case must exercise one or more of the 7 features:
  class, decorator, generics, async, with, list-comp, dict-comp.
"""
from __future__ import annotations

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
FIX = os.path.join(ROOT, "fixtures", "transpile")
sys.path.insert(0, HERE)
from hexa_to_py import transpile_source  # noqa: E402

# Each entry: (label, hexa_src). expected.py is captured from transpiler output
# at generation time. exec runs each .py to ensure it imports cleanly.
CASES: list[tuple[str, str]] = [
    # 1-7: basic def/return/list/dict (the prior subset)
    ("def_return_int", """
fn f() -> int { return 42 }
print(f())
"""),
    ("def_args", """
fn add(a, b) { return a + b }
print(add(2, 3))
"""),
    ("def_default_arg", """
fn greet(name = "world") { return name }
print(greet())
print(greet("hexa"))
"""),
    ("list_literal", """
let xs = [1, 2, 3]
print(xs)
"""),
    ("dict_literal", """
let d = {"a": 1, "b": 2}
print(d["a"])
"""),
    ("nested_collections", """
let m = {"xs": [1, 2, 3], "ys": [4, 5]}
print(m["xs"][0])
"""),
    ("multi_return", """
fn first(xs) { return xs[0] }
print(first([10, 20, 30]))
"""),

    # 8-14: list / dict / set comprehensions
    ("listcomp_simple", """
let xs = [x * x for x in [1, 2, 3]]
print(xs)
"""),
    ("listcomp_filter", """
let xs = [x for x in [1, 2, 3, 4, 5] if (x % 2) == 0]
print(xs)
"""),
    ("listcomp_nested", """
let xs = [x + y for x in [1, 2] for y in [10, 20]]
print(xs)
"""),
    ("dictcomp_simple", """
let d = {x: x * 2 for x in [1, 2, 3]}
print(d)
"""),
    ("dictcomp_filter", """
let d = {x: x * x for x in [1, 2, 3, 4] if x > 1}
print(d)
"""),
    ("setcomp_simple", """
let s = {x % 3 for x in [1, 2, 3, 4, 5, 6]}
print(sorted(s))
"""),
    ("listcomp_in_call", """
print(sum([x for x in [1, 2, 3, 4]]))
"""),

    # 15-22: classes
    ("class_empty", """
class Empty { }
let e = Empty()
print(type(e).__name__)
"""),
    ("class_init", """
class Point {
  fn __init__(self, x, y) {
    self.x = x
    self.y = y
  }
}
let p = Point(1, 2)
print(p.x + p.y)
"""),
    ("class_method", """
class Counter {
  fn __init__(self) { self.n = 0 }
  fn tick(self) { self.n = self.n + 1 }
}
let c = Counter()
c.tick()
c.tick()
print(c.n)
"""),
    ("class_inherit", """
class Base { fn greet(self) { return "base" } }
class Child : Base { fn greet(self) { return "child" } }
print(Child().greet())
print(Base().greet())
"""),
    ("class_super", """
class A { fn name(self) { return "A" } }
class B : A { fn name(self) { return super().name() + "B" } }
print(B().name())
"""),
    ("class_classmethod", """
class C {
  @classmethod
  fn mk(cls) { return cls() }
  fn ping(self) { return "ok" }
}
print(C.mk().ping())
"""),
    ("class_staticmethod", """
class C {
  @staticmethod
  fn add(a, b) { return a + b }
}
print(C.add(2, 5))
"""),
    ("class_attr_default", """
class K {
  fn __init__(self, x = 10) { self.x = x }
}
print(K().x + K(5).x)
"""),

    # 23-28: decorators
    ("decorator_simple", """
fn deco(f) { return f }
@deco
fn ping() { return "p" }
print(ping())
"""),
    ("decorator_args", """
fn tag(t) {
  fn inner(f) {
    fn wrap() { return t + ":" + f() }
    return wrap
  }
  return inner
}
@tag("X")
fn name() { return "n" }
print(name())
"""),
    ("decorator_stack", """
fn d1(f) { fn w() { return "1[" + f() + "]" } return w }
fn d2(f) { fn w() { return "2[" + f() + "]" } return w }
@d1
@d2
fn raw() { return "r" }
print(raw())
"""),
    ("decorator_class", """
fn add_marker(cls) { cls.marker = "yes" return cls }
@add_marker
class M { }
print(M.marker)
"""),
    ("decorator_property", """
class P {
  fn __init__(self, x) { self.__x = x }
  @property
  fn x(self) { return self.__x }
}
print(P(7).x)
"""),
    ("decorator_method_static", """
class S {
  @staticmethod
  fn ping() { return "ok" }
}
print(S.ping())
"""),

    # 29-34: type generics (parsed, then stripped)
    ("generic_fn", """
fn id<T>(x: T) -> T { return x }
print(id(7))
print(id("s"))
"""),
    ("generic_two_params", """
fn pair<A, B>(a: A, b: B) -> [A] { return [a, b] }
print(pair(1, 2))
"""),
    ("generic_class", """
class Box<T> {
  fn __init__(self, v: T) { self.v = v }
  fn get(self) -> T { return self.v }
}
print(Box(11).get())
"""),
    ("generic_nested_type_anno", """
fn make() -> [int] { return [1, 2, 3] }
print(make())
"""),
    ("generic_dict_type_anno", """
fn make() -> {str: int} { return {"a": 1} }
print(make()["a"])
"""),
    ("generic_class_inherit", """
class Base<T> { fn tag(self) { return "B" } }
class Sub<T> : Base { fn tag(self) { return "S" } }
print(Sub().tag())
"""),

    # 35-40: async / await
    ("async_fn_basic", """
import asyncio
async fn main() { return 42 }
print(asyncio.run(main()))
"""),
    ("async_await_chain", """
import asyncio
async fn inner() { return 7 }
async fn outer() { let v = await inner() return v + 1 }
print(asyncio.run(outer()))
"""),
    ("async_gather", """
import asyncio
async fn one() { return 1 }
async fn two() { return 2 }
async fn main() {
  let results = await asyncio.gather(one(), two())
  return results
}
print(asyncio.run(main()))
"""),
    ("async_sleep_zero", """
import asyncio
async fn main() {
  await asyncio.sleep(0)
  return "ok"
}
print(asyncio.run(main()))
"""),
    ("async_class_method", """
import asyncio
class A {
  async fn run(self) { return "ar" }
}
print(asyncio.run(A().run()))
"""),
    ("async_with_loop", """
import asyncio
async fn main() {
  let total = 0
  for x in [1, 2, 3] { total = total + x }
  return total
}
print(asyncio.run(main()))
"""),

    # 41-46: with / context managers
    ("with_open_tmp", """
import tempfile
import os
let path = tempfile.mktemp()
with open(path, "w") as f { f.write("hi") }
with open(path, "r") as f { print(f.read()) }
os.remove(path)
"""),
    ("with_multi_item", """
import tempfile
import os
let p1 = tempfile.mktemp()
let p2 = tempfile.mktemp()
with open(p1, "w") as a, open(p2, "w") as b {
  a.write("A")
  b.write("B")
}
print("ok")
os.remove(p1)
os.remove(p2)
"""),
    ("with_custom_cm", """
class CM {
  fn __enter__(self) { return "in" }
  fn __exit__(self, et, ev, tb) { return False }
}
with CM() as x { print(x) }
"""),
    ("with_no_as", """
class CM {
  fn __enter__(self) { return self }
  fn __exit__(self, et, ev, tb) { return False }
}
with CM() { print("body") }
"""),
    ("with_nested", """
class CM {
  fn __init__(self, n) { self.n = n }
  fn __enter__(self) { return self.n }
  fn __exit__(self, et, ev, tb) { return False }
}
with CM(1) as a {
  with CM(2) as b {
    print(a + b)
  }
}
"""),
    ("with_async", """
import asyncio
class ACM {
  async fn __aenter__(self) { return "ax" }
  async fn __aexit__(self, et, ev, tb) { return False }
}
async fn main() {
  async with ACM() as v { return v }
}
print(asyncio.run(main()))
"""),

    # 47-55: combined / extras (>50 total)
    ("combo_class_decorator_listcomp", """
fn tag(cls) { cls.kind = "tag" return cls }
@tag
class Bag {
  fn __init__(self) { self.xs = [x * 2 for x in [1, 2, 3]] }
}
print(Bag.kind)
print(Bag().xs)
"""),
    ("combo_async_dictcomp", """
import asyncio
async fn main() {
  let d = {x: x * x for x in [1, 2, 3]}
  return d
}
print(asyncio.run(main()))
"""),
    ("combo_generic_with", """
class Holder<T> {
  fn __init__(self, v) { self.v = v }
  fn __enter__(self) { return self.v }
  fn __exit__(self, et, ev, tb) { return False }
}
with Holder(99) as v { print(v) }
"""),
    ("combo_decorator_comprehension", """
fn memo(f) {
  let cache = {}
  fn w(x) {
    if x in cache { return cache[x] }
    let v = f(x)
    cache[x] = v
    return v
  }
  return w
}
@memo
fn sq(n) { return n * n }
print([sq(i) for i in [1, 2, 3, 1, 2]])
"""),
    ("combo_class_inherit_async", """
import asyncio
class Base { async fn run(self) { return "B" } }
class Sub : Base { async fn run(self) { return "S" } }
async fn main() { return await Sub().run() }
print(asyncio.run(main()))
"""),
    ("combo_listcomp_with_call", """
fn double(x) { return x * 2 }
print([double(i) for i in [1, 2, 3, 4]])
"""),
    ("combo_dict_method_chain", """
class Reg {
  fn __init__(self) { self.d = {} }
  fn put(self, k, v) { self.d[k] = v return self }
  fn get(self, k) { return self.d[k] }
}
print(Reg().put("a", 1).put("b", 2).get("b"))
"""),
    ("combo_property_listcomp", """
class P {
  fn __init__(self) { self.xs = [i for i in [1, 2, 3, 4, 5] if i > 2] }
  @property
  fn count(self) { return len(self.xs) }
}
print(P().count)
"""),
    ("combo_all_seven", """
import asyncio
fn track(cls) { cls.tracked = True return cls }
@track
class Stream<T> {
  fn __init__(self, xs) { self.xs = [x for x in xs if x > 0] }
  fn __enter__(self) { return self }
  fn __exit__(self, et, ev, tb) { return False }
  async fn collect(self) {
    let d = {i: v for i, v in enumerate(self.xs)}
    return d
  }
}
async fn main() {
  with Stream([-1, 1, 2, -3, 4]) as s {
    return await s.collect()
  }
}
print(Stream.tracked)
print(asyncio.run(main()))
"""),
]


def main() -> int:
    os.makedirs(FIX, exist_ok=True)
    # remove any old case_NN files first
    for name in os.listdir(FIX):
        if name.startswith("case_"):
            os.remove(os.path.join(FIX, name))
    failed = []
    for i, (label, hexa) in enumerate(CASES, 1):
        slug = f"case_{i:02d}_{label}"
        hexa_path = os.path.join(FIX, slug + ".hexa")
        py_path = os.path.join(FIX, slug + ".expected.py")
        with open(hexa_path, "w") as f:
            f.write(hexa.lstrip("\n"))
        try:
            py = transpile_source(hexa.lstrip("\n"))
        except Exception as e:
            failed.append((slug, repr(e)))
            continue
        with open(py_path, "w") as f:
            f.write(py)
    print(f"generated {len(CASES)} cases under {FIX}")
    if failed:
        print("FAILED to transpile:")
        for slug, err in failed:
            print(f"  {slug}: {err}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
