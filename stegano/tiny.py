#!/usr/bin/env python
# -*- coding: UTF-8 -*-
import math
from tinyscript import *


__author__    = "Alexandre D'Hondt"
__version__   = "1.5"
__copyright__ = ("A. D'Hondt", 2019)
__license__   = "gpl-3.0"
__reference__ = "https://inshallhack.org/paddinganography/"
__examples__  = ["-s . -f \"Comment\" < image.jpg > base32.enc", "-e base32 < base32.enc"]
__docformat__ = "md"
__doc__       = """
*Paddinganograph* allows to unhide data hidden in base32/base64 strings. It can take a PNG or JPG in input to retrieve an EXIF value as the input data.
"""


SCRIPTNAME_FORMAT = "none"

DEF_BASE64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
DEF_BASE32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"


def exif(raw_data, key):
    t = ts.TempPath().tempfile().name
    with open(str(t), 'wb') as f:
        f.write(raw_data)
    logger.debug("Getting EXIF data...")
    exif = subprocess.check_output(["exiftool", str(t)])
    exif = codecs.decode(exif, "utf-8")
    exif = {l.split(':', 1)[0].strip(): l.split(':', 1)[1].strip() for l in exif.split('\n') if l.strip() != ""}
    return exif if not key else exif[key]


def unhide(encoded, encoding="base64", charset=None, sep=".", pad="=", n_pad=8):
    try:
        charset = (charset or globals()["DEF_{}".format(encoding.upper())]).strip()
    except NameError:
        raise ValueError("Bad encoding")
    logger.debug("Unhidding data...")
    bits = b("")
    for token in b(encoded).split(b(sep)):
        bits += unhide_bits(token.strip(), charset, pad, n_pad) or b("")
    return b("").join(ts.bin2str(bits[i:i+8]) for i in range(0, len(bits), 8))


def unhide_bits(encoded, charset, pad="=", n_pad=8):
    def __gcd(a,b):
        while b > 0:
            a, b = b, a % b
        return a
    padding = encoded.count(b(pad))
    n_repr = int(math.ceil(math.log(len(charset), 2)))
    w_len  = n_repr * n_pad / __gcd(n_repr, n_pad)
    n_char = int(math.ceil(float(w_len) / n_repr))
    if encoded == "" or len(encoded) % n_char != 0 or padding == 0:
        return
    unused = {n: int(w_len - n * n_repr) % n_pad for n in range(n_char)}
    b_val  = bin(b(charset).index(encoded.rstrip(b(pad))[-1]))[2:].zfill(n_repr)
    return b(b_val[-unused[padding]:])


if __name__ == '__main__':
    parser.add_argument("-c", "--charset", help="characters set")
    parser.add_argument("-e", "--encoding", choices=["base32", "base64"], default="base64", help="character encoding")
    parser.add_argument("-f", "--exif-field", dest="exif", default="Comment", help="EXIF metadata field to be selected")
    parser.add_argument("-p", "--padding-char", default="=", help="padding character")
    parser.add_argument("-s", "--separator", default="\n", help="base-encoded token separator")
    initialize()
    data = b("").join(l for l in ts.stdin_pipe())
    if data.startswith(b("\x89PNG")) or data.startswith(b("\xff\xd8\xff\xe0")):
        try:
            data = exif(data, args.exif)
        except KeyError:
            logger.error("No EXIF field '%s'" % args.exif)
            sys.exit(1)
    print(ensure_str(unhide(data, args.encoding, args.charset, args.separator)))
