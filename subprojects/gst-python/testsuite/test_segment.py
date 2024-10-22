import unittest
import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst, GLib

Gst.init(None)


class SegmentTest(unittest.TestCase):
    def testSeekNoSize(self):
        segment = Gst.Segment()
        segment.init(Gst.Format.BYTES)

        # configure segment to start at 100 with no defined stop position
        res = segment.do_seek(
            1.0,
            Gst.Format.BYTES,
            Gst.SeekFlags.NONE,
            Gst.SeekType.SET,
            100,
            Gst.SeekType.NONE,
            GLib.MAXUINT64,
        )
        assert res[0] is True
        assert segment.start == 100
        assert segment.stop == GLib.MAXUINT64

        res = segment.do_seek(
            1.0,
            Gst.Format.BYTES,
            Gst.SeekFlags.NONE,
            Gst.SeekType.SET,
            200,
            Gst.SeekType.SET,
            GLib.MAXUINT64,
        )

        assert res[0] is True
        assert segment.start == 200, f"Start should remain 100, but got {segment.start}"
        assert (
            segment.stop == GLib.MAXUINT64
        ), f"Stop should remain MAXUINT64, but got {segment.stop}"

        # clipping on outside range, always returns False
        res, cstart, cstop = segment.clip(Gst.Format.BYTES, 0, 50)
        assert res is False

        # touching lower bound but outside
        res, cstart, cstop = segment.clip(Gst.Format.BYTES, 50, 100)
        assert res is False

        # partially outside (now entirely outside due to start being 200)
        res, cstart, cstop = segment.clip(Gst.Format.BYTES, 50, 150)
        assert res is False

        # partially inside
        res, cstart, cstop = segment.clip(Gst.Format.BYTES, 150, 250)
        assert res is True
        assert cstart == 200
        assert cstop == 250


if __name__ == "__main__":
    unittest.main(verbosity=2)
