#!/usr/bin/env python3

from argparse import ArgumentParser

__version__ = "1.0"


def main():

    parser = ArgumentParser(description="SFXv1 to H converter",
                            epilog="Copyright (C) 2021 Juan J Martinez <jjm@usebox.net>",
                            )

    parser.add_argument("--version", action="version",
                        version="%(prog)s " + __version__)
    parser.add_argument("file", help="file to convert")
    parser.add_argument("id", help="variable to use")

    args = parser.parse_args()

    with open(args.file, "rt") as fd:
        lines = fd.readlines()

    header = lines.pop(0)
    if header != ";SFXv1\n":
        parser.error("The file doesn't seem to be a valid SFV v1 file")

    names = []
    data = []
    for line in lines:
        b = line.strip().split(" ")
        names.append(b[0].upper())
        data.append([int(byt) & 0xff for byt in b[1:]])

    include = "%s_H_" % args.id.upper()
    print("/* file: %s */" % args.file)
    print("#ifndef %s" % include)
    print("#define %s\n" % include)

    print("#include \"beeper.h\"\n")

    print("enum %s_enum {" % args.id)
    print("\t%s_%s = 1," % (args.id.upper(), names[0].upper()))
    for n in names[1:]:
        print("\t%s_%s," % (args.id.upper(), n.upper()))
    print("};\n")

    print("#ifdef LOCAL\n")
    print("const struct beeper_sfx %s[] = {" % args.id)
    for d in data:
        print("{ %s }," % ', '.join(["0x%02x" % b for b in d]))
    print("};\n")
    print("#else")
    print("extern const struct beeper_sfx %s[];" % args.id)
    print("#endif")
    print("#endif // %s" % include)


if __name__ == "__main__":
    main()
