import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstAudio", "1.0")

from gi.repository import Gst, GstAudio
import unittest


class Audio(unittest.TestCase):

    def testBufferclip(self):
        Gst.init(None)
        assert hasattr(GstAudio, "audio_buffer_clip")
        # create a segment
        segment = Gst.Segment.new()
        Gst.debug("Created the new segment")
        # we'll put a new segment of 500ms to 1000ms
        segment.init(Gst.Format.TIME)
        segment.start = Gst.SECOND // 2  # 500ms
        segment.stop = Gst.SECOND  # 1000ms
        Gst.debug("Initialized the new segment")
        # create a new dummy buffer
        b = Gst.Buffer.new_wrapped(b"this is a really useless line")
        Gst.debug("Created the buffer")
        # clip... which shouldn't do anything
        b2 = GstAudio.audio_buffer_clip(b, segment, 44100, 8)
        Gst.debug("DONE !")


if __name__ == "__main__":
    unittest.main()
