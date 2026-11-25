#!/usr/bin/env python3
# Builds the website into the directory given as the first argument (default: site/_build).
# Needs the "markdown" package: pip install markdown

import re
import shutil
import sys
from pathlib import Path

import markdown

SITE = Path(__file__).resolve().parent
REPO = SITE.parent
DOCS_SECTIONS = ('BIOS', 'libretro core', 'Netplay', 'Save sync', 'Wayland and X11', 'User directory',
                 'Building on Linux', 'Building on Windows')
PAGES = {
    'index.html': ('Riverhorse', 'home'),
    'docs.html': ('Documentation', 'docs'),
    'privacy.html': ('Privacy', 'privacy'),
    'licences.html': ('Licences', 'licences'),
}


def render(text: str) -> str:
    return markdown.markdown(text, extensions=['tables', 'fenced_code', 'toc'])


def readme_sections() -> str:
    sections = {}
    for block in (REPO / 'README.md').read_text(encoding='utf-8').split('\n---\n'):
        for part in re.split(r'\n(?=## )', block.strip()):
            heading = re.match(r'## (?:\S+\s)?(.+)', part)
            if heading:
                sections[heading.group(1).strip()] = re.sub(r'^## (?:\S+\s)?', '## ', part, count=1)
    return '\n\n'.join(sections[name] for name in DOCS_SECTIONS)


def page_bodies() -> dict[str, str]:
    licence_intro = ('Riverhorse is free software under the '
                     '[GNU General Public License v3.0](https://github.com/thenotoriousfox/riverhorse/blob/main/LICENSE). '
                     'The source code of every release is on '
                     '[GitHub](https://github.com/thenotoriousfox/riverhorse).\n\n')
    return {
        'index.html': (SITE / 'index.html').read_text(encoding='utf-8'),
        'docs.html': render('# Documentation\n\n' + readme_sections()),
        'privacy.html': render((SITE / 'privacy.md').read_text(encoding='utf-8')),
        'licences.html': render((REPO / 'NOTICE.md').read_text(encoding='utf-8')
                                .replace('# Notice\n\n', '# Licences\n\n' + licence_intro, 1)
                                .replace('](CONTRIBUTORS.md)',
                                         '](https://github.com/thenotoriousfox/riverhorse/blob/main/CONTRIBUTORS.md)')),
    }


def build(out: Path) -> None:
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    template = (SITE / 'template.html').read_text(encoding='utf-8')
    for name, body in page_bodies().items():
        title, page_id = PAGES[name]
        html = (template.replace('{{title}}', title if page_id == 'home' else f'{title} · Riverhorse')
                .replace('{{page}}', page_id).replace('{{body}}', body))
        (out / name).write_text(html, encoding='utf-8')
    shutil.copy(SITE / 'style.css', out / 'style.css')
    shutil.copy(REPO / 'scripts/io.github.thenotoriousfox.Riverhorse.png', out / 'riverhorse.png')
    (out / 'CNAME').write_text('riverhorse.app\n', encoding='utf-8')


if __name__ == '__main__':
    build(Path(sys.argv[1]) if len(sys.argv) > 1 else SITE / '_build')
