"""Small JSONC reader for data tools.

It removes // and /* */ comments without touching comment markers inside
JSON strings, then delegates syntax validation to the standard json module.
"""
import json


def loads(text):
    plain = []
    in_string = False
    escaped = False
    line_comment = False
    block_comment = False
    i = 0
    while i < len(text):
        c = text[i]
        if line_comment:
            if c == "\n":
                line_comment = False
                plain.append(c)
            else:
                plain.append(" ")
        elif block_comment:
            if c == "*" and i + 1 < len(text) and text[i + 1] == "/":
                block_comment = False
                plain.extend((" ", " "))
                i += 1
            else:
                plain.append("\n" if c == "\n" else " ")
        elif in_string:
            plain.append(c)
            if escaped:
                escaped = False
            elif c == "\\":
                escaped = True
            elif c == '"':
                in_string = False
        elif c == '"':
            in_string = True
            plain.append(c)
        elif c == "/" and i + 1 < len(text) and text[i + 1] == "/":
            line_comment = True
            plain.extend((" ", " "))
            i += 1
        elif c == "/" and i + 1 < len(text) and text[i + 1] == "*":
            block_comment = True
            plain.extend((" ", " "))
            i += 1
        else:
            plain.append(c)
        i += 1
    if line_comment or block_comment:
        raise ValueError("unterminated JSONC comment")
    return json.loads("".join(plain))


def load(path):
    with open(path, encoding="utf-8") as stream:
        return loads(stream.read())
