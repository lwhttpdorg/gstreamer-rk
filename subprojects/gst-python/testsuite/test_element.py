import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst

import unittest
import sys

Gst.init(None)


# since I can't subclass Gst.Element for some reason, I use a bin here
class TestElement(Gst.Bin):
    def break_it_down(self):
        Gst.debug("Hammer Time")


class ElementTest(unittest.TestCase):
    name = "fakesink"
    alias = "sink"

    def testGoodConstructor(self):
        element = Gst.ElementFactory.make(self.name, self.alias)
        assert element is not None, "element is None"
        assert isinstance(element, Gst.Element)
        assert element.get_name() == self.alias


class NonExistentTest(ElementTest):
    name = "this-element-does-not-exist"
    alias = "no-alias"

    def testGoodConstructor(self):
        pass

    def testGoodConstructor2(self):
        pass


class FileSrcTest(ElementTest):
    name = "filesrc"
    alias = "source"


class FileSinkTest(ElementTest):
    name = "filesink"
    alias = "sink"


class ElementName(unittest.TestCase):
    def testElementStateGetName(self):
        for state in ("NULL", "READY", "PLAYING", "PAUSED"):
            assert hasattr(Gst.State, state)
            attr = getattr(Gst.State, state)
            assert Gst.Element.state_get_name(attr) == state

        assert Gst.Element.state_get_name(Gst.State.VOID_PENDING) == "VOID_PENDING"
        try:
            Gst.Element.state_get_name(Gst.State(-1))
        except ValueError:
            pass
        else:
            self.fail("ValueError not raised")
        try:
            Gst.Element.state_get_name("")
        except TypeError:
            pass
        else:
            self.fail("TypeError not raised")


class QueryTest(unittest.TestCase):
    def setUp(self):
        self.pipeline = Gst.parse_launch("fakesrc name=source ! fakesink")
        assert self.pipeline.__grefcount__ == 1
        self.element = self.pipeline.get_by_name("source")
        assert self.element.__grefcount__ == 2
        assert sys.getrefcount(self.element) == 2

    def tearDown(self):
        del self.pipeline
        del self.element

    def testQuery(self):
        Gst.debug("querying fakesrc in FORMAT_BYTES")
        success, position = self.element.query_position(Gst.Format.BYTES)
        assert success
        assert position == 0
        success, position = self.element.query_position(Gst.Format.TIME)
        assert not success
        assert position == -1


class QueueTest(unittest.TestCase):
    def testConstruct(self):
        queue = Gst.ElementFactory.make("queue", None)
        assert queue.get_name().startswith("queue")


class DebugTest(unittest.TestCase):
    def testDebug(self):
        e = Gst.ElementFactory.make("fakesrc", None)
        Gst.error("I am an error string")
        Gst.warning("I am a warning string")
        Gst.info("I am an info string")
        Gst.debug("I am a debug string")
        Gst.log("I am a log string")
        Gst.debug("I am a formatted %s %s" % ("log", "string"))

    def testElementDebug(self):
        e = TestElement("testelement")
        e.set_property("name", "testelement")
        e.break_it_down()


class LinkTest(unittest.TestCase):
    def testLinkNoPads(self):
        src = Gst.Bin.new(None)
        sink = Gst.Bin.new(None)
        assert not src.link(sink)

    def testLink(self):
        src = Gst.ElementFactory.make("fakesrc", None)
        sink = Gst.ElementFactory.make("fakesink", None)
        self.assertTrue(src.link(sink))

    def testLinkPads(self):
        src = Gst.ElementFactory.make("fakesrc", None)
        sink = Gst.ElementFactory.make("fakesink", None)
        assert src.link_pads("src", sink, "sink")
        src.unlink_pads("src", sink, "sink")

    def testLinkFiltered(self):
        # a filtered link uses capsfilter and thus needs a bin
        bin = Gst.Bin.new(None)
        src = Gst.ElementFactory.make("fakesrc", None)
        sink = Gst.ElementFactory.make("fakesink", None)
        bin.add(src, sink)
        caps = Gst.caps_from_string("audio/x-raw,format=(int)")

        assert src.link_filtered(sink, caps)

        pad = src.get_static_pad("src")
        pad.unlink(pad.get_peer())
        sink_pad = sink.get_static_pad("sink")

        peer_pad = sink_pad.get_peer()
        assert peer_pad is None, "Sink pad should not have a peer"
        assert not sink_pad.is_linked(), "Sink pad should be unlinked"


if __name__ == "__main__":
    unittest.main()
