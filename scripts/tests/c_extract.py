# SPDX-License-Identifier: GPL-2.0
"""Shared C-function extractor for the host-side NUMA regression scripts.

Each regression script compiles the actual functions from this checkout with
a synthetic harness. This module provides the single extractor they all share:
it locates a function definition by name and returns its exact source span,
ignoring braces that appear inside comments or string/char literals.
"""

from __future__ import annotations

import re

# Comments and string/char literals, so braces inside them are ignored. The
# replacement blanks the match in place (keeping newlines) so the offsets and
# line numbers of the surrounding source are preserved.
_MASK = re.compile(
    r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S
)

# A definition line: optional whitespace/return type (no '(' so a call or an
# 'if (...)' can never be mistaken for the definition), the name, the
# parameter list and the opening brace. A forward declaration ends in ';' and
# therefore never matches.
_BRACES = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|[{}]', re.S)


def _definition_pattern(name: str) -> str:
    return r'^[^\n;{}(]*\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{'


def block(source: str, start: int) -> str:
    """Return the brace-delimited block whose first '{' is at/after `start`."""
    opening = source.index("{", start)
    depth = 0
    for token in _BRACES.finditer(source, opening):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return source[start:token.end()]
    raise RuntimeError("Unterminated source block")


def extract_function(source, name, *, path=None, optional=False, line_directive=False):
    """Return the definition of the C function called `name`.

    `source` is the file text; `path` is only used for diagnostics and for the
    optional `#line` directive. When `optional` is true a missing function
    yields an empty string instead of raising. With `line_directive`, the
    returned text is prefixed with a `#line` directive so compiler
    diagnostics point back at the original file and line.
    """
    masked = _MASK.sub(lambda match: re.sub(r'[^\n]', ' ', match.group()), source)
    match = re.search(_definition_pattern(name), masked, flags=re.M)
    if not match:
        if optional:
            return ''
        raise ValueError(f'{path or "<source>"}: cannot find function {name}')
    end, depth = match.end(), 1
    while depth:
        depth += (masked[end] == '{') - (masked[end] == '}')
        end += 1
    if not line_directive:
        return source[match.start():end]
    line = source.count('\n', 0, match.start()) + 1
    return f'\n#line {line} "{path}"\n{source[match.start():end]}\n'
