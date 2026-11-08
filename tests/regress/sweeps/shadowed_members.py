#!/usr/bin/env python3
"""Static check: data members (m_*) that a class declares again although a base class has them.

A redeclared member shadows the base one: code in the base class reads its own (often empty)
copy while the derived class fills the other. BH-005 was this (ResistorDip::m_pin hid
Component::m_pin, so getPins() was empty). Headers are parsed crudely (class Name : public A,
public B { ... m_x; }), which is enough for this code base.

Usage: python3 tests/regress/sweeps/shadowed_members.py [--all]
Prints one SHADOW line per finding; known intentional ones are listed in KNOWN.
Exit code 1 when there are unknown findings. Debug lines are prefixed [shadow].
"""
import glob
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
KNOWN = set()   # ("Class", "m_member") pairs reviewed and kept on purpose

CLASS = re.compile(r"\bclass\s+(?:\w+\s+)?(\w+)\s*(?::\s*([^{;]+))?\{", re.S)
MEMBER = re.compile(r"^\s*(?:static\s+|mutable\s+|const\s+)*[\w:<>,\s\*&]+?[\s\*&](m_\w+)\s*(?:\[[^\]]*\])?\s*(?:=[^;]*)?;", re.M)


def body(text, start):
    depth, i = 0, start
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1:i]
        i += 1
    return ""


def main():
    classes = {}
    for h in glob.glob(os.path.join(ROOT, "src", "**", "*.h"), recursive=True):
        if "/angel/" in h:
            continue
        text = re.sub(r"//[^\n]*|/\*.*?\*/", "", open(h, errors="replace").read(), flags=re.S)
        for m in CLASS.finditer(text):
            name, bases = m.group(1), m.group(2) or ""
            b = body(text, m.end() - 1)
            b = re.sub(r"\{[^{}]*\}", ";", b)           # drop inline function bodies (one level)
            members = set(MEMBER.findall(b))
            base_names = [re.sub(r"<.*", "", x.split()[-1]) for x in bases.split(",") if x.strip()]
            classes[name] = (base_names, members, os.path.relpath(h, ROOT))
    print("[shadow] %d classes parsed" % len(classes), flush=True)

    def ancestors(c, seen=None):
        seen = seen if seen is not None else set()
        for b in classes.get(c, ([], set(), ""))[0]:
            if b in classes and b not in seen:
                seen.add(b)
                ancestors(b, seen)
        return seen

    found = 0
    for c, (bases, members, path) in sorted(classes.items()):
        for a in sorted(ancestors(c)):
            for m in sorted(members & classes[a][1]):
                if (c, m) in KNOWN and "--all" not in sys.argv:
                    continue
                found += 1
                print("SHADOW %s::%s hides %s::%s  (%s, %s)" % (c, m, a, m, path, classes[a][2]))
    print("[shadow] %d finding(s)" % found)
    sys.exit(1 if found else 0)


if __name__ == "__main__":
    main()
