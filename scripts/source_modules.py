"""Read a source file with its local implementation modules expanded."""

from pathlib import Path
import re


def read_source(path):
    path = Path(path)
    source = path.read_text()
    return re.sub(
        r'^#include "([^"\n]+\.inc)"\n',
        lambda match: read_source(path.parent / match[1]),
        source,
        flags=re.MULTILINE,
    )
