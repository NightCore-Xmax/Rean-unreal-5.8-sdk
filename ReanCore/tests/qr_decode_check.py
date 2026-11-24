#!/usr/bin/env python3
# Copyright (c) REAN Studios. MIT License.
"""Decodes the QR codes written by the ReanCore tests with independent decoders
(zxing-cpp, then OpenCV) and compares them with the expected text.

    REAN_QR_DUMP=/tmp/qr build/rean_core_tests
    pip install zxing-cpp opencv-python-headless numpy
    python3 ReanCore/tests/qr_decode_check.py /tmp/qr

Fails when a code decodes to the wrong text, or when no decoder can read it.
"""
import pathlib
import sys

import numpy as np  # type: ignore

try:
    import zxingcpp  # type: ignore
except ImportError:  # pragma: no cover
    zxingcpp = None
try:
    import cv2  # type: ignore
except ImportError:  # pragma: no cover
    cv2 = None


def read_pgm(path: pathlib.Path) -> np.ndarray:
    parts = path.read_bytes().split(b"\n", 3)
    width, height = (int(x) for x in parts[1].split())
    return np.frombuffer(parts[3], dtype=np.uint8).reshape(height, width)


def decode(pixels: np.ndarray):
    """Every text the available decoders read from the image."""
    texts = []
    if zxingcpp is not None:
        for result in zxingcpp.read_barcodes(pixels, formats=zxingcpp.BarcodeFormat.QRCode):
            texts.append(("zxing", result.text))
    if cv2 is not None:
        text, points, _ = cv2.QRCodeDetector().detectAndDecode(pixels)
        if points is not None and (text or not texts):
            texts.append(("opencv", text))
    return texts


def main() -> int:
    if zxingcpp is None and cv2 is None:
        print("Install zxing-cpp and/or opencv-python-headless")
        return 2
    images = sorted(pathlib.Path(sys.argv[1]).glob("*.pgm"))
    wrong = unread = 0
    for image in images:
        expected = image.with_suffix(".txt").read_bytes().decode("utf-8")
        results = decode(read_pgm(image))
        good = [name for name, text in results if text == expected]
        bad = [name for name, text in results if text and text != expected]
        if bad:
            wrong += 1
            print(f"WRONG  {image.name}: {', '.join(bad)} decoded different text")
        elif not good:
            unread += 1
            print(f"UNREAD {image.name}")
        else:
            print(f"ok     {image.name} ({', '.join(good)})")
    print(f"{len(images)} codes: {len(images) - wrong - unread} ok, {wrong} wrong, {unread} unread")
    return 1 if wrong or unread or not images else 0


if __name__ == "__main__":
    sys.exit(main())
