"""Compare all user-facing translation sources with the shipped catalogs."""
import pathlib
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

root = pathlib.Path(__file__).resolve().parents[1]


def messages(path):
    tree = ET.parse(path)
    return {(context.findtext("name"), message.findtext("source"))
            for context in tree.findall("context")
            for message in context.findall("message")}


with tempfile.TemporaryDirectory() as directory:
    extracted = pathlib.Path(directory) / "source.ts"
    subprocess.run([sys.argv[1], "src/ui", "src/core", "src/scriptengine",
                    "src/main.cpp", "third_party/qtermwidget", "-no-obsolete",
                    "-locations", "none", "-ts", str(extracted)],
                   cwd=root, check=True, capture_output=True, text=True)
    expected = messages(extracted)
    for language in ("en", "zh_CN", "zh_TW"):
        path = root / "src/resources/i18n" / f"qshell_{language}.ts"
        actual = messages(path)
        assert actual == expected, (language, "missing", expected - actual,
                                    "obsolete", actual - expected)
    print(f"All three catalogs cover {len(expected)} source messages")
