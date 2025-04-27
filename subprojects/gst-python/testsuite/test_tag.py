import unittest
from gi.repository import Gst, GstTag


class TestLibTag(unittest.TestCase):
    def setUp(self):
        Gst.init(None)

    @unittest.expectedFailure
    def testXmp(self):
        taglist = Gst.TagList.new_empty()
        # FIXME: Unable to register new tags currently. Gst.tag_register() is not available.
        taglist.add_value(Gst.TagMergeMode.REPLACE, "title", "my funny title")
        taglist.add_value(Gst.TagMergeMode.REPLACE, "geo-location-latitude", 23.25)

        xmp = GstTag.tag_list_to_xmp_buffer(
            taglist, True, ""
        )  # FIXME: doesn't copy taglist
        assert xmp is not None
        taglist2 = GstTag.tag_list_from_xmp_buffer(xmp)

        assert taglist2.n_tags() == 2
        assert taglist2.get_string("title")[1] == "my funny title"
        assert taglist2.get_double("geo-location-latitude")[1] == 23.25

    def testCopy(self):
        taglist = Gst.TagList.new_empty()
        taglist.add_value(Gst.TagMergeMode.APPEND, "title", "my funny title")
        taglist.add_value(Gst.TagMergeMode.REPLACE, Gst.TAG_ARTIST, "my funny artist")
        taglist2 = taglist.copy()
        assert taglist2.n_tags() == 2
        assert taglist2.get_string("title")[1] == "my funny title"
        assert taglist2.get_string("artist")[1] == "my funny artist"

    def testMerge(self):
        taglist = Gst.TagList.new_empty()
        taglist.add_value(Gst.TagMergeMode.APPEND, "title", "my funny title")
        taglist2 = Gst.TagList.new_empty()
        taglist2.add_value(Gst.TagMergeMode.APPEND, "album-artist", "artist2")
        taglist2 = taglist2.merge(taglist, Gst.TagMergeMode.APPEND)
        assert taglist2.n_tags() == 2
        assert taglist2.get_string("title")[1] == "my funny title"
        assert taglist2.get_string("album-artist")[1] == "artist2"


if __name__ == "__main__":
    import unittest

    unittest.main()
