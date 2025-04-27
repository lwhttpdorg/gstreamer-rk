import unittest
import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstPbutils", "1.0")
from gi.repository import Gst, GstPbutils

# Initialize GStreamer
Gst.init(None)


class TestDescriptions(unittest.TestCase):
    def test_source_description(self):
        assert hasattr(GstPbutils, "pb_utils_get_source_description")
        assert (
            GstPbutils.pb_utils_get_source_description("file") == "FILE protocol source"
        )

    def test_sink_description(self):
        assert hasattr(GstPbutils, "pb_utils_get_sink_description")
        assert GstPbutils.pb_utils_get_sink_description("file") == "FILE protocol sink"

    def test_decoder_description(self):
        assert hasattr(GstPbutils, "pb_utils_get_decoder_description")
        caps = Gst.Caps.from_string("audio/mpeg,mpegversion=1,layer=3")
        assert (
            GstPbutils.pb_utils_get_decoder_description(caps)
            == "MPEG-1 Layer 3 (MP3) decoder"
        )

    def test_codec_description(self):
        assert hasattr(GstPbutils, "pb_utils_get_codec_description")
        caps = Gst.Caps.from_string("audio/mpeg,mpegversion=1,layer=3")
        assert GstPbutils.pb_utils_get_codec_description(caps) == "MPEG-1 Layer 3 (MP3)"

    def test_encoder_description(self):
        assert hasattr(GstPbutils, "pb_utils_get_encoder_description")
        caps = Gst.Caps.from_string("audio/mpeg,mpegversion=1,layer=3")
        assert (
            GstPbutils.pb_utils_get_encoder_description(caps)
            == "MPEG-1 Layer 3 (MP3) encoder"
        )

    def test_element_description(self):
        assert hasattr(GstPbutils, "pb_utils_get_element_description")
        assert (
            GstPbutils.pb_utils_get_element_description("something")
            == "GStreamer element something"
        )

    def test_add_codec_description(self):
        assert hasattr(GstPbutils, "pb_utils_add_codec_description_to_tag_list")


# TODO
# Add tests for the other parts of GstPbutils:
# * missing-plugins
# * install-plugins (and check if they are available in the current GStreamer version)


if __name__ == "__main__":
    unittest.main()
