import os
import time
import tempfile

import gi

gi.require_version("Gst", "1.0")
from gi.repository import Gst

import unittest

Gst.init(None)


class EventTest(unittest.TestCase):
    def setUp(self):
        self.pipeline = Gst.parse_launch("fakesrc ! fakesink name=sink")
        self.sink = self.pipeline.get_by_name("sink")
        self.pipeline.set_state(Gst.State.PLAYING)

    def tearDown(self):
        self.pipeline.set_state(Gst.State.NULL)
        # Wait for the pipeline to fully stop
        self.pipeline.get_state(Gst.CLOCK_TIME_NONE)

    def testEventSeek(self):
        event = Gst.Event.new_seek(
            1.0,
            Gst.Format.BYTES,
            Gst.SeekFlags.FLUSH,
            Gst.SeekType.SET,
            0,
            Gst.SeekType.NONE,
            0,
        )
        assert event
        self.sink.send_event(event)

        rate, format, flags, start_type, start, stop_type, stop = event.parse_seek()
        assert (rate, format, flags, start_type, start, stop_type, stop) == (
            1.0,
            Gst.Format.BYTES,
            Gst.SeekFlags.FLUSH,
            Gst.SeekType.SET,
            0,
            Gst.SeekType.NONE,
            0,
        )

    def testWrongEvent(self):
        buffer = Gst.Buffer.new()
        with self.assertRaises(TypeError):
            self.sink.send_event(buffer)
        with self.assertRaises(TypeError):
            self.sink.send_event(1)


class EventFileSrcTest(unittest.TestCase):
    def setUp(self):
        self.filename = tempfile.mktemp()
        # Use a context manager to ensure the file is properly closed
        with open(self.filename, "w") as f:
            f.write("".join(map(str, list(range(10)))))

        self.pipeline = Gst.parse_launch(
            "filesrc name=source location=%s blocksize=1 ! fakesink signal-handoffs=1 name=sink"
            % self.filename
        )
        self.source = self.pipeline.get_by_name("source")
        self.sink = self.pipeline.get_by_name("sink")
        self.sigid = self.sink.connect("handoff", self.handoff_cb)
        self.bus = self.pipeline.get_bus()

    def tearDown(self):
        self.pipeline.set_state(Gst.State.NULL)
        self.sink.disconnect(self.sigid)
        if os.path.exists(self.filename):
            os.remove(self.filename)
        del self.bus
        del self.pipeline
        del self.source
        del self.sink
        del self.handoffs
        unittest.TestCase.tearDown(self)

    def handoff_cb(self, element, buffer, pad):
        content = buffer.extract_dup(0, buffer.get_size()).decode("utf-8")
        self.handoffs.append(content)

    def playAndIter(self):
        self.handoffs = []
        self.pipeline.set_state(Gst.State.PLAYING)
        assert self.pipeline.set_state(Gst.State.PLAYING)
        while 42:
            msg = self.bus.pop()
            if msg and msg.type == Gst.MessageType.EOS:
                break
        assert self.pipeline.set_state(Gst.State.PAUSED)
        handoffs = self.handoffs
        self.handoffs = []
        return handoffs

    def sink_seek(self, offset, method=Gst.SeekType.SET):
        self.sink.seek(
            1.0,
            Gst.Format.BYTES,
            Gst.SeekFlags.FLUSH,
            method,
            offset,
            Gst.SeekType.NONE,
            0,
        )

    def testSimple(self):
        handoffs = self.playAndIter()
        expected = list(map(str, list(range(10))))
        assert (
            handoffs == expected
        ), f"Handoffs don't match. Actual: {handoffs}, Expected: {expected}"

    def testSeekCur(self):
        self.sink_seek(8)
        self.playAndIter()


class TestEmit(unittest.TestCase):
    def testEmit(self):
        object = Gst.Bin()
        object.connect("notify", self._event_cb)
        object.set_property("name", "test-bin")
        object.notify("name")

    def _event_cb(self, obj, pspec):
        self.assertIsInstance(obj, Gst.Object)
        self.assertIsInstance(pspec, gi.repository.GObject.GParamSpec)


class TestDelayedEventProbe(unittest.TestCase):
    # this test:
    # starts a pipeline with only a source
    # adds an event probe to catch the (first) new-segment
    # adds a buffer probe to "autoplug" and send out this event
    def setUp(self):
        self.pipeline = Gst.Pipeline()
        self.src = Gst.ElementFactory.make("fakesrc")
        self.src.set_property("num-buffers", 10)
        self.pipeline.add(self.src)
        self.srcpad = self.src.get_static_pad("src")

    def tearDown(self):
        self.pipeline.set_state(Gst.State.NULL)
        del self.pipeline
        del self.src
        del self.srcpad

    def testProbe(self):
        self._event_probe_id = self.srcpad.add_probe(
            Gst.PadProbeType.EVENT_DOWNSTREAM, self._event_probe_cb
        )
        self._buffer_probe_id = self.srcpad.add_probe(
            Gst.PadProbeType.BUFFER, self._buffer_probe_cb
        )

        self._newsegment = None
        self._eos = None
        self._had_buffer = False

        self.pipeline.set_state(Gst.State.PLAYING)

        # Use a timeout to avoid infinite loop
        timeout = time.time() + 5  # 5 seconds timeout
        while not self._eos and time.time() < timeout:
            time.sleep(0.1)

        self.assertTrue(self._newsegment, "No SEGMENT event received")
        self.assertTrue(self._eos, "No EOS event received")

        # verify if our newsegment event is still around and valid
        self.assertEqual(self._newsegment.type, Gst.EventType.SEGMENT)

        # verify if our eos event is still around and valid
        self.assertEqual(self._eos.type, Gst.EventType.EOS)

    def _event_probe_cb(self, pad, info):
        event = info.get_event()
        if event.type == Gst.EventType.SEGMENT:
            self._newsegment = event
            return Gst.PadProbeReturn.DROP

        if event.type == Gst.EventType.EOS:
            self._eos = event
            return Gst.PadProbeReturn.PASS

        if event.type == Gst.EventType.LATENCY:
            return Gst.PadProbeReturn.PASS

        return Gst.PadProbeReturn.PASS

    def _buffer_probe_cb(self, pad, info):
        self.assertTrue(self._newsegment)

        # fake autoplugging by now putting in a fakesink
        sink = Gst.ElementFactory.make("fakesink")
        self.pipeline.add(sink)
        self.src.link(sink)
        sink.set_state(Gst.State.PLAYING)

        # Use get_static_pad instead of get_pad
        pad = sink.get_static_pad("sink")

        # Send stream-start event before segment event
        stream_start_event = Gst.Event.new_stream_start(f"stream-{id(self)}")
        pad.send_event(stream_start_event)

        pad.send_event(self._newsegment)

        # we don't want to be called again
        self.srcpad.remove_probe(self._buffer_probe_id)

        self._had_buffer = True
        # now let the buffer through
        return Gst.PadProbeReturn.PASS


class TestEventCreationParsing(unittest.TestCase):
    def testEventStep(self):
        e = Gst.Event.new_step(Gst.Format.TIME, 42, 1.0, True, True)

        assert e.type == Gst.EventType.STEP

        fmt, amount, rate, flush, intermediate = e.parse_step()
        assert fmt == Gst.Format.TIME
        assert amount == 42
        assert rate == 1.0
        assert flush is True
        assert intermediate is True


if __name__ == "__main__":
    unittest.main()
