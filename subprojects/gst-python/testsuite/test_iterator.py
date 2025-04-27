import unittest
import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst


class IteratorTest(unittest.TestCase):
    def setUp(self):
        Gst.init(None)

    def testBinIterateElements(self):
        pipeline = Gst.parse_launch("fakesrc name=src ! fakesink name=sink")
        elements = list(pipeline.iterate_elements())
        fakesrc = pipeline.get_by_name("src")
        fakesink = pipeline.get_by_name("sink")

        assert len(elements) == 2
        assert fakesrc in elements
        assert fakesink in elements

        pipeline.remove(fakesrc)
        elements = list(pipeline.iterate_elements())

        assert len(elements) == 1
        assert fakesrc not in pipeline.iterate_elements()

    def testBinIterateSorted(self):
        pipeline = Gst.parse_launch("fakesrc name=src ! fakesink name=sink")
        elements = list(pipeline.iterate_sorted())
        fakesrc = pipeline.get_by_name("src")
        fakesink = pipeline.get_by_name("sink")

        assert elements[0] == fakesink
        assert elements[1] == fakesrc

    def testBinIterateRecurse(self):
        pipeline = Gst.parse_launch("fakesrc name=src ! fakesink name=sink")
        elements = list(pipeline.iterate_recurse())
        fakesrc = pipeline.get_by_name("src")
        fakesink = pipeline.get_by_name("sink")

        assert elements[0] == fakesink
        assert elements[1] == fakesrc

    def testBinIterateSinks(self):
        pipeline = Gst.parse_launch("fakesrc name=src ! fakesink name=sink")
        elements = list(pipeline.iterate_sinks())
        fakesrc = pipeline.get_by_name("src")
        fakesink = pipeline.get_by_name("sink")

        assert len(elements) == 1
        assert fakesink in elements
        assert fakesrc not in elements

    def testIteratePadsFakeSrc(self):
        fakesrc = Gst.ElementFactory.make("fakesrc", None)
        pads = list(fakesrc.iterate_pads())
        srcpad = fakesrc.get_static_pad("src")
        assert len(pads) == 1
        assert pads[0] == srcpad
        srcpads = list(fakesrc.iterate_src_pads())
        assert len(srcpads) == 1
        assert srcpads[0] == srcpad
        sinkpads = list(fakesrc.iterate_sink_pads())
        self.assertEqual(sinkpads, [])

        assert len(list(fakesrc.iterate_pads())) == 1
        for pad in fakesrc.iterate_pads():
            assert pad == srcpad
            break
        else:
            raise AssertionError

    def testIteratePadsFakeSink(self):
        fakesink = Gst.ElementFactory.make("fakesink", None)
        pads = list(fakesink.iterate_pads())
        sinkpad = fakesink.get_static_pad("sink")
        assert len(pads) == 1
        assert pads[0] == sinkpad
        srcpads = list(fakesink.iterate_src_pads())
        assert srcpads == []
        sinkpads = list(fakesink.iterate_sink_pads())
        self.assertEqual(len(sinkpads), 1)
        self.assertEqual(sinkpads[0], sinkpad)

        self.assertEqual(len(list(fakesink.iterate_pads())), 1)
        for pad in fakesink.iterate_pads():
            self.assertEqual(pad, sinkpad)
            break
        else:
            raise AssertionError

    def testInvalidIterator(self):
        p = Gst.Pad.new("p", Gst.PadDirection.SRC)
        # The C function will return NULL, we should
        # therefore have an exception raised
        with self.assertRaises(TypeError):
            list(p.iterate_internal_links())
        del p


if __name__ == "__main__":
    import unittest

    unittest.main()
