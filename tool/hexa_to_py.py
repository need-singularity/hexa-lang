#!/usr/bin/env python3
"""
hexa -> Python transpiler.

Surface grammar (subset, hexa-flavored):

    fn name(args) -> Ret { body }              # def name(args): body
    return expr
    let x = expr  /  let mut x = expr           # x = expr
    [a, b, c]                                  # list literal
    {k: v}                                     # dict literal
    {x for x in xs}                            # set comprehension (or dict if has :)
    [x for x in xs]                            # list comprehension
    {k: v for k, v in xs}                      # dict comprehension
    class Name<T> : Base { ... }               # class with generics + base
    class Name { fn method(self, x) {...} }    # methods
    @decorator                                 # decorator
    @decorator(args)
    async fn name() { ... }                    # async def
    await expr
    with ctx as name { body }                  # with statement
    with ctx { body }
    if cond { } else if cond2 { } else { }     # if/elif/else
    while cond { }
    for x in xs { }
    print(...)
    pass

Identifier-level grammar features added beyond the original (def/return/list/dict):
    1. class
    2. decorator
    3. type generics (parsed and stripped)
    4. async / await
    5. context manager (with)
    6. list/dict/set comprehension
    (plus required scaffolding: if/while/for/let/method calls/attribute access)

The transpiler is a hand-written recursive-descent parser over a small token stream.
It emits Python source with 4-space indentation, then validates by parsing with
Python's `ast` module.
"""

from __future__ import annotations

import re
import sys
import os
import ast
from typing import List, Optional, Tuple

# ----------------------------------------------------------------------------
# Tokenizer
# ----------------------------------------------------------------------------

KEYWORDS = {
    "fn", "return", "let", "mut", "class", "if", "else", "while", "for",
    "in", "async", "await", "with", "as", "true", "false", "null", "None",
    "True", "False", "and", "or", "not", "is", "pass", "import", "from",
    "yield", "lambda", "raise", "try", "except", "finally", "break",
    "continue", "self",
}

TOKEN_RE = re.compile(
    r"""
    (?P<NEWLINE>\n) |
    (?P<WS>[ \t\r]+) |
    (?P<COMMENT>//[^\n]*) |
    (?P<STRING>"(?:\\.|[^"\\])*") |
    (?P<FSTRING>f"(?:\\.|[^"\\])*") |
    (?P<FLOAT>\d+\.\d+) |
    (?P<INT>\d+) |
    (?P<ARROW>->) |
    (?P<FATARROW>=>) |
    (?P<EQEQ>==) |
    (?P<NEQ>!=) |
    (?P<LE>\<=) |
    (?P<GE>\>=) |
    (?P<AND>&&) |
    (?P<OR>\|\|) |
    (?P<NOT>!) |
    (?P<IDENT>[A-Za-z_][A-Za-z_0-9]*) |
    (?P<OP>[+\-*/%<>=:.,;()\[\]{}@&|^])
    """,
    re.VERBOSE,
)


class Token:
    __slots__ = ("kind", "value", "line")

    def __init__(self, kind: str, value: str, line: int):
        self.kind = kind
        self.value = value
        self.line = line

    def __repr__(self):
        return f"Token({self.kind!r},{self.value!r},L{self.line})"


def tokenize(src: str) -> List[Token]:
    tokens: List[Token] = []
    line = 1
    i = 0
    while i < len(src):
        m = TOKEN_RE.match(src, i)
        if not m:
            raise SyntaxError(f"unexpected char {src[i]!r} at line {line}")
        kind = m.lastgroup
        value = m.group()
        i = m.end()
        if kind == "NEWLINE":
            line += 1
            continue
        if kind in ("WS", "COMMENT"):
            continue
        if kind == "IDENT" and value in KEYWORDS:
            kind = value.upper()  # treat keyword as own token kind
        tokens.append(Token(kind, value, line))
    tokens.append(Token("EOF", "", line))
    return tokens


# ----------------------------------------------------------------------------
# Parser / Codegen (single-pass: emits Python directly)
# ----------------------------------------------------------------------------

class Transpiler:
    def __init__(self, tokens: List[Token]):
        self.toks = tokens
        self.pos = 0

    # --- token helpers ----------------------------------------------------

    def peek(self, off: int = 0) -> Token:
        return self.toks[self.pos + off]

    def eat(self, *kinds_or_values) -> Token:
        t = self.peek()
        if t.kind in kinds_or_values or t.value in kinds_or_values:
            self.pos += 1
            return t
        raise SyntaxError(
            f"expected {kinds_or_values} got {t.kind}({t.value!r}) at line {t.line}"
        )

    def accept(self, *kinds_or_values) -> Optional[Token]:
        t = self.peek()
        if t.kind in kinds_or_values or t.value in kinds_or_values:
            self.pos += 1
            return t
        return None

    # --- top level --------------------------------------------------------

    def transpile(self) -> str:
        out: List[str] = []
        while self.peek().kind != "EOF":
            self.parse_top(out, indent=0)
        if not out:
            out.append("pass")
        return "\n".join(out) + "\n"

    # ---- statements ------------------------------------------------------

    def parse_top(self, out: List[str], indent: int):
        # collect decorators
        decos: List[str] = []
        while self.peek().value == "@":
            self.pos += 1
            decos.append("@" + self.parse_expr())
        # async fn / fn / class / async / with / let / if / while / for / return / pass / expr
        t = self.peek()
        if t.kind == "ASYNC" and self.peek(1).kind == "FN":
            self.pos += 1  # async
            self.parse_fn(out, indent, decos, is_async=True)
            return
        if t.kind == "FN":
            self.parse_fn(out, indent, decos)
            return
        if t.kind == "CLASS":
            self.parse_class(out, indent, decos)
            return
        if decos:
            raise SyntaxError(
                f"decorators must precede fn or class at line {t.line}"
            )
        if t.kind == "ASYNC" and self.peek(1).kind == "WITH":
            self.pos += 1  # async
            self.parse_with(out, indent, is_async=True)
            return
        if t.kind == "ASYNC" and self.peek(1).kind == "FOR":
            self.pos += 1
            self.parse_for(out, indent, is_async=True)
            return
        if t.kind == "WITH":
            self.parse_with(out, indent)
            return
        if t.kind == "LET":
            self.parse_let(out, indent)
            return
        if t.kind == "IF":
            self.parse_if(out, indent)
            return
        if t.kind == "WHILE":
            self.parse_while(out, indent)
            return
        if t.kind == "FOR":
            self.parse_for(out, indent)
            return
        if t.kind == "RETURN":
            self.pos += 1
            if self.peek().kind in ("EOF",) or self.peek().value in (";", "}"):
                out.append(" " * indent + "return")
            else:
                out.append(" " * indent + "return " + self.parse_expr())
            self.accept(";")
            return
        if t.kind == "PASS":
            self.pos += 1
            out.append(" " * indent + "pass")
            self.accept(";")
            return
        if t.kind == "BREAK":
            self.pos += 1
            out.append(" " * indent + "break")
            self.accept(";")
            return
        if t.kind == "CONTINUE":
            self.pos += 1
            out.append(" " * indent + "continue")
            self.accept(";")
            return
        if t.kind == "RAISE":
            self.pos += 1
            out.append(" " * indent + "raise " + self.parse_expr())
            self.accept(";")
            return
        if t.kind == "IMPORT":
            self.pos += 1
            name = self.parse_dotted_name()
            out.append(" " * indent + "import " + name)
            self.accept(";")
            return
        if t.kind == "FROM":
            self.pos += 1
            mod = self.parse_dotted_name()
            self.eat("IMPORT")
            names = [self.eat("IDENT").value]
            while self.accept(","):
                names.append(self.eat("IDENT").value)
            out.append(" " * indent + f"from {mod} import " + ", ".join(names))
            self.accept(";")
            return
        # expression statement (incl assignment)
        expr = self.parse_expr_stmt()
        out.append(" " * indent + expr)
        self.accept(";")

    def parse_dotted_name(self) -> str:
        parts = [self.eat("IDENT").value]
        while self.accept("."):
            parts.append(self.eat("IDENT").value)
        return ".".join(parts)

    # ---- fn --------------------------------------------------------------

    def parse_fn(self, out: List[str], indent: int, decos: List[str],
                 is_async: bool = False):
        self.eat("FN")
        # optional generics: <T, U>
        self.skip_generics()
        name = self.eat("IDENT").value
        self.skip_generics()
        self.eat("(")
        params: List[str] = []
        if self.peek().value != ")":
            params.append(self.parse_param())
            while self.accept(","):
                if self.peek().value == ")":
                    break
                params.append(self.parse_param())
        self.eat(")")
        # optional -> ReturnType
        if self.accept("ARROW"):
            self.skip_type()
        self.eat("{")
        for d in decos:
            out.append(" " * indent + d)
        kw = "async def" if is_async else "def"
        out.append(" " * indent + f"{kw} {name}({', '.join(params)}):")
        self.parse_block(out, indent + 4)
        self.eat("}")

    def parse_param(self) -> str:
        # name (: Type)? (= default)?
        if self.peek().kind == "SELF":
            self.pos += 1
            return "self"
        # accept *args / **kwargs syntax
        prefix = ""
        if self.peek().value == "*":
            self.pos += 1
            prefix = "*"
            if self.peek().value == "*":
                self.pos += 1
                prefix = "**"
        name = self.eat("IDENT").value
        if self.accept(":"):
            self.skip_type()
        default = ""
        if self.accept("="):
            default = "=" + self.parse_expr()
        return prefix + name + default

    # ---- class -----------------------------------------------------------

    def parse_class(self, out: List[str], indent: int, decos: List[str]):
        self.eat("CLASS")
        name = self.eat("IDENT").value
        self.skip_generics()
        bases: List[str] = []
        if self.accept(":"):
            bases.append(self.parse_expr_no_block())
            while self.accept(","):
                bases.append(self.parse_expr_no_block())
        self.eat("{")
        for d in decos:
            out.append(" " * indent + d)
        if bases:
            out.append(" " * indent + f"class {name}({', '.join(bases)}):")
        else:
            out.append(" " * indent + f"class {name}:")
        body_start = len(out)
        while self.peek().value != "}":
            self.parse_top(out, indent + 4)
        self.eat("}")
        if len(out) == body_start:
            out.append(" " * (indent + 4) + "pass")

    # ---- generics / types -----------------------------------------------

    def skip_generics(self):
        # <T> or <T, U: Bound>
        if self.peek().value != "<":
            return
        # only treat as generics if it looks like an identifier follows
        save = self.pos
        self.pos += 1
        depth = 1
        while depth > 0 and self.peek().kind != "EOF":
            v = self.peek().value
            if v == "<":
                depth += 1
            elif v == ">":
                depth -= 1
            self.pos += 1
        if depth != 0:
            self.pos = save  # not generics, rewind

    def skip_type(self):
        # consume one type expression: Ident (. Ident)* (<...>)? optionally followed by | T (union)
        self.parse_one_type()
        while self.peek().value in ("|",):
            self.pos += 1
            self.parse_one_type()

    def parse_one_type(self):
        # primitive: Ident or [T] or {K:V} or (T,T) or fn(...) -> T
        if self.peek().value == "[":
            self.pos += 1
            self.parse_one_type()
            self.eat("]")
            return
        if self.peek().value == "{":
            self.pos += 1
            self.parse_one_type()
            if self.accept(":"):
                self.parse_one_type()
            self.eat("}")
            return
        if self.peek().value == "(":
            self.pos += 1
            if self.peek().value != ")":
                self.parse_one_type()
                while self.accept(","):
                    self.parse_one_type()
            self.eat(")")
            if self.accept("ARROW"):
                self.parse_one_type()
            return
        if self.peek().kind == "FN":
            self.pos += 1
            self.eat("(")
            if self.peek().value != ")":
                self.parse_one_type()
                while self.accept(","):
                    self.parse_one_type()
            self.eat(")")
            if self.accept("ARROW"):
                self.parse_one_type()
            return
        # bare ident path
        self.eat("IDENT")
        while self.accept("."):
            self.eat("IDENT")
        self.skip_generics()

    # ---- block -----------------------------------------------------------

    def parse_block(self, out: List[str], indent: int):
        body_start = len(out)
        while self.peek().value != "}":
            if self.peek().kind == "EOF":
                raise SyntaxError("unexpected EOF in block")
            self.parse_top(out, indent)
        if len(out) == body_start:
            out.append(" " * indent + "pass")

    # ---- if/while/for/with/let ------------------------------------------

    def parse_if(self, out: List[str], indent: int):
        self.eat("IF")
        cond = self.parse_expr_no_block()
        self.eat("{")
        out.append(" " * indent + f"if {cond}:")
        self.parse_block(out, indent + 4)
        self.eat("}")
        while self.peek().kind == "ELSE":
            self.pos += 1
            if self.peek().kind == "IF":
                self.pos += 1
                cond = self.parse_expr_no_block()
                self.eat("{")
                out.append(" " * indent + f"elif {cond}:")
                self.parse_block(out, indent + 4)
                self.eat("}")
            else:
                self.eat("{")
                out.append(" " * indent + "else:")
                self.parse_block(out, indent + 4)
                self.eat("}")
                break

    def parse_while(self, out: List[str], indent: int):
        self.eat("WHILE")
        cond = self.parse_expr_no_block()
        self.eat("{")
        out.append(" " * indent + f"while {cond}:")
        self.parse_block(out, indent + 4)
        self.eat("}")

    def parse_for(self, out: List[str], indent: int, is_async: bool = False):
        self.eat("FOR")
        # for x in iter or for (x, y) in iter
        target = self.parse_for_target()
        self.eat("IN")
        it = self.parse_expr_no_block()
        self.eat("{")
        kw = "async for" if is_async else "for"
        out.append(" " * indent + f"{kw} {target} in {it}:")
        self.parse_block(out, indent + 4)
        self.eat("}")

    def parse_for_target(self) -> str:
        if self.accept("("):
            names = [self.eat("IDENT").value]
            while self.accept(","):
                names.append(self.eat("IDENT").value)
            self.eat(")")
            return ", ".join(names)
        names = [self.eat("IDENT").value]
        while self.accept(","):
            names.append(self.eat("IDENT").value)
        return ", ".join(names) if len(names) > 1 else names[0]

    def parse_with(self, out: List[str], indent: int, is_async: bool = False):
        self.eat("WITH")
        items = [self.parse_with_item()]
        while self.accept(","):
            items.append(self.parse_with_item())
        self.eat("{")
        kw = "async with" if is_async else "with"
        out.append(" " * indent + f"{kw} {', '.join(items)}:")
        self.parse_block(out, indent + 4)
        self.eat("}")

    def parse_with_item(self) -> str:
        e = self.parse_expr_no_block()
        if self.accept("AS"):
            name = self.eat("IDENT").value
            return f"{e} as {name}"
        return e

    def parse_let(self, out: List[str], indent: int):
        self.eat("LET")
        self.accept("MUT")
        # optionally tuple destructure: (a, b) =
        if self.accept("("):
            names = [self.eat("IDENT").value]
            while self.accept(","):
                names.append(self.eat("IDENT").value)
            self.eat(")")
            target = ", ".join(names)
        else:
            target = self.eat("IDENT").value
            if self.accept(":"):
                self.skip_type()
        self.eat("=")
        rhs = self.parse_expr()
        out.append(" " * indent + f"{target} = {rhs}")
        self.accept(";")

    # ---- expressions -----------------------------------------------------

    def parse_expr_stmt(self) -> str:
        # expression possibly followed by = / += / -= / *= / /=
        lhs = self.parse_expr()
        if self.peek().value in ("=",):
            self.pos += 1
            rhs = self.parse_expr()
            return f"{lhs} = {rhs}"
        for op in ("+=", "-=", "*=", "/="):
            # composite ops not in tokenizer; handle as op then '='
            pass
        # composite assigns: + =, - =, etc -> not produced; user writes x = x + 1
        return lhs

    # ----- expressions are translated mostly verbatim -----
    # Precedence climber over our token stream.

    def parse_expr(self) -> str:
        return self.parse_or()

    def parse_expr_no_block(self) -> str:
        # used in if/while/for-iter — same as parse_expr; '{' terminates.
        return self.parse_or()

    def parse_or(self) -> str:
        left = self.parse_and()
        while self.peek().kind == "OR" or self.peek().value == "||":
            self.pos += 1
            right = self.parse_and()
            left = f"({left} or {right})"
        return left

    def parse_and(self) -> str:
        left = self.parse_not()
        while self.peek().kind == "AND" or self.peek().value == "&&":
            self.pos += 1
            right = self.parse_not()
            left = f"({left} and {right})"
        return left

    def parse_not(self) -> str:
        if self.peek().kind == "NOT" or self.peek().value == "!":
            self.pos += 1
            return f"(not {self.parse_not()})"
        return self.parse_cmp()

    def parse_cmp(self) -> str:
        left = self.parse_add()
        while True:
            t = self.peek()
            op = None
            if t.kind == "EQEQ":
                op = "=="
            elif t.kind == "NEQ":
                op = "!="
            elif t.kind == "LE":
                op = "<="
            elif t.kind == "GE":
                op = ">="
            elif t.value == "<":
                op = "<"
            elif t.value == ">":
                op = ">"
            elif t.kind == "IN":
                op = "in"
            elif t.kind == "IS":
                op = "is"
            else:
                break
            self.pos += 1
            # support `not in` / `is not`
            if op == "is" and self.peek().kind == "NOT":
                self.pos += 1
                op = "is not"
            right = self.parse_add()
            left = f"({left} {op} {right})"
        return left

    def parse_add(self) -> str:
        left = self.parse_mul()
        while self.peek().value in ("+", "-"):
            op = self.peek().value
            self.pos += 1
            right = self.parse_mul()
            left = f"({left} {op} {right})"
        return left

    def parse_mul(self) -> str:
        left = self.parse_unary()
        while self.peek().value in ("*", "/", "%"):
            op = self.peek().value
            self.pos += 1
            right = self.parse_unary()
            left = f"({left} {op} {right})"
        return left

    def parse_unary(self) -> str:
        if self.peek().value in ("-", "+"):
            op = self.peek().value
            self.pos += 1
            inner = self.parse_unary()
            return f"({op}{inner})"
        if self.peek().kind == "AWAIT":
            self.pos += 1
            inner = self.parse_unary()
            return f"(await {inner})"
        return self.parse_postfix()

    def parse_postfix(self) -> str:
        e = self.parse_atom()
        while True:
            t = self.peek()
            if t.value == "(":
                self.pos += 1
                args: List[str] = []
                if self.peek().value != ")":
                    args.append(self.parse_call_arg())
                    while self.accept(","):
                        if self.peek().value == ")":
                            break
                        args.append(self.parse_call_arg())
                self.eat(")")
                e = f"{e}({', '.join(args)})"
            elif t.value == "[":
                self.pos += 1
                idx = self.parse_slice_or_expr()
                self.eat("]")
                e = f"{e}[{idx}]"
            elif t.value == ".":
                self.pos += 1
                name = self.eat("IDENT").value
                e = f"{e}.{name}"
            else:
                break
        return e

    def parse_call_arg(self) -> str:
        # ident = expr  → keyword arg
        if self.peek().kind == "IDENT" and self.peek(1).value == "=":
            n = self.eat("IDENT").value
            self.eat("=")
            v = self.parse_expr()
            return f"{n}={v}"
        # *args / **kwargs
        if self.peek().value == "*":
            self.pos += 1
            if self.peek().value == "*":
                self.pos += 1
                return "**" + self.parse_expr()
            return "*" + self.parse_expr()
        return self.parse_expr()

    def parse_slice_or_expr(self) -> str:
        # support a:b:c slices
        parts: List[str] = []
        cur = ""
        if self.peek().value != ":":
            cur = self.parse_expr()
        parts.append(cur)
        while self.accept(":"):
            cur = ""
            if self.peek().value not in (":", "]"):
                cur = self.parse_expr()
            parts.append(cur)
        if len(parts) == 1:
            return parts[0]
        return ":".join(parts)

    def parse_atom(self) -> str:
        t = self.peek()
        if t.kind == "INT":
            self.pos += 1
            return t.value
        if t.kind == "FLOAT":
            self.pos += 1
            return t.value
        if t.kind == "STRING":
            self.pos += 1
            return t.value
        if t.kind == "FSTRING":
            self.pos += 1
            return t.value
        if t.kind == "TRUE":
            self.pos += 1
            return "True"
        if t.kind == "FALSE":
            self.pos += 1
            return "False"
        if t.kind == "NULL" or t.kind == "NONE":
            self.pos += 1
            return "None"
        if t.kind == "TRUE_":
            self.pos += 1
            return "True"
        if t.kind == "SELF":
            self.pos += 1
            return "self"
        if t.kind == "LAMBDA":
            self.pos += 1
            params: List[str] = []
            if self.peek().value != ":":
                params.append(self.eat("IDENT").value)
                while self.accept(","):
                    params.append(self.eat("IDENT").value)
            self.eat(":")
            body = self.parse_expr()
            return f"(lambda {', '.join(params)}: {body})"
        if t.value == "(":
            self.pos += 1
            # tuple or grouped expr
            if self.peek().value == ")":
                self.pos += 1
                return "()"
            first = self.parse_expr()
            if self.accept(","):
                items = [first]
                if self.peek().value != ")":
                    items.append(self.parse_expr())
                    while self.accept(","):
                        if self.peek().value == ")":
                            break
                        items.append(self.parse_expr())
                self.eat(")")
                return "(" + ", ".join(items) + ",)" if len(items) == 1 else "(" + ", ".join(items) + ")"
            self.eat(")")
            return f"({first})"
        if t.value == "[":
            self.pos += 1
            return self.parse_list_or_listcomp()
        if t.value == "{":
            self.pos += 1
            return self.parse_dict_or_set_or_comp()
        if t.kind == "IDENT":
            self.pos += 1
            return t.value
        raise SyntaxError(f"unexpected token {t.kind}({t.value!r}) at line {t.line}")

    def parse_list_or_listcomp(self) -> str:
        # already consumed '['
        if self.peek().value == "]":
            self.pos += 1
            return "[]"
        first = self.parse_expr()
        if self.peek().kind == "FOR":
            comp = self.parse_comp_clauses()
            self.eat("]")
            return f"[{first}{comp}]"
        items = [first]
        while self.accept(","):
            if self.peek().value == "]":
                break
            items.append(self.parse_expr())
        self.eat("]")
        return "[" + ", ".join(items) + "]"

    def parse_dict_or_set_or_comp(self) -> str:
        # already consumed '{'
        if self.peek().value == "}":
            self.pos += 1
            return "{}"
        first = self.parse_expr()
        if self.accept(":"):
            v = self.parse_expr()
            if self.peek().kind == "FOR":
                comp = self.parse_comp_clauses()
                self.eat("}")
                return f"{{{first}: {v}{comp}}}"
            items = [f"{first}: {v}"]
            while self.accept(","):
                if self.peek().value == "}":
                    break
                k = self.parse_expr()
                self.eat(":")
                v = self.parse_expr()
                items.append(f"{k}: {v}")
            self.eat("}")
            return "{" + ", ".join(items) + "}"
        # set / set comp
        if self.peek().kind == "FOR":
            comp = self.parse_comp_clauses()
            self.eat("}")
            return f"{{{first}{comp}}}"
        items = [first]
        while self.accept(","):
            if self.peek().value == "}":
                break
            items.append(self.parse_expr())
        self.eat("}")
        return "{" + ", ".join(items) + "}"

    def parse_comp_clauses(self) -> str:
        out = ""
        while self.peek().kind == "FOR":
            self.pos += 1
            target = self.parse_for_target()
            self.eat("IN")
            it = self.parse_or()  # avoid greedy comparison capture? parse_or is fine here
            out += f" for {target} in {it}"
            while self.peek().kind == "IF":
                self.pos += 1
                cond = self.parse_or()
                out += f" if {cond}"
        return out


# ----------------------------------------------------------------------------
# Public API
# ----------------------------------------------------------------------------

def transpile_source(src: str) -> str:
    toks = tokenize(src)
    py = Transpiler(toks).transpile()
    # validate by parsing
    ast.parse(py)
    return py


def main(argv: List[str]) -> int:
    if len(argv) < 2:
        print("usage: hexa_to_py.py <input.hexa> [output.py]", file=sys.stderr)
        return 2
    src = open(argv[1]).read()
    py = transpile_source(src)
    if len(argv) >= 3:
        with open(argv[2], "w") as f:
            f.write(py)
    else:
        sys.stdout.write(py)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
