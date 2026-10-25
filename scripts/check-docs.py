#!/usr/bin/env python3
# Copyright (c) 2026 Rob McKay
# SPDX-License-Identifier: Apache-2.0

"""Check that every function in the repository's own code is documented.

Runs Doxygen (docs/Doxyfile) and reads its XML output. Each function needs
a Doxygen comment with a brief description, an @param for every parameter,
an @return or @retval when it returns a value, and an @sideeffects
paragraph listing the global state it changes; for C, that paragraph must
also say what happens to errno. Prints one line per problem and exits
non-zero if there are any.

A public function's interface is documented once, in its header. Doxygen
merges a declaration with its definition, so it can't check what is above
a definition; the C sources are therefore also read directly. Above every
function definition there must be a comment: for a function declared in
a header, a plain /* comment saying "Public interface: documented in
<header>." (and any implementation notes), not a second doc block; for
any other function, its doc block.

    scripts/check-docs.py
"""

import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

## Parameters that are never documented (Python's implicit first argument).
IMPLICIT_PARAMS = {"self", "cls"}


## @brief Run Doxygen on docs/Doxyfile and return its XML output directory.
#
# Doxygen makes its output directory (docs/Doxyfile's OUTPUT_DIRECTORY,
# build/doxygen) but not that directory's parent, which a fresh checkout or
# `make clean` has removed, so it is made here first. Doxygen's own
# messages are shown only if it fails.
#
# @param root Repository root.
# @return Path of the XML directory.
#
# @sideeffects Creates build/doxygen and writes Doxygen's XML output there; on failure, prints
#              Doxygen's messages and exits the process.
def run_doxygen(root):
    out_dir = os.path.join(root, "build", "doxygen")
    os.makedirs(out_dir, exist_ok=True)
    result = subprocess.run(["doxygen", "docs/Doxyfile"], cwd=root,
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.exit(f"doxygen failed (exit status {result.returncode})")
    return os.path.join(out_dir, "xml")


## @brief Concatenate the text inside an XML element.
#
# @param elem Element, or None.
# @return Its text with whitespace collapsed; "" for None.
#
# @sideeffects None.
def text_of(elem):
    if elem is None:
        return ""
    return " ".join("".join(elem.itertext()).split())


## @brief Collect the parameter names a function's documentation covers.
#
# @param member Doxygen memberdef element.
# @return Set of documented parameter names.
#
# @sideeffects None.
def documented_params(member):
    names = set()
    for plist in member.iter("parameterlist"):
        if plist.get("kind") != "param":
            continue
        for name in plist.iter("parametername"):
            names.add(text_of(name))
    return names


## @brief Tell whether a function's documentation describes its result.
#
# @param member Doxygen memberdef element.
# @return True if it has an @return section or @retval entries.
#
# @sideeffects None.
def documents_return(member):
    for sect in member.iter("simplesect"):
        if sect.get("kind") == "return":
            return True
    return any(p.get("kind") == "retval" for p in member.iter("parameterlist"))


## @brief List a function's parameter names from its declaration.
#
# @param member Doxygen memberdef element.
# @return Parameter names, without unnamed, variadic and implicit ones.
#
# @sideeffects None.
def declared_params(member):
    names = []
    for param in member.findall("param"):
        name = text_of(param.find("declname"))
        if name and name not in IMPLICIT_PARAMS and text_of(param.find("type")) != "...":
            names.append(name)
    return names


## @brief Tell whether a Python function's body returns or yields a value.
#
# @param path Source file.
# @param start First line of the body (1-based).
# @param end Last line of the body.
# @return True if any line returns an expression or yields.
#
# @sideeffects None (reads @p path).
def python_returns_value(path, start, end):
    with open(path, encoding="utf-8") as f:
        lines = f.readlines()[start - 1:end]
    return any(re.match(r"\s*(return\s+\S|yield\b)", line) for line in lines)


## @brief Tell whether a function returns a value.
#
# @param member Doxygen memberdef element.
# @param path Source file the function is defined in.
# @return True for a non-void C function or a Python function that returns.
#
# @sideeffects None (may read @p path).
def returns_value(member, path):
    if path.endswith(".py"):
        loc = member.find("location")
        start, end = loc.get("bodystart"), loc.get("bodyend")
        if not start or not end or int(end) < 0:
            return False
        return python_returns_value(path, int(start), int(end))
    rtype = text_of(member.find("type"))
    rtype = re.sub(r"\b(static|inline|extern|const)\b", "", rtype).strip()
    return rtype not in ("", "void")


## @brief Find the text of a function's @sideeffects paragraph.
#
# @param member Doxygen memberdef element.
# @return The paragraph's text, or None if the function has none.
#
# @sideeffects None.
def side_effects(member):
    for sect in member.iter("simplesect"):
        if sect.get("kind") == "par" and text_of(sect.find("title")) == "Side effects:":
            return text_of(sect.find("para"))
    return None


## @brief Check a function's @sideeffects paragraph.
#
# Every function needs one. In C it must also say what happens to errno,
# even if only that it is unchanged.
#
# @param member Doxygen memberdef element.
# @param path Source file the function is defined in.
# @return Problem description, or None.
#
# @sideeffects None.
def side_effects_problem(member, path):
    text = side_effects(member)
    if text is None:
        return "no @sideeffects paragraph"
    if not path.endswith(".py") and "errno" not in text:
        return "@sideeffects does not say what happens to errno"
    return None


## @brief Check one function's documentation.
#
# @param member Doxygen memberdef element of kind "function".
# @param root Repository root, for relative paths in messages.
# @return Problems found, as "file:line: message" strings.
#
# @sideeffects None (may read the function's source file).
def check_function(member, root):
    loc = member.find("location")
    path = loc.get("bodyfile") or loc.get("file")
    line = loc.get("bodystart") or loc.get("line")
    where = f"{os.path.relpath(path, root)}:{line}: {text_of(member.find('name'))}()"
    if not text_of(member.find("briefdescription")):
        return [f"{where}: no Doxygen comment with a brief description"]
    problems = []
    documented = documented_params(member)
    for name in declared_params(member):
        if name not in documented:
            problems.append(f"{where}: parameter '{name}' not documented")
    if returns_value(member, path) and not documents_return(member):
        problems.append(f"{where}: return value not documented")
    effects = side_effects_problem(member, path)
    if effects:
        problems.append(f"{where}: {effects}")
    return problems


## @brief Find the comment that ends on the line before a given line.
#
# @param lines The file's lines.
# @param i Index of the line the comment must be directly above.
# @return The comment's lines (a doc block starts with "/**"), or None if
#         there is no comment there.
#
# @sideeffects None.
def comment_above(lines, i):
    if i == 0 or not lines[i - 1].rstrip().endswith("*/"):
        return None
    j = i - 1
    while j > 0 and not lines[j].lstrip().startswith("/*"):
        j -= 1
    return lines[j:i]


## @brief Tell whether a comment is a Doxygen doc block.
#
# @param comment Comment lines, or None.
# @return True if it starts with "/**".
#
# @sideeffects None.
def is_doc_block(comment):
    return comment is not None and comment[0].lstrip().startswith("/**")


## @brief List the C function definitions in a file, with the comment
#         above each.
#
# A definition is a signature starting in column 0, possibly continued on
# indented lines, followed by a line holding only "{" (Zephyr's style).
#
# @param path Source file.
# @return (name, line number, comment lines or None) for each definition.
#
# @sideeffects None (reads @p path).
def c_definitions(path):
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    found = []
    for i, line in enumerate(lines):
        if line != "{" or i == 0:
            continue
        start = i - 1
        while start > 0 and lines[start].startswith(("\t", " ")):
            start -= 1
        m = re.match(r"ZTEST\((\w+), (\w+)\)|.*?\b(\w+)\(", lines[start])
        if not m:
            continue
        name = f"{m.group(1)}_{m.group(2)}" if m.group(1) else m.group(3)
        found.append((name, start + 1, comment_above(lines, start)))
    return found


## @brief List the functions a header declares with a doc block.
#
# @param path Header file.
# @return Function name to the header's file name.
#
# @sideeffects None (reads @p path).
def header_declarations(path):
    with open(path, encoding="utf-8") as f:
        lines = f.read().split("\n")
    decls = {}
    for i, line in enumerate(lines):
        m = re.match(r"[A-Za-z][^;{]*?\b(\w+)\(", line)
        if m and not line.startswith(("static", "#")) and \
                (line.rstrip().endswith(";") or lines[i + 1].startswith("\t")):
            if is_doc_block(comment_above(lines, i)):
                decls[m.group(1)] = os.path.basename(path)
    return decls


## @brief Check the comment above every C function definition.
#
# A public function (declared in a header) has a plain comment naming
# that header; any other function has its doc block.
#
# @param root Repository root.
# @return Problems found, as "file:line: message" strings.
#
# @sideeffects None (reads the C sources).
def check_sources(root):
    dirs = [os.path.join(root, "app", "src")] + \
        [os.path.join(root, "tests", t, "src") for t in os.listdir(os.path.join(root, "tests"))]
    files = sorted(os.path.join(d, n) for d in dirs if os.path.isdir(d)
                   for n in os.listdir(d) if n.endswith((".c", ".h")))
    decls = {}
    for f in files:
        if f.endswith(".h"):
            decls.update(header_declarations(f))
    problems = []
    for f in files:
        for name, line, comment in c_definitions(f):
            where = f"{os.path.relpath(f, root)}:{line}: {name}()"
            if name in decls:
                ref = f"Public interface: documented in {decls[name]}."
                if comment is None or is_doc_block(comment) or \
                        ref not in " ".join(c.strip(" */") for c in comment):
                    problems.append(f'{where}: needs a plain comment above it saying "{ref}"')
            elif not is_doc_block(comment):
                problems.append(f"{where}: definition has no doc block above it")
    return problems


## @brief Check every function in Doxygen's XML output.
#
# Each function is checked once, at whichever of its declaration and
# definition carries the documentation (Doxygen merges the two).
#
# @param xml_dir Doxygen XML directory.
# @param root Repository root.
# @return All problems found, sorted.
#
# @sideeffects None (reads Doxygen's XML files and source files).
def check_all(xml_dir, root):
    problems = set()
    seen = set()
    for name in os.listdir(xml_dir):
        if not name.endswith(".xml") or name in ("index.xml", "Doxyfile.xml"):
            continue
        for member in ET.parse(os.path.join(xml_dir, name)).iter("memberdef"):
            if member.get("kind") != "function" or member.get("id") in seen:
                continue
            seen.add(member.get("id"))
            problems.update(check_function(member, root))
    return sorted(problems)


## @brief Entry point.
#
# @return Process exit status: 0 if every function is documented.
#
# @sideeffects Runs Doxygen, which writes build/doxygen, and prints the problems found.
def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    problems = check_all(run_doxygen(root), root) + check_sources(root)
    for p in problems:
        print(p)
    print(f"{len(problems)} documentation problem(s)" if problems
          else "All functions documented")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
