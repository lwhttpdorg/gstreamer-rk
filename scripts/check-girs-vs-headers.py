#!/usr/bin/python

import argparse
import operator
import os
import sys

from dataclasses import dataclass
from pathlib import Path
from lxml import etree


GI_NAMESPACE = "http://www.gtk.org/introspection/core/1.0"
GI = "{%s}" % GI_NAMESPACE
GI_C_NAMESPACE = "http://www.gtk.org/introspection/c/1.0"
GI_C = "{%s}" % GI_C_NAMESPACE
NAMESPACES = {'x': GI_NAMESPACE, 'c': GI_C_NAMESPACE}


PTR_TYPES = ("gpointer", "GStrv", "GstClockID")
IGNORED = ("g_string_free", "g_variant_print_string", "g_variant_ref_sink", "g_thread_join")


@dataclass
class Stats:
    total_methods = 0
    is_marked_unused = 0
    not_marked_unused = 0


@dataclass(frozen=True)
class MethodInfo:
    type: str
    idendifier: str
    filename: str
    line: int
    tag: str


class Checker:
    source: Path
    gir_path: Path
    prefix: Path
    # filename      line no
    results: dict[str, dict[str, MethodInfo]]
    check_only: bool

    def __init__(self, source: Path, gir_path: Path, check_only: bool, verbose: bool, stats: Stats):
        self.source = source
        self.gir_path = gir_path
        self.check_only = check_only
        self.verbose: bool = verbose
        self.stats: Stats = stats
        self.prefix = self.find_prefix(self.gir_path)
        self.results = {}

    # Either '' or 'subprojects/name'
    @staticmethod
    def find_prefix(filename: Path) -> Path:
        for i, p in enumerate(filename.parts):
            if p == 'subprojects':
                return Path(p) / filename.parts[i + 1]

        return Path('')

    def process_gir(self):

        tree = etree.parse(self.gir_path)
        root = tree.getroot()

    #    constructors = root.xpath('//x:constructor[x:return-value[@transfer-ownership="full"]]',
    #        namespaces=NAMESPACES)
        constructors = root.xpath('//x:constructor', namespaces=NAMESPACES)
        methods = root.xpath('//x:method[x:return-value[@transfer-ownership="full"]]',
            namespaces=NAMESPACES)
        methods += constructors
        self.stats.total_methods += len(methods)

        for method in methods:
            if method.tag == GI + "method":
                tag = "method"
            else:
                tag = "constructor"

            ident = method.get(GI_C + "identifier")
            if ident.endswith('_ref') or ident in IGNORED:
                continue

            source_pos = method.find("x:source-position", namespaces=NAMESPACES)
            filename = source_pos.get("filename")
            line = source_pos.get("line")

            r_value = method.find("x:return-value/x:type", namespaces=NAMESPACES)
            r_type = ""
            if r_value is None:
                r_value = method.find("x:return-value/x:array", namespaces=NAMESPACES)
            if r_value is None:
                print(f"WARNING: Can't find return type for {ident}")
                continue

            r_type = r_value.get(GI_C + "type")
            if r_type.find("*") == -1 and r_type not in PTR_TYPES:
                print(f"WARNING: Return type {r_type} for {ident} is not a pointer!")
                continue

            self.check_method(ident, filename, int(line), r_type, tag)

    def check_method(self, ident: str, filename: str, line: int, r_type: str, tag: str):
        with open(self.source / self.prefix / filename, "r", encoding="utf-8") as header:
            unused_found = False
            text = ""

            for _ in range(0, line):
                text = header.readline()
            while True:
                if text.find("G_GNUC_WARN_UNUSED_RESULT") >= 0:
                    self.stats.is_marked_unused += 1
                    unused_found = True
                    break
                if text.find(";") >= 0:
                    break
                text = header.readline()
            if not unused_found:
                if filename not in self.results:
                    self.results[filename] = {}
                mi = MethodInfo(r_type, ident, filename, line, tag)
                if self.verbose:
                    print(f' {mi.filename:>50s}:{mi.line:<5d}  {mi.tag:15s} {mi.idendifier} -> {mi.type}')
                self.results[filename][f"{line:05d}"] = mi
                self.stats.not_marked_unused += 1

    def process_header(self, filename: str):
        src_name = self.source / self.prefix / filename
        tmp_name = src_name.with_suffix('.tmp')

        print(f"Header: {self.prefix / filename}")
        lines = sorted(self.results[filename].items(), key=operator.itemgetter(0))

        with open(src_name, "r", encoding="utf-8") as src, open(tmp_name, "w", encoding="utf-8") as dst:
            cur_line = 0
            cur_idx = 0
            mi: MethodInfo | None
            found_ident = False

            for text in src:
                try:
                    mi = lines[cur_idx][1]
                except IndexError:
                    mi = None

                cur_line = cur_line + 1

                if mi is not None and cur_line >= mi.line:
                    if not found_ident and text.find(mi.idendifier) >= 0:
                        found_ident = True

                    if found_ident and text.find(";") >= 0:
                        if self.verbose:
                            print(f'*{mi.filename:>50s}:{mi.line:<5d}  {mi.tag:15s} {mi.idendifier} -> {mi.type}')
                        text = text.replace(";", " G_GNUC_WARN_UNUSED_RESULT;", 1)
                        cur_idx = cur_idx + 1
                        found_ident = False

                dst.write(text)

        os.rename(tmp_name, src_name)

    def run(self) -> bool:
        self.process_gir()

        if not self.check_only:
            sorted_filenames = sorted(self.results.keys())
            for filename in sorted_filenames:
                self.process_header(filename)

        return len(self.results) == 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('-s', '--source', required=True, help='Source directory')
    parser.add_argument('-c', '--check-only', action='store_true', help='Only check the header files')
    parser.add_argument('-v', '--verbose', action='store_true', help='Print extra info')
    parser.add_argument('girs', nargs='*', help='List of .gir files')
    args = parser.parse_args()

    stats = Stats()

    found = False
    for gir in args.girs:
        gir_path = Path(gir)
        if not gir_path.is_file():
            print(f"WARNING: Can't find Gir file {gir_path}!")
            continue

        print(f"Gir: {gir_path}")
        checker = Checker(Path(args.source), gir_path, args.check_only, args.verbose, stats)
        if checker.run():
            found = True

    print(f"\nMethods: {stats.total_methods}")
    print(f"Already marked: {stats.is_marked_unused}")
    print(f"Should be marked: {stats.not_marked_unused}")
    return not found


if __name__ == "__main__":
    sys.exit(main())
