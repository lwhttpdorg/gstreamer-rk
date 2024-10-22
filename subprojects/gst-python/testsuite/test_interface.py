import unittest
from gi.repository import Gst, GObject, GstVideo

Gst.init(None)


def find_video_sink_element():
    """Searches for a video sink element"""
    registry = Gst.Registry.get()
    return next(
        (
            factory
            for factory in registry.get_feature_list(Gst.ElementFactory)
            if factory.get_klass().lower().find("sink/video") != -1
            and factory.get_name().startswith("x")
        ),
        None,
    )


class Availability(unittest.TestCase):
    def test_video_overlay(self):
        assert hasattr(GstVideo, "VideoOverlay")
        assert issubclass(GstVideo.VideoOverlay, GObject.GInterface)

    # Mixer interface is deprecated in GStreamer 1.0, so we remove this test


class FunctionCall(unittest.TestCase):
    def test_video_overlay(self):
        factory = find_video_sink_element()
        if not factory:
            assert False, "No video sink element available"

        element = factory.create(None)
        assert isinstance(element, Gst.Element)
        assert isinstance(element, GstVideo.VideoOverlay)

        # Note: set_window_handle expects a native window handle
        # For testing purposes, we're just checking if the method exists
        assert hasattr(element, "set_window_handle")


# MixerTest class is removed as the Mixer interface is deprecated in GStreamer 1.0

if __name__ == "__main__":
    unittest.main()
