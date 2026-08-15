"""Split a PDDL problem's conjunctive goal into single-atom goal variants.

Pure text/s-expression manipulation: we locate the top-level ``(:goal ...)``
block, read its conjuncts, and re-emit the problem file with the goal replaced
by exactly one conjunct.  Everything else in the file is byte-identical, so the
resulting problems differ from the original only in the goal.
"""

from __future__ import annotations


def strip_comments(text: str) -> str:
    out = []
    for line in text.splitlines(keepends=True):
        idx = line.find(";")
        out.append(line if idx < 0 else line[:idx] + "\n")
    return "".join(out)


def _match_paren(text: str, start: int) -> int:
    """Index just past the ')' closing the '(' at ``start``."""
    assert text[start] == "("
    depth = 0
    for i in range(start, len(text)):
        c = text[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i + 1
    raise ValueError("unbalanced parentheses")


def find_goal_block(text: str) -> tuple[int, int]:
    """Return (start, end) character offsets of the ``(:goal ...)`` s-expression."""
    lowered = text.lower()
    cursor = 0
    while True:
        idx = lowered.find("(:goal", cursor)
        if idx < 0:
            raise ValueError("no (:goal ...) block found")
        # Ensure ':goal' is a whole token, not a prefix of e.g. ':goalx'.
        nxt = lowered[idx + len("(:goal")]
        if nxt.isspace() or nxt == "(":
            return idx, _match_paren(text, idx)
        cursor = idx + 1


def _tokenize(sexp: str):
    return sexp.replace("(", " ( ").replace(")", " ) ").split()


def _parse(tokens, pos=0):
    if tokens[pos] != "(":
        return tokens[pos], pos + 1
    out = []
    pos += 1
    while tokens[pos] != ")":
        node, pos = _parse(tokens, pos)
        out.append(node)
    return out, pos + 1


def _unparse(node) -> str:
    if isinstance(node, str):
        return node
    return "(" + " ".join(_unparse(c) for c in node) + ")"


def goal_conjuncts(problem_text: str) -> list[str]:
    """Return the goal's conjuncts as normalised s-expression strings."""
    text = strip_comments(problem_text)
    start, end = find_goal_block(text)
    tree, _ = _parse(_tokenize(text[start:end]))
    # tree == [':goal', <formula>]
    body = tree[1:]
    if len(body) != 1:
        raise ValueError(f"unexpected (:goal ...) arity: {len(body)}")
    formula = body[0]
    if isinstance(formula, list) and formula and isinstance(formula[0], str) and formula[0].lower() == "and":
        conjuncts = formula[1:]
    else:
        conjuncts = [formula]
    return [_unparse(c) for c in conjuncts]


def with_single_goal(problem_text: str, conjunct: str) -> str:
    """Return the problem text with its goal replaced by ``(:goal (and <conjunct>))``."""
    text = strip_comments(problem_text)
    start, end = find_goal_block(text)
    return text[:start] + f"(:goal (and {conjunct}))" + text[end:]


if __name__ == "__main__":
    import sys

    src = open(sys.argv[1]).read()
    for i, c in enumerate(goal_conjuncts(src)):
        print(i, c)
