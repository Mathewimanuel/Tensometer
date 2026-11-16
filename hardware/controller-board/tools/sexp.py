"""Minimal KiCad S-expression reader/writer (enough for symbol libraries and schematics)."""
import re


class Q(str):
    """A quoted string atom."""


_TOKEN = re.compile(r'\s*(?:(\()|(\))|"((?:[^"\\]|\\.)*)"|([^\s()"]+))', re.S)


def parse(text):
    stack = [[]]
    pos = 0
    n = len(text)
    while pos < n:
        m = _TOKEN.match(text, pos)
        if not m:
            if text[pos:].strip() == "":
                break
            raise ValueError(f"parse error at {pos}: {text[pos:pos+40]!r}")
        pos = m.end()
        if m.group(1):
            stack.append([])
        elif m.group(2):
            done = stack.pop()
            stack[-1].append(done)
        elif m.group(3) is not None:
            stack[-1].append(Q(m.group(3).replace('\\"', '"').replace("\\\\", "\\")))
        else:
            stack[-1].append(m.group(4))
    assert len(stack) == 1, "unbalanced parentheses"
    return stack[0]


def _atom(a):
    if isinstance(a, Q):
        return '"' + a.replace("\\", "\\\\").replace('"', '\\"') + '"'
    if isinstance(a, float):
        s = f"{a:.4f}".rstrip("0").rstrip(".")
        return "0" if s in ("-0", "") else s
    return str(a)


def dump(node, indent=0):
    if not isinstance(node, list):
        return _atom(node)
    # Short lists on one line, long ones broken per child list.
    flat = "(" + " ".join(dump(c) if not isinstance(c, list) else dump(c, 0) for c in node) + ")"
    if len(flat) <= 100 and "\n" not in flat:
        return flat
    pad = "  " * (indent + 1)
    head = [c for c in node if not isinstance(c, list)]
    out = "(" + " ".join(_atom(c) for c in head)
    for c in node:
        if isinstance(c, list):
            out += "\n" + pad + dump(c, indent + 1)
    return out + "\n" + "  " * indent + ")"


def find(node, key):
    return [c for c in node if isinstance(c, list) and c and c[0] == key]


def first(node, key):
    r = find(node, key)
    return r[0] if r else None
