import time
import unittest
import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst, GLib

# Initialize GStreamer
Gst.init(None)


class TestConstruction(unittest.TestCase):
    def testGoodConstructor(self):
        name = "test-pipeline"
        pipeline = Gst.Pipeline.new(name)
        assert pipeline.__grefcount__ == 1
        assert pipeline is not None, "pipeline is None"
        assert isinstance(pipeline, Gst.Pipeline)
        assert pipeline.get_name() == name

    def testParseLaunch(self):
        pipeline = Gst.parse_launch("fakesrc ! fakesink")
        assert pipeline is not None, "parse_launch failed"


class Pipeline:
    def setUp(self):
        self.pipeline = Gst.Pipeline.new("test-pipeline")
        source = Gst.ElementFactory.make("fakesrc", "source")
        source.set_property("num-buffers", 5)
        sink = Gst.ElementFactory.make("fakesink", "sink")
        self.pipeline.add(source)
        self.pipeline.add(sink)
        source.link(sink)

    def tearDown(self):
        del self.pipeline

    def testRun(self):
        assert self.pipeline.get_state(0)[1] == Gst.State.NULL
        self.pipeline.set_state(Gst.State.PLAYING)
        assert self.pipeline.get_state(0)[1] == Gst.State.PLAYING

        time.sleep(1)

        assert self.pipeline.get_state(0)[1] == Gst.State.PLAYING
        self.pipeline.set_state(Gst.State.NULL)
        assert self.pipeline.get_state(0)[1] == Gst.State.NULL


class PipelineTags(unittest.TestCase):
    def setUp(self):
        self.pipeline = Gst.parse_launch(
            "audiotestsrc num-buffers=100 ! vorbisenc name=encoder ! oggmux name=muxer ! fakesink"
        )

    def tearDown(self):
        del self.pipeline

    def testRun(self):
        taglist = Gst.TagList.new_empty()
        taglist.add_value(Gst.TagMergeMode.APPEND, Gst.TAG_ARTIST, "artist")
        encoder = self.pipeline.get_by_name("encoder")
        encoder.merge_tags(taglist, Gst.TagMergeMode.APPEND)

        assert self.pipeline.get_state(0)[1] == Gst.State.NULL
        self.pipeline.set_state(Gst.State.PLAYING)

        time.sleep(1)

        assert self.pipeline.get_state(0)[1] == Gst.State.PLAYING
        self.pipeline.set_state(Gst.State.NULL)
        assert self.pipeline.get_state(0)[1] == Gst.State.NULL


class Bus(unittest.TestCase):
    def testGet(self):
        pipeline = Gst.Pipeline.new("test")
        bus = pipeline.get_bus()
        assert bus is not None, "Failed to get bus"


class PipelineAndBus(unittest.TestCase):
    def setUp(self):
        self.pipeline = Gst.Pipeline.new("test-pipeline")
        source = Gst.ElementFactory.make("fakesrc", "source")
        sink = Gst.ElementFactory.make("fakesink", "sink")
        self.pipeline.add(source)
        self.pipeline.add(sink)
        source.link(sink)

        self.bus = self.pipeline.get_bus()
        self.handler = self.bus.add_watch(GLib.PRIORITY_DEFAULT, self._message_received)

        self.loop = GLib.MainLoop()

    def tearDown(self):
        GLib.source_remove(self.handler)
        del self.pipeline
        del self.bus

    def _message_received(self, bus, message):
        t = message.type
        if t == Gst.MessageType.STATE_CHANGED:
            _old, new, _pending = message.parse_state_changed()
            if message.src == self.pipeline and new == self.final:
                self.loop.quit()
        return True

    def testPlaying(self):
        self.final = Gst.State.PLAYING
        ret = self.pipeline.set_state(Gst.State.PLAYING)
        assert ret == Gst.StateChangeReturn.ASYNC

        self.loop.run()

        self.final = Gst.State.READY
        ret = self.pipeline.set_state(Gst.State.READY)
        assert ret == Gst.StateChangeReturn.SUCCESS
        self.loop.run()

        assert self.pipeline.set_state(Gst.State.NULL) == Gst.StateChangeReturn.SUCCESS
        assert self.pipeline.get_state(0) == (
            Gst.StateChangeReturn.SUCCESS,
            Gst.State.NULL,
            Gst.State.VOID_PENDING,
        )


class TestPipeSub(Gst.Pipeline):
    def do_handle_message(self, message):
        Gst.Pipeline.do_handle_message(self, message)
        self.type = message.type


class TestPipeSubSub(TestPipeSub):
    def do_handle_message(self, message):
        TestPipeSub.do_handle_message(self, message)


class TestSubClass(unittest.TestCase):
    def setUp(self):
        pass

    def tearDown(self):
        pass

    def testSubClass(self):
        p = TestPipeSub()
        u = Gst.ElementFactory.make("uridecodebin")
        p.add(u)
        assert not hasattr(p, "type")

        p.set_state(Gst.State.READY)

        assert hasattr(p, "type")
        assert isinstance(p.type, Gst.MessageType)

    def testSubSubClass(self):
        p = TestPipeSubSub()
        u = Gst.ElementFactory.make("uridecodebin")
        p.add(u)
        assert not hasattr(p, "type")

        p.set_state(Gst.State.READY)

        assert hasattr(p, "type")
        assert isinstance(p.type, Gst.MessageType)


class TestBusOperations(unittest.TestCase):
    def setUp(self):
        self.pipeline = Gst.Pipeline.new("test-pipeline")
        self.source = Gst.ElementFactory.make("fakesrc", "source")
        self.sink = Gst.ElementFactory.make("fakesink", "sink")
        self.pipeline.add(self.source)
        self.pipeline.add(self.sink)
        self.source.link(self.sink)
        self.bus = self.pipeline.get_bus()

    def tearDown(self):
        self.pipeline.set_state(Gst.State.NULL)
        del self.pipeline
        del self.bus

    def testBusTimedPop(self):
        self.pipeline.set_state(Gst.State.PLAYING)

        # Wait for up to 1 second for a message
        message = self.bus.timed_pop(Gst.SECOND)

        assert message is not None, "No message received within timeout"
        assert (
            message.type == Gst.MessageType.STATE_CHANGED
        ), f"Unexpected message type: {message.type}"

    def testBusTimedPopFilter(self):
        self.pipeline.set_state(Gst.State.PLAYING)

        # Wait for up to 1 second for a specific message type
        message = self.bus.timed_pop_filtered(Gst.SECOND, Gst.MessageType.STATE_CHANGED)

        assert message is not None, "No state changed message received within timeout"
        assert message.type == Gst.MessageType.STATE_CHANGED

    def testBusPoll(self):
        self.pipeline.set_state(Gst.State.PLAYING)

        # Poll for a message with a timeout of 1 second
        message = self.bus.poll(
            Gst.MessageType.STATE_CHANGED | Gst.MessageType.ERROR, Gst.SECOND
        )

        assert message is not None, "No message received within timeout"
        assert (
            message.type == Gst.MessageType.STATE_CHANGED
        ), f"Unexpected message type: {message.type}"

    def testBusAddSignalWatch(self):
        self.message_received = False

        def on_message(bus, message):
            self.message_received = True
            return True

        self.bus.add_signal_watch()
        self.bus.connect("message", on_message)

        self.pipeline.set_state(Gst.State.PLAYING)

        # Run a main loop for a short time to process messages
        loop = GLib.MainLoop()
        GLib.timeout_add(100, loop.quit)
        loop.run()

        assert self.message_received, "No message received via signal watch"


if __name__ == "__main__":
    import unittest

    unittest.main()
